use std::collections::{HashMap, HashSet};
use std::sync::{Arc, RwLock};
use std::time::Duration;

use dioxus::prelude::ReadableExt;
use tokio::sync::broadcast;

use crate::room::{LogEntry, OutEvent, RoomManager};

use super::state::{
    EVENTS, LOGS, MAX_EVENTS, MAX_LOGS, PROFILER_ROOM, PROFILE_SNAPSHOT, ProfileSnapshot,
};

/// 100ms 周期：拉房间 broadcast 到全局 Signal。每 200ms 拍一次 profiler 快照。
pub async fn pump(manager: Arc<RwLock<RoomManager>>) {
    let mut log_rxs: HashMap<String, broadcast::Receiver<LogEntry>> = HashMap::new();
    let mut evt_rxs: HashMap<String, broadcast::Receiver<OutEvent>> = HashMap::new();
    let mut subscribed: HashSet<String> = HashSet::new();
    let mut tick: u32 = 0;
    let mut interval = tokio::time::interval(Duration::from_millis(100));

    loop {
        interval.tick().await;
        sync_subscriptions(&manager, &mut subscribed, &mut log_rxs, &mut evt_rxs);

        let (log_batch, evt_batch) = drain(&mut log_rxs, &mut evt_rxs);
        if !log_batch.is_empty() {
            let mut w = LOGS.write();
            w.extend(log_batch);
            while w.len() > MAX_LOGS {
                w.pop_front();
            }
        }
        if !evt_batch.is_empty() {
            let mut w = EVENTS.write();
            w.extend(evt_batch);
            while w.len() > MAX_EVENTS {
                w.pop_front();
            }
        }

        tick = tick.wrapping_add(1);
        if tick % 2 == 0 {
            snapshot_profile(&manager);
        }
    }
}

fn sync_subscriptions(
    manager: &Arc<RwLock<RoomManager>>,
    subscribed: &mut HashSet<String>,
    log_rxs: &mut HashMap<String, broadcast::Receiver<LogEntry>>,
    evt_rxs: &mut HashMap<String, broadcast::Receiver<OutEvent>>,
) {
    let mgr = match manager.try_read() {
        Ok(m) => m,
        Err(_) => return,
    };
    let current: HashSet<String> = mgr.rooms.keys().cloned().collect();

    // 新增
    for id in &current {
        if !subscribed.contains(id) {
            if let Some(room) = mgr.rooms.get(id) {
                if let Ok(shared) = room.shared.try_read() {
                    LOGS.write().extend(shared.log_buffer.iter().cloned());
                    EVENTS.write().extend(shared.event_buffer.iter().cloned());
                }
                log_rxs.insert(id.clone(), room.log_tx.subscribe());
                evt_rxs.insert(id.clone(), room.out_event_tx.subscribe());
            }
        }
    }

    // 移除
    let removed: Vec<String> = subscribed.difference(&current).cloned().collect();
    for id in &removed {
        log_rxs.remove(id);
        evt_rxs.remove(id);
    }

    *subscribed = current;
}

fn drain(
    log_rxs: &mut HashMap<String, broadcast::Receiver<LogEntry>>,
    evt_rxs: &mut HashMap<String, broadcast::Receiver<OutEvent>>,
) -> (Vec<LogEntry>, Vec<OutEvent>) {
    let mut logs = Vec::new();
    let mut evts = Vec::new();
    for rx in log_rxs.values_mut() {
        loop {
            match rx.try_recv() {
                Ok(e) => logs.push(e),
                Err(broadcast::error::TryRecvError::Lagged(_)) => continue,
                _ => break,
            }
        }
    }
    for rx in evt_rxs.values_mut() {
        loop {
            match rx.try_recv() {
                Ok(e) => evts.push(e),
                Err(broadcast::error::TryRecvError::Lagged(_)) => continue,
                _ => break,
            }
        }
    }
    (logs, evts)
}

fn snapshot_profile(manager: &Arc<RwLock<RoomManager>>) {
    let rid = match PROFILER_ROOM.peek().clone() {
        Some(r) => r,
        None => {
            if PROFILE_SNAPSHOT.peek().is_some() {
                *PROFILE_SNAPSHOT.write() = None;
            }
            return;
        }
    };
    let mgr = match manager.try_read() {
        Ok(m) => m,
        Err(_) => return,
    };
    let room = match mgr.rooms.get(&rid) {
        Some(r) => r,
        None => {
            *PROFILE_SNAPSHOT.write() = None;
            return;
        }
    };
    let shared = match room.shared.try_read() {
        Ok(s) => s,
        Err(_) => return,
    };
    *PROFILE_SNAPSHOT.write() = Some(ProfileSnapshot {
        room_id: rid,
        available: shared.profiler_available,
        running: shared.profiler_running,
        hook_count: super::state::PROFILER_HOOK_COUNT(),
        window_sec: super::state::PROFILER_WINDOW(),
        frame_ms: super::state::PROFILER_FRAME_MS(),
        data: shared.profile_data.clone(),
    });
}
