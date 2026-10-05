use crate::host::{Host, HostConfig, HostEvent, HostSender};
use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::sync::Arc;
use std::{collections::HashMap, time::Duration};
use subtle::ConstantTimeEq;
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    net::{TcpListener, TcpStream},
    sync::{mpsc, oneshot, watch, Mutex},
    task::{JoinHandle, JoinSet},
    time::timeout,
};

const MAX_LINE: usize = 256 * 1024;
type Outbox = mpsc::Sender<Value>;
type Cache = HashMap<(usize, String), ([u8; 32], u64)>;
#[derive(Debug, Clone, Serialize)]
pub struct Profile {
    pub version: u32,
    pub room: String,
    pub player: usize,
    pub token: String,
    pub host: String,
    pub port: u16,
}
#[derive(Debug, Clone, Default)]
pub struct Status {
    pub connected: usize,
    pub active: bool,
    pub delivery: u64,
    pub failure: Option<String>,
}
pub struct Room {
    pub profiles: [Profile; 2],
    pub status: watch::Receiver<Status>,
    stop: watch::Sender<bool>,
    task: JoinHandle<()>,
}
impl Room {
    pub async fn start(
        config: HostConfig,
        relay: Option<usize>,
        logs: mpsc::Sender<String>,
    ) -> Result<Self> {
        anyhow::ensure!(relay.is_none_or(|v| v < 2), "invalid relay");
        let listener = TcpListener::bind("127.0.0.1:0").await?;
        let port = listener.local_addr()?.port();
        let room = uuid::Uuid::new_v4().to_string();
        let profiles = std::array::from_fn(|player| Profile {
            version: 1,
            room: room.clone(),
            player,
            token: hex::encode(rand::random::<[u8; 32]>()),
            host: "127.0.0.1".into(),
            port,
        });
        let (stop, stopping) = watch::channel(false);
        let (status, status_rx) = watch::channel(Status::default());
        let profiles_copy = profiles.clone();
        let task = tokio::spawn(async move {
            serve(
                listener,
                profiles_copy,
                config,
                relay,
                logs,
                status,
                stopping,
            )
            .await;
        });
        Ok(Self {
            profiles,
            status: status_rx,
            stop,
            task,
        })
    }
    pub async fn close(self) {
        let _ = self.stop.send(true);
        let _ = self.task.await;
    }
}

