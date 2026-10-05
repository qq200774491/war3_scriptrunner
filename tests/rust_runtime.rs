//! 直接加载原版 DLL，验证 Rust 服务与已有 TCP / 宿主协议的真实边界。
use scriptroom::{
    host::{Host, HostConfig, HostEvent},
    room::{Profile, Room},
};
use serde_json::{json, Value};
use std::{
    collections::VecDeque,
    path::{Path, PathBuf},
    time::Duration,
};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    net::{
        tcp::{OwnedReadHalf, OwnedWriteHalf},
        TcpStream,
    },
    sync::mpsc,
    time::timeout,
};

struct Client {
    input: BufReader<OwnedReadHalf>,
    output: OwnedWriteHalf,
    inbox: VecDeque<Value>,
}
impl Client {
    async fn connect(profile: &Profile) -> Self {
        let stream = TcpStream::connect(("127.0.0.1", profile.port))
            .await
            .unwrap();
        let (input, output) = stream.into_split();
        let mut client = Self {
            input: BufReader::new(input),
            output,
            inbox: VecDeque::new(),
        };
        client.send(serde_json::to_value(profile).unwrap()).await;
        client.expect("joined").await;
        client
    }
    async fn send(&mut self, value: Value) {
        self.output
            .write_all(format!("{value}\n").as_bytes())
            .await
            .unwrap();
    }
    async fn expect(&mut self, kind: &str) -> Value {
        if let Some(index) = self.inbox.iter().position(|v| v["kind"] == kind) {
            return self.inbox.remove(index).unwrap();
        }
        timeout(Duration::from_secs(25), async {
            loop {
                let mut line = String::new();
                assert!(
                    self.input.read_line(&mut line).await.unwrap() > 0,
                    "connection closed waiting for {kind}"
                );
                let value: Value = serde_json::from_str(&line).unwrap();
                if value["kind"] == kind {
                    return value;
                }
                assert!(value["kind"] != "room_failed", "unexpected failure {value}");
                self.inbox.push_back(value);
            }
        })
        .await
        .unwrap()
    }
    async fn exchange(&mut self, id: &str, event: &str, payload: &str) -> (Value, Value) {
        self.send(json!({"id":id,"event":event,"payload":payload}))
            .await;
        let reply = self.expect("event").await;
        let ack = self.expect("ack").await;
        (reply, ack)
    }
}
fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}
fn host_config(scripts: &Path, archives: &Path) -> HostConfig {
    HostConfig {
        executable: root().join("build/Release/script_host.exe"),
        dll: root().join("scriptrunner.dll"),
        scripts: scripts.into(),
        archives: archives.into(),
    }
}
async fn room(relay: Option<usize>) -> (Room, [Client; 2], tempfile::TempDir) {
    let state = tempfile::tempdir().unwrap();
    let (logs, mut receiver) = mpsc::channel(2048);
    tokio::spawn(async move { while receiver.recv().await.is_some() {} });
    let room = Room::start(
        host_config(&root().join("tests/scripts/counter"), state.path()),
        relay,
        logs,
    )
    .await
    .unwrap();
    let mut zero = Client::connect(&room.profiles[0]).await;
    let mut one = Client::connect(&room.profiles[1]).await;
    zero.expect("ready").await;
    one.expect("ready").await;
    (room, [zero, one], state)
}
#[tokio::test]
async fn real_dll_shared_state_dedup_and_identity() {
    let (room, [mut zero, mut one], _state) = room(None).await;
    let (reply, ack) = zero.exchange("same", "increment", "7").await;
    assert_eq!(reply["payload"], "7");
    assert_eq!(reply["player"], 0);
    assert_eq!(
        one.exchange("same", "increment", "5").await.0["payload"],
        "12"
    );
    zero.send(json!({"id":"same","event":"increment","payload":"7"}))
        .await;
    let repeat = zero.expect("ack").await;
    assert_eq!(repeat["duplicate"], true);
    assert_eq!(repeat["sequence"], ack["sequence"]);
    zero.send(json!({"id":"same","event":"increment","payload":"100"}))
        .await;
    assert!(zero.expect("error").await["reason"]
        .as_str()
        .unwrap()
        .contains("different content"));
    zero.send(json!({"id":"spoof","event":"increment","payload":"100","player":1}))
        .await;
    zero.expect("error").await;
    assert_eq!(one.exchange("query", "query", "").await.0["payload"], "12");
    room.close().await;
}
#[tokio::test]
async fn concurrent_requests_timer_broadcast_and_disconnect() {
    let (room, [mut zero, mut one], _state) = room(None).await;
    for i in 0..20 {
        let id = i.to_string();
        tokio::join!(
            zero.exchange(&id, "increment", "1"),
            one.exchange(&id, "increment", "1")
        );
    }
    assert_eq!(one.exchange("query", "query", "").await.0["payload"], "40");
    assert_eq!(
        zero.exchange("later", "later", "deferred").await.0["payload"],
        "deferred"
    );
    one.send(json!({"id":"broadcast","event":"broadcast","payload":"both"}))
        .await;
    assert_eq!(zero.expect("event").await["payload"], "both");
    assert_eq!(one.expect("event").await["payload"], "both");
    one.expect("ack").await;
    drop(zero);
    assert!(one.expect("room_failed").await["reason"]
        .as_str()
        .unwrap()
        .contains("disconnected"));
    room.close().await;
}
#[tokio::test]
async fn relay_and_lua_failure() {
    let (room, [mut zero, mut one], _state) = room(Some(0)).await;
    one.send(json!({"id":"relay","event":"increment","payload":"3"}))
        .await;
    let event = zero.expect("event").await;
    assert_eq!(event["player"], 1);
    assert_eq!(event["payload"], "3");
    assert_eq!(event["delivery"], 1);
    one.expect("ack").await;
    zero.send(json!({"id":"failure","event":"fail","payload":""}))
        .await;
    assert!(one.expect("room_failed").await["reason"]
        .as_str()
        .unwrap()
        .contains("Lua callback"));
    room.close().await;
}
#[tokio::test]
async fn invalid_credentials_do_not_break_existing_room() {
    let (room, [mut zero, _one], _state) = room(None).await;
    let stream = TcpStream::connect(("127.0.0.1", room.profiles[0].port))
        .await
        .unwrap();
    let (input, output) = stream.into_split();
    let mut intruder = Client {
        input: BufReader::new(input),
        output,
        inbox: VecDeque::new(),
    };
    let mut profile = serde_json::to_value(&room.profiles[0]).unwrap();
    profile["token"] = "invalid".into();
    intruder.send(profile).await;
    intruder.expect("error").await;
    assert_eq!(zero.exchange("query", "query", "").await.0["payload"], "0");
    room.close().await;
}
#[tokio::test]
async fn real_cloud_script_initializes_with_isolated_archives() {
    let scripts = PathBuf::from("D:/Code/MlScriptFolder/script_compile");
    assert!(
        scripts.join("main.lua").is_file(),
        "integration requires local cloud scripts"
    );
    let directory = tempfile::tempdir().unwrap();
    let (events, mut receive) = mpsc::channel(2048);
    let host = Host::start(host_config(&scripts, directory.path()), events);
    timeout(Duration::from_secs(25), async {
        loop {
            match receive.recv().await.unwrap() {
                HostEvent::Ready => break,
                HostEvent::Failed(reason) => panic!("{reason}"),
                _ => {}
            }
        }
    })
    .await
    .unwrap();
    host.close().await;
}

