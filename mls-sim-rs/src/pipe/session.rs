use std::sync::{Arc, RwLock};
use std::time::Duration;
use tokio::io::{AsyncRead, AsyncWrite};
use tokio::sync::{broadcast, mpsc};

use super::protocol::{
    read_frame, write_frame, ErrorPayload, EventPayload, Frame, HelloAckPayload, HelloPayload,
};
use crate::room::{OutEvent, RoomManager, ERR_OK, ERR_ROOM_NOT_EXIST};

const HELLO_TIMEOUT: Duration = Duration::from_secs(10);

pub async fn run_session<S>(stream: S, manager: Arc<RwLock<RoomManager>>)
where
    S: AsyncRead + AsyncWrite + Unpin + Send + 'static,
{
    let (mut reader, mut writer) = tokio::io::split(stream);

    let hello = match tokio::time::timeout(HELLO_TIMEOUT, read_frame(&mut reader)).await {
        Ok(Ok(Frame::Hello(p))) => p,
        Ok(Ok(other)) => {
            tracing::warn!("pipe: expected Hello, got {:?}", other);
            let _ = write_frame(
                &mut writer,
                &Frame::Error(ErrorPayload {
                    errnu: 1,
                    message: "expected Hello as first frame".into(),
                }),
            )
            .await;
            return;
        }
        Ok(Err(e)) => {
            tracing::warn!("pipe: read hello failed: {}", e);
            return;
        }
        Err(_) => {
            tracing::warn!("pipe: hello timeout");
            return;
        }
    };

    let HelloPayload {
        room_id,
        player_index,
        name,
        ..
    } = hello;

    let (out_rx, errnu) = subscribe_and_join(&manager, &room_id, player_index, &name);

    let ack = Frame::HelloAck(HelloAckPayload {
        ok: errnu == ERR_OK,
        errnu,
        message: if out_rx.is_none() {
            format!("room not found: {}", room_id)
        } else {
            String::new()
        },
    });
    if write_frame(&mut writer, &ack).await.is_err() {
        return;
    }

    let mut out_rx = match out_rx {
        Some(rx) => rx,
        None => return,
    };

    tracing::info!(
        "pipe: client connected room={} player={}",
        room_id,
        player_index
    );

    let (ctrl_tx, mut ctrl_rx) = mpsc::unbounded_channel::<Frame>();
    let pi = player_index;
    let writer_task = tokio::spawn(async move {
        loop {
            tokio::select! {
                ctrl = ctrl_rx.recv() => {
                    match ctrl {
                        Some(f) => {
                            if write_frame(&mut writer, &f).await.is_err() {
                                return;
                            }
                        }
                        None => return,
                    }
                }
                ev = out_rx.recv() => {
                    match ev {
                        Ok(ev) => {
                            if event_targets_player(&ev, pi) {
                                let f = Frame::Event(EventPayload {
                                    dir: "s2c".into(),
                                    ename: ev.ename.clone(),
                                    evalue: ev.evalue.clone(),
                                    ts: ev.timestamp,
                                    player_index: ev.player_index,
                                    room_id: ev.room_id.clone(),
                                });
                                if write_frame(&mut writer, &f).await.is_err() {
                                    return;
                                }
                            }
                        }
                        Err(broadcast::error::RecvError::Lagged(n)) => {
                            tracing::warn!("pipe: broadcast lagged {}", n);
                        }
                        Err(broadcast::error::RecvError::Closed) => return,
                    }
                }
            }
        }
    });

    loop {
        let frame = match read_frame(&mut reader).await {
            Ok(f) => f,
            Err(e) => {
                tracing::debug!("pipe: read end: {}", e);
                break;
            }
        };
        match frame {
            Frame::Event(p) => {
                let err = dispatch_event(&manager, &room_id, p, player_index);
                if err != ERR_OK
                    && ctrl_tx
                        .send(Frame::Error(ErrorPayload {
                            errnu: err,
                            message: "send_event failed".into(),
                        }))
                        .is_err()
                {
                    break;
                }
            }
            Frame::Ping => {
                if ctrl_tx.send(Frame::Pong).is_err() {
                    break;
                }
            }
            Frame::Bye(_) => break,
            Frame::Hello(_)
            | Frame::HelloAck(_)
            | Frame::Pong
            | Frame::Error(_) => {
                // ignore unsolicited
            }
        }
    }

    drop(ctrl_tx);
    let _ = writer_task.await;

    if let Some(room) = manager.read().unwrap().get_room(&room_id) {
        let _ = room.leave_player(player_index, "PipeClosed".into());
    }

    tracing::info!(
        "pipe: client disconnected room={} player={}",
        room_id,
        player_index
    );
}

