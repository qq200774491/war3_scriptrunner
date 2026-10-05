//! 一个 actor 独占宿主 stdin；读取、心跳和全局回包在等待 ACK 时仍能推进。
use crate::{
    global_archive::GlobalArchive,
    wire::{self, Message},
};
use anyhow::{Context, Result};
use serde_json::{json, Value};
use std::{collections::VecDeque, path::PathBuf, process::Stdio, time::Duration};
use tokio::{
    process::{Child, ChildStdin, Command},
    sync::{mpsc, oneshot, watch},
    task::JoinHandle,
    time::{timeout, Instant},
};

#[derive(Clone)]
pub struct HostConfig {
    pub executable: PathBuf,
    pub dll: PathBuf,
    pub scripts: PathBuf,
    pub archives: PathBuf,
}
#[derive(Debug)]
pub enum HostEvent {
    Ready,
    Log(String),
    Callback(Value),
    Failed(String),
}
struct Request {
    player: i32,
    name: String,
    payload: String,
    reply: oneshot::Sender<Result<u64>>,
}
#[derive(Clone)]
pub struct HostSender {
    commands: mpsc::Sender<Request>,
}
impl HostSender {
    pub async fn event(&self, player: i32, name: String, payload: String) -> Result<u64> {
        let (reply, receive) = oneshot::channel();
        self.commands
            .send(Request {
                player,
                name,
                payload,
                reply,
            })
            .await
            .context("host closed")?;
        receive
            .await
            .context("host stopped before acknowledgement")?
    }
}
pub struct Host {
    pub sender: HostSender,
    stop: watch::Sender<bool>,
    task: JoinHandle<()>,
}
impl Host {
    pub fn start(config: HostConfig, events: mpsc::Sender<HostEvent>) -> Self {
        let (commands, receive) = mpsc::channel(256);
        let (stop, mut stopping) = watch::channel(false);
        let task = tokio::spawn(async move {
            if let Err(error) = run(config, receive, stopping.clone(), &events).await {
                if !*stopping.borrow() {
                    tokio::select! {
                        _ = events.send(HostEvent::Failed(format!("{error:#}"))) => {},
                        _ = stopping.changed() => {},
                    }
                }
            }
        });
        Self {
            sender: HostSender { commands },
            stop,
            task,
        }
    }
    pub async fn close(self) {
        let _ = self.stop.send(true);
        let _ = self.task.await;
    }
}

struct Pending {
    sequence: u64,
    since: Instant,
    reply: Option<oneshot::Sender<Result<u64>>>,
}
struct Runtime {
    input: ChildStdin,
    pending: Option<Pending>,
    globals: VecDeque<(String, String)>,
    archive: Option<GlobalArchive>,
    sequence: u64,
    ready: bool,
    heartbeat: Instant,
}
impl Runtime {
    async fn send(
        &mut self,
        kind: u8,
        player: i32,
        name: &str,
        payload: &str,
        reply: Option<oneshot::Sender<Result<u64>>>,
    ) -> Result<()> {
        self.sequence += 1;
        let bytes = wire::event(kind, self.sequence, player, name, payload)?;
        timeout(Duration::from_secs(5), wire::write(&mut self.input, &bytes)).await??;
        self.pending = Some(Pending {
            sequence: self.sequence,
            since: Instant::now(),
            reply,
        });
        Ok(())
    }
    fn message(
        &mut self,
        message: Message,
        config: &HostConfig,
        events: &mpsc::Sender<HostEvent>,
    ) -> Result<()> {
        match message {
            Message::Log { level, text } => {
                events.try_send(HostEvent::Log(format!("[云脚本 {level}] {text}")))?
            }
            Message::Event {
                player,
                name,
                payload,
            } => {
                let mut event = json!({"kind":"event","player":player,"event":name});
                match String::from_utf8(payload) {
                    Ok(text) => event["payload"] = text.into(),
                    Err(error) => {
                        event["payload"] = hex::encode(error.as_bytes()).into();
                        event["payload_encoding"] = "hex".into();
                    }
                }
                events.try_send(HostEvent::Callback(event))?;
            }
            Message::Ready => {
                anyhow::ensure!(!self.ready, "duplicate host readiness");
                self.ready = true;
                self.heartbeat = Instant::now();
                events.try_send(HostEvent::Ready)?;
            }
            Message::Ack(sequence) => {
                let pending = self
                    .pending
                    .take()
                    .context("unexpected host acknowledgement")?;
                anyhow::ensure!(
                    pending.sequence == sequence,
                    "host acknowledgement sequence mismatch"
                );
                if let Some(reply) = pending.reply {
                    let _ = reply.send(Ok(sequence));
                }
            }
            Message::Failure(reason) => anyhow::bail!("{reason}"),
            Message::Heartbeat => self.heartbeat = Instant::now(),
            Message::Global {
                transaction,
                operation,
                key,
                argument,
            } => {
                anyhow::ensure!(self.globals.len() < 256, "global request queue full");
                if self.archive.is_none() {
                    self.archive = Some(GlobalArchive::open(&config.archives)?);
                }
                let (name, result) = self.archive.as_mut().unwrap().request(
                    transaction,
                    &operation,
                    &key,
                    &argument,
                )?;
                self.globals
                    .push_back((name, serde_json::to_string(&result)?));
            }
        }
        Ok(())
    }
}

