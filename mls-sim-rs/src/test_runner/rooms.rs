//! 一个 invocation 的房间集合：共享 GA，隔离玩家、Lua VM 和消息队列，统一回收。
use super::*;
use crate::room::ga_test_control::GaTestControl;

pub(super) struct TestRooms {
    pub suite: ResolvedSuite,
    pub config: AutoRoomConfig,
    pub archive: Arc<TempDir>,
    pub clock: TestClock,
    pub records: Mutex<Vec<(String, Arc<RoomExecutionControl>)>>,
    pub closed: Mutex<HashSet<String>>,
}

pub(super) fn install(
    lua: &Lua,
    context: &LuaTable,
    manager: Arc<Mutex<RoomManager>>,
    room_id: String,
    group: Arc<TestRooms>,
    deadline: Instant,
    event_timeout: Duration,
) -> RunnerResult<()> {
    context.set("roomId", room_id.clone()).map_err(lua_error)?;
    let spawn_manager = manager.clone();
    let spawn_group = group.clone();
    context.set("createRoom", lua.create_function(move |lua, (_self, options): (LuaTable, Option<LuaTable>)| {
        if Instant::now() >= deadline { return Err(mlua::Error::RuntimeError(TEST_TIMEOUT_MESSAGE.into())); }
        let mut config = spawn_group.config.clone();
        if let Some(options) = options {
            for pair in options.clone().pairs::<String, LuaValue>() {
                let (key, _) = pair?;
                if key != "players" { return Err(mlua::Error::RuntimeError(format!("unknown createRoom option: {key}"))); }
            }
            if let Some(players) = options.get::<Option<LuaTable>>("players")? {
                config.players = lua.from_value(LuaValue::Table(players))?;
                let mut seen = HashSet::new();
                if config.players.is_empty() || config.players.iter().any(|p| p.index < 0 || !seen.insert(p.index)) {
                    return Err(mlua::Error::RuntimeError("players must have distinct nonnegative indices and cannot be empty".into()));
                }
            }
        }
        let players = config::build_players_from_config(&config.players);
        let indices: Vec<_> = players.keys().copied().collect();
        let simulation = config::build_room_simulation_from_config(Some(&config), &players);
        let control = Arc::new(RoomExecutionControl::new());
        let startup = deadline.min(Instant::now() + seconds(spawn_group.suite.timeouts.startup));
        control.set_deadline(startup);
        let id = spawn_manager.lock().unwrap().create_room_with_options(
            spawn_group.suite.script_dir.clone(), config.mode_id, players, simulation,
            spawn_group.archive.path().to_string_lossy().into_owned(),
            RoomLaunchOptions {
                bootstrap_files: spawn_group.suite.bootstrap_files.clone(),
                execution_control: Some(control.clone()), archive_guard: Some(spawn_group.archive.clone()),
                persist_archives_to_disk: false, global_archive: None,
                ga_test_control: Some(Arc::new(Mutex::new(GaTestControl::default()))),
            },
        );
        // 创建后立即登记，初始化失败也必须随用例取消和回收。
        spawn_group.records.lock().unwrap().push((id.clone(), control.clone()));
        wait_for_room_start(&spawn_manager, &id, startup).map_err(mlua::Error::RuntimeError)?;
        {
            let manager = spawn_manager.lock().unwrap();
            let room = manager.get_room(&id).unwrap();
            if !room.barrier(startup.saturating_duration_since(Instant::now())) {
                return Err(mlua::Error::RuntimeError("child room _roomloaded timed out".into()));
            }
            for index in indices { room.drain_events(index); }
        }
        control.set_deadline(deadline);
        create_test_context(lua, spawn_manager.clone(), id, Arc::new(Mutex::new(ClientState::default())),
            deadline, event_timeout, spawn_group.clone()).map_err(|e| mlua::Error::RuntimeError(e.to_string()))
    }).map_err(lua_error)?).map_err(lua_error)?;

    let close_manager = manager.clone();
    let close_group = group.clone();
    let close_id = room_id.clone();
    context
        .set(
            "close",
            lua.create_function(move |_, _self: LuaTable| {
                if close_group.closed.lock().unwrap().contains(&close_id) {
                    return Ok(false);
                }
                let control = close_group
                    .records
                    .lock()
                    .unwrap()
                    .iter()
                    .find(|(id, _)| id == &close_id)
                    .unwrap()
                    .1
                    .clone();
                control.cancel();
                let stopped = close_manager
                    .lock()
                    .unwrap()
                    .destroy_room_and_wait(&close_id, seconds(close_group.suite.timeouts.shutdown));
                close_group.closed.lock().unwrap().insert(close_id.clone());
                if !stopped {
                    return Err(mlua::Error::RuntimeError(format!(
                        "room {close_id} shutdown timed out"
                    )));
                }
                Ok(true)
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    // 回包闸门只针对这个房间；不会暂停存储操作，也不阻塞其他房间。
    for method in [
        "holdGaReplies",
        "gaReplyState",
        "releaseGaReplies",
        "dropGaReplies",
    ] {
        let manager = manager.clone();
        let id = room_id.clone();
        context.set(method, lua.create_function(move |lua, (_self, op, count): (LuaTable, Option<String>, Option<usize>)| {
            let manager = manager.lock().unwrap();
            let room = manager.get_room(&id).ok_or_else(|| mlua::Error::RuntimeError("cloud room no longer exists".into()))?;
            let store = room.global_archive.as_ref().ok_or_else(|| mlua::Error::RuntimeError("GA is unavailable in this room".into()))?;
            let control = room.ga_test_control.as_ref().unwrap();
            if method == "holdGaReplies" {
                let op = op.ok_or_else(|| mlua::Error::RuntimeError("GA operation name is required".into()))?;
                if !["MsGASet","MsGAGet","MsGADel","MsGAIncrby","MsGAHmSet","MsGAHmGet","MsGAHGetAll","MsGAHIncrby","MsGAHDel","MsGALockAcquire","MsGALockRelease"].contains(&op.as_str()) || count == Some(0) {
                    return Err(mlua::Error::RuntimeError("invalid GA operation or count".into()));
                }
                let mut control = control.lock().unwrap();
                if control.gate.is_some() || !control.held.is_empty() { return Err(mlua::Error::RuntimeError("resolve the current GA gate before replacing it".into())); }
                control.gate = Some((op, count.unwrap_or(1)));
                return Ok(LuaValue::Nil);
            }
            if method == "gaReplyState" {
                let control = control.lock().unwrap();
                return lua.to_value(&serde_json::json!({"held":control.held.len(),"armed":control.gate.is_some(),"calls":control.calls}));
            }
            // 先取出所有结果再碰存储锁，避免与原生 API 的 store -> control 顺序倒置。
            let held = { let mut control = control.lock().unwrap(); control.gate = None; std::mem::take(&mut control.held) };
            let count = held.len();
            if method == "releaseGaReplies" {
                let store = store.lock().unwrap();
                for (op, id, outcome) in held { store.deliver_reply(&room.command_tx, &op, id, outcome); }
            }
            Ok(LuaValue::Integer(count as i64))
        }).map_err(lua_error)?).map_err(lua_error)?;
    }
    Ok(())
}