#[tokio::test]
async fn native_global_hash_lease_and_broadcast_roundtrip() {
    let directory = tempfile::tempdir().unwrap();
    let (events, mut receive) = mpsc::channel(2048);
    let host = Host::start(
        host_config(&root().join("tests/scripts/global"), directory.path()),
        events,
    );
    timeout(Duration::from_secs(25), async {
        loop {
            match receive.recv().await.unwrap() {
                HostEvent::Callback(event) => {
                    assert_eq!(event["payload"], "global native chain");
                    break;
                }
                HostEvent::Failed(reason) => panic!("{reason}"),
                _ => {}
            }
        }
    })
    .await
    .unwrap();
    host.close().await;
    let database = rusqlite::Connection::open(directory.path().join("global.sqlite3")).unwrap();
    let value: String = database
        .query_row(
            "SELECT value FROM fields WHERE key='hash' AND field='owner'",
            [],
            |row| row.get(0),
        )
        .unwrap();
    assert_eq!(value, "玩家一");
}

#[tokio::test]
async fn stop_hung_host_releases_native_archive_lock() {
    let directory = tempfile::tempdir().unwrap();
    std::fs::write(directory.path().join("main.lua"), "while true do end").unwrap();
    let state = directory.path().join("state");
    let (events, _receive) = mpsc::channel(2048);
    let host = Host::start(host_config(directory.path(), &state), events);
    tokio::time::sleep(Duration::from_millis(300)).await;
    timeout(Duration::from_secs(3), host.close()).await.unwrap();
    // 同目录再开宿主成功，证明旧子进程及其存档锁已被释放。
    let (events, mut receive) = mpsc::channel(2048);
    let host = Host::start(
        host_config(&root().join("tests/scripts/counter"), &state),
        events,
    );
    timeout(Duration::from_secs(25), async {
        loop {
            match receive.recv().await.unwrap() {
                HostEvent::Ready => break,
                HostEvent::Failed(reason) => panic!("{reason}"),
                _ => {}
            }
        }
    })
    .await
    .unwrap();
    host.close().await;
}