enum Incoming {
    Hello(Value, Outbox, oneshot::Sender<Result<usize>>),
    Left(usize, String),
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Request {
    id: String,
    event: String,
    payload: String,
}
impl Request {
    fn parse(value: Value) -> Result<Self> {
        let request: Self = serde_json::from_value(value)?;
        anyhow::ensure!(
            (1..=64).contains(&request.id.chars().count())
                && (1..=64).contains(&request.event.len())
                && !request.event.starts_with('_')
                && !request.event.contains('\0')
                && request.payload.len() <= 128 * 1024
                && !request.payload.contains('\0'),
            "invalid request content"
        );
        Ok(request)
    }
}
struct Requests {
    active: watch::Receiver<Status>,
    sender: Mutex<Option<HostSender>>,
    cache: Mutex<Cache>,
}
impl Requests {
    async fn request(&self, player: usize, message: Value) -> Result<Value> {
        let request = Request::parse(message)?;
        let fingerprint: [u8; 32] = Sha256::digest(serde_json::to_vec(
            &json!({"event":request.event,"payload":request.payload}),
        )?)
        .into();
        let key = (player, request.id.clone());
        // 与原 Python request_lock 一样：检查、执行、记录构成一次串行事务。
        let mut cache = self.cache.lock().await;
        anyhow::ensure!(
            self.active.borrow().active && self.active.borrow().failure.is_none(),
            "room not ready"
        );
        if let Some((previous, sequence)) = cache.get(&key) {
            anyhow::ensure!(
                *previous == fingerprint,
                "request id reused with different content"
            );
            return Ok(json!({"kind":"ack","id":request.id,"sequence":sequence,"duplicate":true}));
        }
        anyhow::ensure!(
            cache.len() < 10000,
            "room request limit reached; open a new room"
        );
        let sender = self.sender.lock().await.clone().context("room not ready")?;
        let sequence = sender
            .event(player as i32, request.event, request.payload)
            .await?;
        cache.insert(key, (fingerprint, sequence));
        Ok(json!({"kind":"ack","id":request.id,"sequence":sequence,"duplicate":false}))
    }
}

fn authenticate(
    hello: &Value,
    profiles: &[Profile; 2],
    sessions: &HashMap<usize, Outbox>,
) -> Result<usize> {
    let player = hello["player"]
        .as_u64()
        .filter(|v| *v < 2)
        .context("invalid player identity")? as usize;
    let profile = &profiles[player];
    let token = hello["token"]
        .as_str()
        .context("invalid room credentials")?;
    anyhow::ensure!(
        hello["version"] == 1
            && hello["room"] == profile.room
            && bool::from(token.as_bytes().ct_eq(profile.token.as_bytes())),
        "invalid room credentials"
    );
    anyhow::ensure!(!sessions.contains_key(&player), "player already connected");
    Ok(player)
}
fn send(sessions: &HashMap<usize, Outbox>, player: usize, value: Value) -> Result<()> {
    sessions
        .get(&player)
        .context("callback target not in connected room")?
        .try_send(value)?;
    Ok(())
}

async fn serve(
    listener: TcpListener,
    profiles: [Profile; 2],
    config: HostConfig,
    relay: Option<usize>,
    logs: mpsc::Sender<String>,
    status: watch::Sender<Status>,
    mut stop: watch::Receiver<bool>,
) {
    let (incoming, mut messages) = mpsc::channel(256);
    let (events, mut host_events) = mpsc::channel(256);
    let requests = Arc::new(Requests {
        active: status.subscribe(),
        sender: Mutex::new(None),
        cache: Mutex::new(HashMap::new()),
    });
    let mut sessions = HashMap::new();
    let mut clients = JoinSet::new();
    let mut host = None;
    let mut current = Status::default();
    let result: Result<()> = async {
        loop {
            if *stop.borrow() { break; }
            tokio::select! {
                _ = stop.changed() => break,
                accepted = listener.accept() => {
                    let (socket, _) = accepted?;
                    socket.set_nodelay(true)?;
                    // 拒绝多余的未认证连接，不让它们终止已认证的房间。
                    if clients.len() < 32 { clients.spawn(client(socket, incoming.clone(), requests.clone())); }
                },
                Some(_) = clients.join_next(), if !clients.is_empty() => {},
                Some(message) = messages.recv() => match message {
                    Incoming::Hello(hello, outbox, answer) => match authenticate(&hello, &profiles, &sessions) {
                        Ok(player) => {
                            outbox.try_send(json!({"kind":"joined","room":profiles[0].room,"player":player}))?;
                            sessions.insert(player, outbox);
                            current.connected = sessions.len();
                            status.send_replace(current.clone());
                            let _ = answer.send(Ok(player));
                            if sessions.len() == 2 && host.is_none() {
                                let native = Host::start(config.clone(), events.clone());
                                *requests.sender.lock().await = Some(native.sender.clone());
                                host = Some(native);
                            }
                        },
                        Err(error) => { let _ = answer.send(Err(error)); }
                    },
                    Incoming::Left(player, reason) => anyhow::bail!("player {player} disconnected; reconnect requires a new room: {reason}"),
                },
                Some(event) = host_events.recv() => match event {
                    HostEvent::Ready => {
                        current.active = true;
                        status.send_replace(current.clone());
                        for outbox in sessions.values() {
                            outbox.try_send(json!({"kind":"ready","room":profiles[0].room,"players":[0,1]}))?;
                        }
                    },
                    HostEvent::Log(line) => { logs.try_send(line)?; },
                    HostEvent::Failed(reason) => anyhow::bail!("{reason}"),
                    HostEvent::Callback(event) => {
                        route(event, &profiles[0].room, relay, &sessions, &mut current)?;
                        status.send_replace(current.clone());
                    }
                }
            }
        }
        Ok(())
    }.await;
    current.active = false;
    let failed = result.is_err();
    if let Err(error) = result {
        let reason = format!("{error:#}");
        current.failure = Some(reason.clone());
        for outbox in sessions.values() {
            let _ = outbox
                .try_send(json!({"kind":"room_failed","room":profiles[0].room,"reason":reason}));
        }
        let _ = logs.try_send(format!("[故障] {reason}"));
    }
    status.send_replace(current);
    if let Some(host) = host {
        host.close().await;
    }
    if failed {
        // 失败消息必须实际写到 socket 后才能回收连接任务。
        let _ = timeout(Duration::from_secs(1), async {
            while clients.join_next().await.is_some() {}
        })
        .await;
    }
    clients.abort_all();
    while clients.join_next().await.is_some() {}
}

fn route(
    mut event: Value,
    room: &str,
    relay: Option<usize>,
    sessions: &HashMap<usize, Outbox>,
    status: &mut Status,
) -> Result<()> {
    anyhow::ensure!(
        serde_json::to_vec(&event)?.len() <= MAX_LINE - 256,
        "callback exceeds client frame limit"
    );
    let target = event["player"]
        .as_i64()
        .context("invalid callback player")?;
    anyhow::ensure!(
        target == -1 || (target >= 0 && sessions.contains_key(&(target as usize))),
        "callback target not in connected room"
    );
    status.delivery += 1;
    event["room"] = room.into();
    event["delivery"] = status.delivery.into();
    if let Some(player) = relay {
        return send(sessions, player, event);
    }
    if target != -1 {
        return send(sessions, target as usize, event);
    }
    for outbox in sessions.values() {
        outbox.try_send(event.clone())?;
    }
    Ok(())
}

async fn read_json(reader: &mut BufReader<tokio::net::tcp::OwnedReadHalf>) -> Result<Value> {
    // fill_buf 限制累计大小，拒绝无换行的大包，不能先无限分配再检查。
    let mut bytes = Vec::new();
    loop {
        let buffer = reader.fill_buf().await?;
        anyhow::ensure!(!buffer.is_empty(), "client disconnected");
        let count = buffer
            .iter()
            .position(|b| *b == b'\n')
            .map(|v| v + 1)
            .unwrap_or(buffer.len());
        anyhow::ensure!(
            bytes.len() + count <= MAX_LINE,
            "client frame exceeds limit"
        );
        let done = buffer[count - 1] == b'\n';
        bytes.extend_from_slice(&buffer[..count]);
        reader.consume(count);
        if done {
            break;
        }
    }
    let value: Value = serde_json::from_slice(&bytes)?;
    anyhow::ensure!(value.is_object(), "expected JSON object");
    Ok(value)
}
async fn client(socket: TcpStream, incoming: mpsc::Sender<Incoming>, requests: Arc<Requests>) {
    let (read, mut write) = socket.into_split();
    let mut reader = BufReader::new(read);
    let (outbox, mut outgoing) = mpsc::channel::<Value>(256);
    let mut player = None;
    let pump = async {
        while let Some(message) = outgoing.recv().await {
            let mut bytes = serde_json::to_vec(&message)?;
            bytes.push(b'\n');
            timeout(Duration::from_secs(5), write.write_all(&bytes)).await??;
            if message["kind"] == "room_failed" {
                break;
            }
        }
        Ok::<(), anyhow::Error>(())
    };
    let receive = async {
        let hello = timeout(Duration::from_secs(5), read_json(&mut reader)).await??;
        let (answer, result) = oneshot::channel();
        incoming
            .send(Incoming::Hello(hello, outbox.clone(), answer))
            .await?;
        let identity = match result.await? {
            Ok(identity) => identity,
            Err(error) => {
                outbox
                    .send(json!({"kind":"error","reason":error.to_string()}))
                    .await?;
                tokio::time::sleep(Duration::from_millis(50)).await;
                return Err(error);
            }
        };
        player = Some(identity);
        loop {
            let message = timeout(Duration::from_secs(3600), read_json(&mut reader)).await??;
            let id = message.get("id").cloned();
            let response = match requests.request(identity, message).await {
                Ok(ack) => ack,
                Err(error) => json!({"kind":"error","id":id,"reason":error.to_string()}),
            };
            outbox.send(response).await?;
        }
    };
    let result: Result<()> = tokio::select! {result=pump=>result,result=receive=>result};
    if let Some(player) = player {
        let _ = incoming
            .send(Incoming::Left(
                player,
                result.err().map(|e| e.to_string()).unwrap_or_default(),
            ))
            .await;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_spoofed_and_reserved_requests() {
        for value in [
            json!({"id":"1","event":"normal","payload":"","player":1}),
            json!({"id":"1","event":"_msga_reply","payload":""}),
            json!({"id":true,"event":"normal","payload":""}),
        ] {
            assert!(Request::parse(value).is_err());
        }
    }
}