fn subscribe_and_join(
    manager: &Arc<RwLock<RoomManager>>,
    room_id: &str,
    player_index: i32,
    name: &str,
) -> (Option<broadcast::Receiver<OutEvent>>, i32) {
    let mgr = manager.read().unwrap();
    match mgr.get_room(room_id) {
        Some(room) => {
            // 先 subscribe，再 join，避免漏掉 _playerjoin 触发的出站事件
            let rx = room.out_event_tx.subscribe();
            let actual_name = if name.is_empty() {
                format!("Player_{}", player_index)
            } else {
                name.to_string()
            };
            let errnu = room.join_player(player_index, actual_name, "PipeConnect".into());
            (Some(rx), errnu)
        }
        None => (None, ERR_ROOM_NOT_EXIST),
    }
}

fn dispatch_event(
    manager: &Arc<RwLock<RoomManager>>,
    room_id: &str,
    payload: EventPayload,
    player_index: i32,
) -> i32 {
    let mgr = manager.read().unwrap();
    match mgr.get_room(room_id) {
        Some(room) => room.send_event(payload.ename, payload.evalue, player_index),
        None => ERR_ROOM_NOT_EXIST,
    }
}

fn event_targets_player(ev: &OutEvent, player_index: i32) -> bool {
    ev.player_index == player_index || ev.player_index < 0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::player::Player;
    use crate::room::{Room, RoomManager, RoomSharedState, RoomStatus};
    use crate::simulation::RoomSimulationData;
    use std::collections::{HashMap, VecDeque};
    use std::path::PathBuf;
    use tokio::io::duplex;
    use tokio::io::AsyncWriteExt;

    fn empty_manager() -> Arc<RwLock<RoomManager>> {
        Arc::new(RwLock::new(RoomManager::new()))
    }

    /// 构造一个不带工作线程的 Room，仅用于测试 pipe session 与 Room 之间的契约。
    /// command_rx / log_rx 也返回，让测试能确认上行事件确实进了 command 队列。
    fn test_room_with_player(
        room_id: &str,
        player_index: i32,
    ) -> (
        Room,
        mpsc::UnboundedReceiver<crate::room::RoomCommand>,
    ) {
        let (command_tx, command_rx) = mpsc::unbounded_channel();
        let (log_tx, _log_rx) = broadcast::channel(16);
        let (out_event_tx, _out_rx) = broadcast::channel(16);

        let mut players = HashMap::new();
        players.insert(player_index, Player::new(player_index, "Tester".into()));

        let mut out_queues = HashMap::new();
        out_queues.insert(player_index, VecDeque::new());

        let shared = Arc::new(RwLock::new(RoomSharedState {
            id: room_id.to_string(),
            status: RoomStatus::Running,
            error_message: String::new(),
            script_dir: PathBuf::new(),
            mode_id: 0,
            start_ts: 0,
            loaded_ts: 0,
            simulation: RoomSimulationData::default_for_players(&players),
            players,
            out_queues,
            log_buffer: VecDeque::new(),
            event_buffer: VecDeque::new(),
            profile_data: None,
            profiler_available: false,
            profiler_running: false,
        }));

        (
            Room {
                shared,
                command_tx,
                log_tx,
                out_event_tx,
                join_handle: None,
            },
            command_rx,
        )
    }

    #[tokio::test]
    async fn hello_to_unknown_room_returns_hello_ack_err() {
        let (server_side, mut client_side) = duplex(64 * 1024);
        let manager = empty_manager();
        let handle = tokio::spawn(run_session(server_side, manager));

        let hello = Frame::Hello(HelloPayload {
            room_id: "missing".into(),
            player_index: 0,
            name: "tester".into(),
            version: "test".into(),
        });
        write_frame(&mut client_side, &hello).await.unwrap();

        let ack = read_frame(&mut client_side).await.unwrap();
        match ack {
            Frame::HelloAck(p) => {
                assert!(!p.ok);
                assert_eq!(p.errnu, ERR_ROOM_NOT_EXIST);
                assert!(p.message.contains("missing"));
            }
            other => panic!("expected HelloAck, got {:?}", other),
        }

        client_side.shutdown().await.ok();
        let _ = tokio::time::timeout(Duration::from_secs(2), handle)
            .await
            .expect("session should finish");
    }

    #[tokio::test]
    async fn non_hello_first_frame_is_rejected() {
        let (server_side, mut client_side) = duplex(64 * 1024);
        let manager = empty_manager();
        let handle = tokio::spawn(run_session(server_side, manager));

        write_frame(&mut client_side, &Frame::Ping).await.unwrap();

        let resp = read_frame(&mut client_side).await.unwrap();
        match resp {
            Frame::Error(p) => assert!(p.message.contains("Hello")),
            other => panic!("expected Error, got {:?}", other),
        }

        client_side.shutdown().await.ok();
        let _ = tokio::time::timeout(Duration::from_secs(2), handle)
            .await
            .expect("session should finish");
    }

    #[tokio::test]
    async fn s2c_broadcast_event_forwards_to_pipe() {
        let manager = empty_manager();
        let (room, _cmd_rx) = test_room_with_player("room-001", 0);
        let out_event_tx = room.out_event_tx.clone();
        manager.write().unwrap().rooms.insert("room-001".into(), room);

        let (server_side, mut client_side) = duplex(64 * 1024);
        let handle = tokio::spawn(run_session(server_side, manager));

        let hello = Frame::Hello(HelloPayload {
            room_id: "room-001".into(),
            player_index: 0,
            name: "tester".into(),
            version: "test".into(),
        });
        write_frame(&mut client_side, &hello).await.unwrap();

        match read_frame(&mut client_side).await.unwrap() {
            Frame::HelloAck(p) => assert!(p.ok, "hello ack should be ok, got {:?}", p),
            other => panic!("expected HelloAck, got {:?}", other),
        }

        let _ = out_event_tx.send(OutEvent {
            timestamp: 1.0,
            player_index: 0,
            ename: "echo_back".into(),
            evalue: "pong".into(),
            room_id: "room-001".into(),
        });

        let ev = tokio::time::timeout(Duration::from_secs(2), read_frame(&mut client_side))
            .await
            .expect("timeout reading event")
            .expect("read failed");
        match ev {
            Frame::Event(p) => {
                assert_eq!(p.dir, "s2c");
                assert_eq!(p.ename, "echo_back");
                assert_eq!(p.evalue, "pong");
                assert_eq!(p.player_index, 0);
                assert_eq!(p.room_id, "room-001");
            }
            other => panic!("expected Event, got {:?}", other),
        }

        let _ = out_event_tx.send(OutEvent {
            timestamp: 1.5,
            player_index: 1,
            ename: "for_other".into(),
            evalue: String::new(),
            room_id: "room-001".into(),
        });
        let _ = out_event_tx.send(OutEvent {
            timestamp: 1.6,
            player_index: 0,
            ename: "for_me".into(),
            evalue: "ok".into(),
            room_id: "room-001".into(),
        });

        let ev = tokio::time::timeout(Duration::from_secs(2), read_frame(&mut client_side))
            .await
            .expect("timeout")
            .unwrap();
        match ev {
            Frame::Event(p) => assert_eq!(p.ename, "for_me", "should skip player 1's event"),
            other => panic!("expected Event, got {:?}", other),
        }

        client_side.shutdown().await.ok();
        let _ = tokio::time::timeout(Duration::from_secs(2), handle).await;
    }

    #[tokio::test]
    async fn c2s_event_enqueues_dispatch_command() {
        use crate::room::RoomCommand;

        let manager = empty_manager();
        let (room, mut cmd_rx) = test_room_with_player("room-002", 0);
        manager.write().unwrap().rooms.insert("room-002".into(), room);

        let (server_side, mut client_side) = duplex(64 * 1024);
        let handle = tokio::spawn(run_session(server_side, manager));

        write_frame(
            &mut client_side,
            &Frame::Hello(HelloPayload {
                room_id: "room-002".into(),
                player_index: 0,
                name: "tester".into(),
                version: "test".into(),
            }),
        )
        .await
        .unwrap();

        match read_frame(&mut client_side).await.unwrap() {
            Frame::HelloAck(p) => assert!(p.ok),
            other => panic!("expected HelloAck, got {:?}", other),
        }

        // 第一个 command 是 join_player 触发的 PlayerJoin，先把它吞掉
        let join_cmd = tokio::time::timeout(Duration::from_secs(2), cmd_rx.recv())
            .await
            .expect("timeout join")
            .expect("channel closed");
        match join_cmd {
            RoomCommand::PlayerJoin { player_index, .. } => assert_eq!(player_index, 0),
            _ => panic!("expected PlayerJoin first, got something else"),
        }

        write_frame(
            &mut client_side,
            &Frame::Event(EventPayload {
                dir: "c2s".into(),
                ename: "buy_tower".into(),
                evalue: r#"{"id":1}"#.into(),
                ts: 0.0,
                player_index: 0,
                room_id: "room-002".into(),
            }),
        )
        .await
        .unwrap();

        let cmd = tokio::time::timeout(Duration::from_secs(2), cmd_rx.recv())
            .await
            .expect("timeout recv")
            .expect("channel closed");
        match cmd {
            RoomCommand::DispatchEvent {
                ename,
                evalue,
                player_index,
            } => {
                assert_eq!(ename, "buy_tower");
                assert_eq!(evalue, r#"{"id":1}"#);
                assert_eq!(player_index, 0);
            }
            _ => panic!("expected DispatchEvent"),
        }

        client_side.shutdown().await.ok();
        let _ = tokio::time::timeout(Duration::from_secs(2), handle).await;
    }

    #[tokio::test]
    async fn ping_gets_pong() {
        let manager = empty_manager();
        let (room, _cmd_rx) = test_room_with_player("room-003", 0);
        manager.write().unwrap().rooms.insert("room-003".into(), room);

        let (server_side, mut client_side) = duplex(64 * 1024);
        let handle = tokio::spawn(run_session(server_side, manager));

        write_frame(
            &mut client_side,
            &Frame::Hello(HelloPayload {
                room_id: "room-003".into(),
                player_index: 0,
                name: String::new(),
                version: String::new(),
            }),
        )
        .await
        .unwrap();
        let _ = read_frame(&mut client_side).await.unwrap();

        write_frame(&mut client_side, &Frame::Ping).await.unwrap();
        let resp = tokio::time::timeout(Duration::from_secs(2), read_frame(&mut client_side))
            .await
            .expect("timeout")
            .unwrap();
        assert_eq!(resp, Frame::Pong);

        client_side.shutdown().await.ok();
        let _ = tokio::time::timeout(Duration::from_secs(2), handle).await;
    }

    #[test]
    fn event_filter_matches_target_player() {
        let ev = OutEvent {
            timestamp: 0.0,
            player_index: 0,
            ename: "x".into(),
            evalue: String::new(),
            room_id: "r".into(),
        };
        assert!(event_targets_player(&ev, 0));
        assert!(!event_targets_player(&ev, 1));
        let broadcast_ev = OutEvent {
            player_index: -1,
            ..ev.clone()
        };
        assert!(event_targets_player(&broadcast_ev, 0));
        assert!(event_targets_player(&broadcast_ev, 7));
    }
}