async fn run(
    config: HostConfig,
    mut commands: mpsc::Receiver<Request>,
    mut stop: watch::Receiver<bool>,
    events: &mpsc::Sender<HostEvent>,
) -> Result<()> {
    let mut child = Command::new(&config.executable)
        .arg(&config.dll)
        .arg(&config.scripts)
        .arg("0,1")
        .arg(&config.archives)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .creation_flags(0x08000000)
        .kill_on_drop(true)
        .spawn()
        .context("启动 script_host.exe")?;
    let mut output = child.stdout.take().context("missing host stdout")?;
    let (frames, mut receive) = mpsc::channel(256);
    let reader = tokio::spawn(async move {
        loop {
            let frame = wire::read(&mut output).await;
            let failed = frame.is_err();
            if frames.send(frame).await.is_err() || failed {
                break;
            }
        }
    });
    let mut runtime = Runtime {
        input: child.stdin.take().context("missing host stdin")?,
        pending: None,
        globals: VecDeque::new(),
        archive: None,
        sequence: 0,
        ready: false,
        heartbeat: Instant::now(),
    };
    let result = async {
        let start = Instant::now();
        let mut tick = tokio::time::interval(Duration::from_millis(250));
        loop {
            if *stop.borrow() { break; }
            if runtime.ready && runtime.pending.is_none() {
                if let Some((name, payload)) = runtime.globals.pop_front() {
                    runtime.send(0x12, -1, &name, &payload, None).await?;
                }
            }
            tokio::select! {
                _ = stop.changed() => break,
                frame = receive.recv() => runtime.message(frame.context("host stdout closed")??, &config, events)?,
                request = commands.recv(), if runtime.ready && runtime.pending.is_none() => {
                    let Some(request) = request else { break; };
                    runtime.send(0x10, request.player, &request.name, &request.payload, Some(request.reply)).await?;
                },
                _ = tick.tick() => {
                    anyhow::ensure!(runtime.ready || start.elapsed() < Duration::from_secs(20), "host startup timed out");
                    anyhow::ensure!(!runtime.ready || runtime.heartbeat.elapsed() < Duration::from_secs(20), "host heartbeat timed out");
                    anyhow::ensure!(runtime.pending.as_ref().is_none_or(|p| p.since.elapsed() < Duration::from_secs(20)), "host request timed out");
                }
            }
        }
        Ok(())
    }.await;
    // 所有退出路径都回收子进程；停止不依赖 GUI 是否仍在接收日志。
    shutdown(&mut child, &mut runtime.input).await;
    reader.abort();
    let _ = reader.await;
    result
}

async fn shutdown(child: &mut Child, input: &mut ChildStdin) {
    let _ = timeout(Duration::from_millis(500), wire::write(input, &[0x11])).await;
    if timeout(Duration::from_secs(1), child.wait()).await.is_err() {
        let _ = child.kill().await;
        let _ = child.wait().await;
    }
}
