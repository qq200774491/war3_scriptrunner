use super::global_archive::{GaArgs, SharedGlobalArchive};
use super::{
    now_secs, validate_user_event, CommandSender, EventHandler, RoomCommand, RoomSharedState,
    ServerResponseDelivery, TimerThreadControl, ERR_ITEM_NOT_ENOUGH, ERR_ITEM_NOT_FOUND, ERR_OK,
    ERR_PLAYER_NOT_EXIST, ERR_SCRIPT_ARCHIVE_TOO_LONG, ERR_UNKNOWN, MAX_LOG_LEN,
    MAX_SCRIPT_ARCHIVE_LEN,
};
use mlua::prelude::*;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::{Arc, Mutex, RwLock};

pub(super) struct InstallLuaApiContext<LogFn, OutFn>
where
    LogFn: Fn(&str, &str, String) + Clone + Send + 'static,
    OutFn: Fn(i32, String, String) + Clone + Send + 'static,
{
    pub shared: Arc<RwLock<RoomSharedState>>,
    pub command_tx: CommandSender,
    pub running: Arc<AtomicBool>,
    pub timer_control: Arc<TimerThreadControl>,
    pub event_handlers: Arc<Mutex<HashMap<String, Vec<EventHandler>>>>,
    pub ticker_callbacks: Arc<Mutex<HashMap<i32, mlua::RegistryKey>>>,
    pub next_event_id: Arc<AtomicI32>,
    pub next_ticker_id: Arc<AtomicI32>,
    pub trans_id_counter: Arc<AtomicI32>,
    pub emit_log: LogFn,
    pub emit_out_event: OutFn,
    pub server_response_delivery: Arc<Mutex<ServerResponseDelivery>>,
    pub archive_dir: String,
    pub persist_archives_to_disk: bool,
    pub script_dir: PathBuf,
    /// Some 时安装 MsGA* 与 MsBroadcast；None 表示模拟没有全局存档能力的旧平台。
    pub global_archive: Option<SharedGlobalArchive>,
}

pub(super) fn install_lua_apis<LogFn, OutFn>(
    lua: &Lua,
    ctx: InstallLuaApiContext<LogFn, OutFn>,
) -> LuaResult<()>
where
    LogFn: Fn(&str, &str, String) + Clone + Send + 'static,
    OutFn: Fn(i32, String, String) + Clone + Send + 'static,
{
    install_log_api(lua, ctx.emit_log.clone())?;
    install_print(lua, ctx.emit_log.clone())?;
    install_timer_api(
        lua,
        ctx.command_tx.clone(),
        ctx.running.clone(),
        ctx.timer_control.clone(),
        ctx.ticker_callbacks.clone(),
        ctx.next_ticker_id.clone(),
    )?;
    install_event_api(lua, ctx.event_handlers.clone(), ctx.next_event_id.clone())?;
    install_player_api(lua, ctx.shared.clone())?;
    install_room_api(lua, ctx.shared.clone())?;
    install_item_api(
        lua,
        ctx.shared.clone(),
        ctx.emit_out_event.clone(),
        ctx.trans_id_counter.clone(),
    )?;
    install_archive_api(
        lua,
        ctx.shared.clone(),
        ctx.emit_out_event.clone(),
        ctx.archive_dir.clone(),
        ctx.persist_archives_to_disk,
    )?;
    install_control_api(
        lua,
        ctx.shared.clone(),
        ctx.command_tx.clone(),
        ctx.emit_log.clone(),
        ctx.emit_out_event.clone(),
        ctx.server_response_delivery,
    )?;
    install_md5_api(lua)?;
    if let Some(store) = ctx.global_archive {
        install_global_archive_api(
            lua,
            store,
            ctx.command_tx.clone(),
            ctx.trans_id_counter,
        )?;
    }
    install_require(lua, ctx.script_dir)?;
    Ok(())
}

// 全局存档：结果经房间命令队列异步投递，由平台封装层分派给可选的末参 cb。
// 不在原生调用栈内同步通知业务，保持平台的异步契约。
fn install_global_archive_api(
    lua: &Lua,
    store: SharedGlobalArchive,
    command_tx: CommandSender,
    trans_id_counter: Arc<AtomicI32>,
) -> LuaResult<()> {
    fn lua_table_to_string_pairs(table: &LuaTable) -> LuaResult<Vec<(String, String)>> {
        let mut pairs = Vec::new();
        for pair in table.pairs::<LuaValue, LuaValue>() {
            let (field, value) = pair?;
            pairs.push((lua_value_to_string(&field), lua_value_to_string(&value)));
        }
        Ok(pairs)
    }

    fn lua_table_to_string_list(table: &LuaTable) -> LuaResult<Vec<String>> {
        let mut list = Vec::new();
        for value in table.sequence_values::<LuaValue>() {
            list.push(lua_value_to_string(&value?));
        }
        Ok(list)
    }

    // 模拟平台封装层：按事务号转交已解码对象，调用前移除回调，避免重复通知。
    let callbacks = lua.create_table()?;
    let reply_callbacks = callbacks.clone();
    let handler = lua.create_function(
        move |lua, (_event_id, _event_name, payload, _player): (i32, String, String, i32)| {
            let json: LuaTable = lua.globals().get("json")?;
            let decode: LuaFunction = json.get("decode")?;
            let message: LuaTable = decode.call(payload)?;
            let trans_id: i32 = message.get("trans_id")?;
            let callback: Option<LuaFunction> = reply_callbacks.raw_get(trans_id)?;
            if let Some(callback) = callback {
                reply_callbacks.raw_set(trans_id, LuaValue::Nil)?;
                callback.call::<()>(message)?;
            }
            Ok(())
        },
    )?;
    lua.globals()
        .get::<LuaFunction>("RegisterEvent")?
        .call::<i32>(("_msga_reply", handler))?;

    macro_rules! ga_api {
        ($name:expr, |$($arg:ident : $ty:ty),*| $build:expr) => {{
            let store = store.clone();
            let command_tx = command_tx.clone();
            let trans_id_counter = trans_id_counter.clone();
            let callbacks = callbacks.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, ($($arg,)* callback): ($($ty,)* Option<LuaFunction>)| {
                    let trans_id = trans_id_counter.fetch_add(1, Ordering::Relaxed) + 1;
                    let args = $build;
                    let has_callback = callback.is_some();
                    if let Some(callback) = callback {
                        callbacks.raw_set(trans_id, callback)?;
                    }
                    let mut store = store.lock().unwrap();
                    let outcome = store.execute($name, args);
                    store.deliver_reply(&command_tx, $name, trans_id, outcome);
                    // cb 模式的返回值未被文档保证，故返回 nil，防止业务误依赖。
                    Ok(if has_callback { None } else { Some(trans_id) })
                })?,
            )?;
        }};
    }

    ga_api!("MsGASet", |key: String, value: LuaValue| GaArgs::Set {
        key,
        value: lua_value_to_string(&value),
    });
    ga_api!("MsGAGet", |key: String| GaArgs::Get { key });
    ga_api!("MsGADel", |key: String| GaArgs::Del { key });
    ga_api!("MsGAIncrby", |key: String, delta: i64| GaArgs::Incrby {
        key,
        delta
    });
    ga_api!("MsGAHmSet", |key: String, kvs: LuaTable| GaArgs::HmSet {
        key,
        kvs: lua_table_to_string_pairs(&kvs)?,
    });
    ga_api!("MsGAHmGet", |key: String, fields: LuaTable| GaArgs::HmGet {
        key,
        fields: lua_table_to_string_list(&fields)?,
    });
    ga_api!("MsGAHGetAll", |key: String| GaArgs::HGetAll { key });
    ga_api!("MsGAHIncrby", |key: String, field: String, delta: i64| {
        GaArgs::HIncrby { key, field, delta }
    });
    ga_api!("MsGAHDel", |key: String, fields: LuaTable| GaArgs::HDel {
        key,
        fields: lua_table_to_string_list(&fields)?,
    });
    ga_api!("MsGALockAcquire", |key: String, ttl_ms: i64| {
        GaArgs::LockAcquire { key, ttl_ms }
    });
    ga_api!("MsGALockRelease", |key: String| GaArgs::LockRelease { key });

    let broadcast_store = store.clone();
    lua.globals().set(
        "MsBroadcast",
        lua.create_function(move |_, (key, value): (String, LuaValue)| {
            let value = lua_value_to_string(&value);
            broadcast_store.lock().unwrap().broadcast(&key, &value);
            Ok(())
        })?,
    )?;
    Ok(())
}

fn install_log_api<LogFn>(lua: &Lua, emit_log: LogFn) -> LuaResult<()>
where
    LogFn: Fn(&str, &str, String) + Clone + Send + 'static,
{
    let log_table = lua.create_table()?;
    for (method, level) in [("Debug", "DBG"), ("Info", "INF"), ("Error", "ERR")] {
        let emit = emit_log.clone();
        let level = level.to_string();
        let func = lua.create_function(move |lua_ctx: &Lua, args: mlua::MultiValue| {
            let msg = if args.is_empty() {
                String::new()
            } else if args.len() == 1 {
                lua_value_to_string(&args[0])
            } else {
                let string_table: mlua::Table = lua_ctx.globals().get("string")?;
                let sf: LuaFunction = string_table.get("format")?;
                match sf.call::<String>(args.clone()) {
                    Ok(s) => s,
                    Err(_) => lua_value_to_string(&args[0]),
                }
            };
            emit(&level, "Lua", truncate_log(msg));
            Ok(())
        })?;
        log_table.set(method, func)?;
    }
    lua.globals().set("Log", log_table)?;
    Ok(())
}

fn install_print<LogFn>(lua: &Lua, emit_log: LogFn) -> LuaResult<()>
where
    LogFn: Fn(&str, &str, String) + Clone + Send + 'static,
{
    let print_fn = lua.create_function(move |_lua_ctx: &Lua, args: mlua::MultiValue| {
        let parts: Vec<String> = args.iter().map(lua_value_to_string).collect();
        emit_log("INF", "Lua", truncate_log(parts.join("\t")));
        Ok(())
    })?;
    lua.globals().set("print", print_fn)?;
    Ok(())
}

fn install_timer_api(
    lua: &Lua,
    command_tx: CommandSender,
    running: Arc<AtomicBool>,
    timer_control: Arc<TimerThreadControl>,
    ticker_callbacks: Arc<Mutex<HashMap<i32, mlua::RegistryKey>>>,
    next_ticker_id: Arc<AtomicI32>,
) -> LuaResult<()> {
    let timer_table = lua.create_table()?;

    let timer_after = lua.create_function({
        let cmd_tx = command_tx.clone();
        let running = running.clone();
        let timer_control = timer_control.clone();
        move |lua_ctx: &Lua, (seconds, callback): (f64, LuaFunction)| {
            let key = lua_ctx.create_registry_value(callback)?;
            let cmd_tx = cmd_tx.clone();
            let running = running.clone();
            let timer_control = timer_control.clone();
            std::thread::spawn(move || {
                let elapsed = timer_control
                    .wait_timeout(std::time::Duration::from_secs_f64(seconds.max(0.0)), None);
                if elapsed && running.load(Ordering::Acquire) {
                    let _ = cmd_tx.send(RoomCommand::TimerCallback { func_key: key });
                }
            });
            Ok(())
        }
    })?;
    timer_table.set("After", timer_after)?;

    let timer_ticker = lua.create_function({
        let cmd_tx = command_tx.clone();
        let running = running.clone();
        let ticker_cbs = ticker_callbacks.clone();
        let next_tid = next_ticker_id.clone();
        let timer_control = timer_control.clone();
        move |lua_ctx: &Lua, (seconds, callback): (f64, LuaFunction)| {
            let ticker_id = next_tid.fetch_add(1, Ordering::Relaxed);
            let key = lua_ctx.create_registry_value(callback)?;
            ticker_cbs.lock().unwrap().insert(ticker_id, key);

            let cancelled = Arc::new(AtomicBool::new(false));
            let cancelled_clone = cancelled.clone();
            let cmd_tx = cmd_tx.clone();
            let running = running.clone();
            let timer_control_for_thread = timer_control.clone();

            std::thread::spawn(move || loop {
                let elapsed = timer_control_for_thread.wait_timeout(
                    std::time::Duration::from_secs_f64(seconds.max(0.001)),
                    Some(&cancelled_clone),
                );
                if !elapsed || !running.load(Ordering::Acquire) {
                    break;
                }
                let _ = cmd_tx.send(RoomCommand::TickerFire { ticker_id });
            });

            let ticker = lua_ctx.create_table()?;
            ticker.set(
                "Cancel",
                lua_ctx.create_function({
                    let cancel_flag = cancelled.clone();
                    let timer_control = timer_control.clone();
                    move |_, ()| {
                        cancel_flag.store(true, Ordering::Release);
                        timer_control.notify_waiters();
                        Ok(())
                    }
                })?,
            )?;
            Ok(ticker)
        }
    })?;
    timer_table.set("NewTicker", timer_ticker)?;
    lua.globals().set("Timer", timer_table)?;
    Ok(())
}

fn install_event_api(
    lua: &Lua,
    event_handlers: Arc<Mutex<HashMap<String, Vec<EventHandler>>>>,
    next_event_id: Arc<AtomicI32>,
) -> LuaResult<()> {
    let handlers = event_handlers.clone();
    let register = lua.create_function(
        move |lua_ctx: &Lua, (ename, callback): (String, LuaFunction)| {
            let id = next_event_id.fetch_add(1, Ordering::Relaxed);
            let key = lua_ctx.create_registry_value(callback)?;
            let mut h = handlers.lock().unwrap();
            h.entry(ename)
                .or_insert_with(Vec::new)
                .push(EventHandler { id, func_key: key });
            Ok(id)
        },
    )?;
    lua.globals().set("RegisterEvent", register)?;

    let unregister = lua.create_function(move |_lua_ctx: &Lua, eid: i32| {
        let mut h = event_handlers.lock().unwrap();
        for handlers_list in h.values_mut() {
            handlers_list.retain(|eh| eh.id != eid);
        }
        Ok(())
    })?;
    lua.globals().set("UnregisterEvent", unregister)?;
    Ok(())
}

fn install_player_api(lua: &Lua, shared: Arc<RwLock<RoomSharedState>>) -> LuaResult<()> {
    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerName",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .map(|p| p.name.clone())
                .unwrap_or_default())
        })?,
    )?;

    macro_rules! player_int_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, idx: i32| {
                    let shared = s.read().unwrap();
                    Ok(shared.players.get(&idx).map(|p| p.$field).unwrap_or(0))
                })?,
            )?;
        }};
    }

    macro_rules! platform_int_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, idx: i32| {
                    let shared = s.read().unwrap();
                    Ok(shared
                        .players
                        .get(&idx)
                        .map(|p| p.platform.$field)
                        .unwrap_or(0))
                })?,
            )?;
        }};
    }

    macro_rules! activity_int_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, idx: i32| {
                    let shared = s.read().unwrap();
                    Ok(shared
                        .players
                        .get(&idx)
                        .map(|p| p.activity.$field)
                        .unwrap_or(0))
                })?,
            )?;
        }};
    }

    macro_rules! sign_in_int_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, idx: i32| {
                    let shared = s.read().unwrap();
                    Ok(shared
                        .players
                        .get(&idx)
                        .map(|p| p.sign_in.$field)
                        .unwrap_or(0))
                })?,
            )?;
        }};
    }

    macro_rules! community_int_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, idx: i32| {
                    let shared = s.read().unwrap();
                    Ok(shared
                        .players
                        .get(&idx)
                        .map(|p| p.community.$field)
                        .unwrap_or(0))
                })?,
            )?;
        }};
    }

    player_int_api!("MsGetPlayerMapLevel", map_level);
    player_int_api!("MsGetPlayerMapExp", map_exp);
    player_int_api!("MsGetTestPlayTime", test_play_time);
    player_int_api!("MsGetPlayedCount", played_count);

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayedTime",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .map(|p| p.played_time())
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerGuid",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .map(|p| p.platform.guid)
                .unwrap_or(0))
        })?,
    )?;
    platform_int_api!("MsGetPlayerPlatLevel", plat_level);
    platform_int_api!("MsGetPlayerVipLevel", vip_level);
    platform_int_api!("MsGetPlayerMapVipLevel", map_vip_level);
    platform_int_api!("MsGetPlayerIsAuthor", is_author);
    platform_int_api!("MsGetPlayerIsCollected", is_collected);
    platform_int_api!("MsGetPlayerIsBackflow", is_backflow);

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlatVipType",
        lua.create_function(move |_, (idx, vip_type): (i32, i32)| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .and_then(|p| p.platform.vip_types.get(&vip_type))
                .copied()
                .unwrap_or(0))
        })?,
    )?;

    activity_int_api!("MsGetPlayerDayRounds", day_rounds);
    activity_int_api!("MsGetPlayerSinceLastGame", since_last_game);

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerLotteryCount",
        lua.create_function(move |_, (idx, cfg_index): (i32, i32)| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .map(|p| p.activity.lottery_count(cfg_index))
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerAchievePoint",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .map(|p| p.achievements.point)
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerAchieveDone",
        lua.create_function(move |_, (idx, ach_id): (i32, String)| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .and_then(|p| p.achievements.done.get(&ach_id))
                .copied()
                .unwrap_or(0))
        })?,
    )?;

    macro_rules! task_int_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, (idx, task_id): (i32, i32)| {
                    let shared = s.read().unwrap();
                    Ok(shared
                        .players
                        .get(&idx)
                        .map(|p| p.tasks.get(task_id).$field)
                        .unwrap_or(0))
                })?,
            )?;
        }};
    }

    task_int_api!("MsGetPlayerTaskTotalProgress", total);
    task_int_api!("MsGetPlayerTaskCurProgress", current);
    task_int_api!("MsGetPlayerTaskDone", done);

    sign_in_int_api!("MsGetPlayerSignInTotal", total);
    sign_in_int_api!("MsGetPlayerSignInContMax", cont_max);
    sign_in_int_api!("MsGetPlayerSignInContCur", cont_cur);

    community_int_api!("MsGetPlayerHasTopic", has_topic);
    community_int_api!("MsGetPlayerIsManager", is_manager);
    community_int_api!("MsGetPlayerTopicCount", topic_count);
    community_int_api!("MsGetPlayerCommentCount", comment_count);
    community_int_api!("MsGetPlayerHappyCount", happy_count);
    community_int_api!("MsGetPlayerBestCount", best_count);
    community_int_api!("MsGetPlayerAppraiseCount", appraise_count);
    community_int_api!("MsGetPlayerIsPinned", is_pinned);

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerPetAdvTime",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .map(|p| p.community.pet_adv_time)
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerGuildLevel",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared.players.get(&idx).map(|p| p.guild.level).unwrap_or(0))
        })?,
    )?;

    Ok(())
}

fn install_room_api(lua: &Lua, shared: Arc<RwLock<RoomSharedState>>) -> LuaResult<()> {
    let s = shared.clone();
    lua.globals().set(
        "MsGetRoomStartTs",
        lua.create_function(move |_, ()| Ok(s.read().unwrap().start_ts))?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetRoomLoadedTs",
        lua.create_function(move |_, ()| Ok(s.read().unwrap().loaded_ts))?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetRoomGameTime",
        lua.create_function(move |_, ()| {
            let shared = s.read().unwrap();
            if shared.loaded_ts == 0 {
                Ok(0i64)
            } else {
                Ok(now_secs() - shared.loaded_ts)
            }
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetRoomPlayerCount",
        lua.create_function(move |_, ()| {
            let shared = s.read().unwrap();
            Ok(shared.players.values().filter(|p| p.is_connected).count() as i32)
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetRoomModeId",
        lua.create_function(move |_, ()| Ok(s.read().unwrap().mode_id))?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetMapVersion",
        lua.create_function(move |_, ()| Ok(s.read().unwrap().simulation.map_version.clone()))?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetEnvType",
        lua.create_function(move |_, ()| Ok(s.read().unwrap().simulation.env_type))?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetPrebookCount",
        lua.create_function(move |_, ()| Ok(s.read().unwrap().simulation.prebook_count))?,
    )?;

    install_ranking_api(lua, shared)?;
    Ok(())
}

fn install_ranking_api(lua: &Lua, shared: Arc<RwLock<RoomSharedState>>) -> LuaResult<()> {
    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerRanking",
        lua.create_function(move |_, (idx, ranking_num): (i32, i32)| {
            let shared = s.read().unwrap();
            if !shared.simulation.rankings_loaded {
                return Ok(-1);
            }
            if !shared.players.contains_key(&idx) {
                return Ok(0);
            }
            Ok(shared
                .simulation
                .rankings
                .get(&ranking_num)
                .and_then(|entries| {
                    entries
                        .iter()
                        .position(|entry| entry.player_index == idx)
                        .map(|pos| pos as i32 + 1)
                })
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerRankValue",
        lua.create_function(move |_, (idx, ranking_num): (i32, i32)| {
            let shared = s.read().unwrap();
            if !shared.simulation.rankings_loaded {
                return Ok(-1);
            }
            if !shared.players.contains_key(&idx) {
                return Ok(0);
            }
            Ok(shared
                .simulation
                .rankings
                .get(&ranking_num)
                .and_then(|entries| {
                    entries
                        .iter()
                        .find(|entry| entry.player_index == idx)
                        .map(|entry| entry.value)
                })
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetRankPlayerName",
        lua.create_function(move |_, (ranking_num, rank): (i32, i32)| {
            let shared = s.read().unwrap();
            if !(1..=100).contains(&rank) {
                return Ok(String::new());
            }
            Ok(shared
                .simulation
                .rankings
                .get(&ranking_num)
                .and_then(|entries| entries.get(rank as usize - 1))
                .map(|entry| {
                    if entry.player_name.is_empty() {
                        shared
                            .players
                            .get(&entry.player_index)
                            .map(|p| p.name.clone())
                            .unwrap_or_default()
                    } else {
                        entry.player_name.clone()
                    }
                })
                .unwrap_or_default())
        })?,
    )?;

    let s = shared.clone();
    lua.globals().set(
        "MsGetRankValue",
        lua.create_function(move |_, (ranking_num, rank): (i32, i32)| {
            let shared = s.read().unwrap();
            if !(1..=100).contains(&rank) {
                return Ok(0);
            }
            Ok(shared
                .simulation
                .rankings
                .get(&ranking_num)
                .and_then(|entries| entries.get(rank as usize - 1))
                .map(|entry| entry.value)
                .unwrap_or(0))
        })?,
    )?;

    Ok(())
}

fn install_item_api<OutFn>(
    lua: &Lua,
    shared: Arc<RwLock<RoomSharedState>>,
    emit_out_event: OutFn,
    trans_id_counter: Arc<AtomicI32>,
) -> LuaResult<()>
where
    OutFn: Fn(i32, String, String) + Clone + Send + 'static,
{
    let s = shared.clone();
    lua.globals().set(
        "MsGetPlayerItem",
        lua.create_function(move |_, (idx, key): (i32, String)| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .and_then(|p| p.items.get(&key))
                .copied()
                .unwrap_or(0))
        })?,
    )?;

    let s = shared.clone();
    let emit_out = emit_out_event.clone();
    lua.globals().set(
        "MsConsumeItem",
        lua.create_function(move |_lua_ctx: &Lua, (idx, iteminfo_json): (i32, String)| {
            let trans_id = trans_id_counter.fetch_add(1, Ordering::Relaxed) + 1;
            let mut shared = s.write().unwrap();
            if !shared.players.contains_key(&idx) {
                let result = serde_json::json!({
                    "trans_id": trans_id,
                    "errnu": ERR_PLAYER_NOT_EXIST,
                    "iteminfo": {},
                });
                drop(shared);
                emit_out(idx, "_citemret".to_string(), result.to_string());
                return Ok(trans_id);
            }

            let items_to_consume: HashMap<String, i32> = match serde_json::from_str(&iteminfo_json)
            {
                Ok(v) => v,
                Err(_) => {
                    let result = serde_json::json!({
                        "trans_id": trans_id,
                        "errnu": ERR_UNKNOWN,
                        "iteminfo": {},
                    });
                    drop(shared);
                    emit_out(idx, "_citemret".to_string(), result.to_string());
                    return Ok(trans_id);
                }
            };

            let player = shared.players.get(&idx).unwrap();
            for (key, count) in &items_to_consume {
                let Some(current) = player.items.get(key).copied() else {
                    let result = serde_json::json!({
                        "trans_id": trans_id,
                        "errnu": ERR_ITEM_NOT_FOUND,
                        "iteminfo": items_to_consume,
                    });
                    drop(shared);
                    emit_out(idx, "_citemret".to_string(), result.to_string());
                    return Ok(trans_id);
                };
                if current < *count {
                    let result = serde_json::json!({
                        "trans_id": trans_id,
                        "errnu": ERR_ITEM_NOT_ENOUGH,
                        "iteminfo": items_to_consume,
                    });
                    drop(shared);
                    emit_out(idx, "_citemret".to_string(), result.to_string());
                    return Ok(trans_id);
                }
            }

            let player = shared.players.get_mut(&idx).unwrap();
            for (key, count) in &items_to_consume {
                let cur = player.items.get(key).copied().unwrap_or(0);
                player.items.insert(key.clone(), cur - count);
            }

            let result = serde_json::json!({
                "trans_id": trans_id,
                "errnu": ERR_OK,
                "iteminfo": items_to_consume,
            });
            drop(shared);
            emit_out(idx, "_citemret".to_string(), result.to_string());
            Ok(trans_id)
        })?,
    )?;

    Ok(())
}

// persist_archives_to_disk 为 false 时（测试房间）三个写入口只更新内存中的玩家存档；
// MsGet*Archive 始终从内存读取，磁盘文件只为跨房间重启的 GUI/桥接场景服务。
fn install_archive_api<OutFn>(
    lua: &Lua,
    shared: Arc<RwLock<RoomSharedState>>,
    emit_out_event: OutFn,
    archive_dir: String,
    persist_archives_to_disk: bool,
) -> LuaResult<()>
where
    OutFn: Fn(i32, String, String) + Clone + Send + 'static,
{
    let s = shared.clone();
    lua.globals().set(
        "MsGetScriptArchive",
        lua.create_function(move |_, idx: i32| {
            let shared = s.read().unwrap();
            Ok(shared
                .players
                .get(&idx)
                .and_then(|p| p.script_archive.clone())
                .unwrap_or_default())
        })?,
    )?;

    let s = shared.clone();
    let ad = archive_dir.clone();
    lua.globals().set(
        "MsSaveScriptArchive",
        lua.create_function(move |_, (idx, data): (i32, mlua::MultiValue)| {
            let data_str = match data.iter().next() {
                Some(v) => lua_value_to_string(v),
                None => String::new(),
            };
            let result = {
                let mut shared = s.write().unwrap();
                if let Some(p) = shared.players.get_mut(&idx) {
                    if data_str.as_bytes().len() > MAX_SCRIPT_ARCHIVE_LEN {
                        ERR_SCRIPT_ARCHIVE_TOO_LONG
                    } else {
                        p.script_archive = Some(data_str);
                        ERR_OK
                    }
                } else {
                    ERR_PLAYER_NOT_EXIST
                }
            };
            if result == ERR_OK && persist_archives_to_disk {
                let shared = s.read().unwrap();
                let _ = crate::storage::save_room_archives(
                    &ad,
                    &shared.script_dir.to_string_lossy(),
                    &shared.players,
                );
            }
            Ok(result)
        })?,
    )?;

    macro_rules! archive_get_api {
        ($name:expr, $field:ident) => {{
            let s = shared.clone();
            lua.globals().set(
                $name,
                lua.create_function(move |_, (idx, key): (i32, String)| {
                    let shared = s.read().unwrap();
                    Ok(shared
                        .players
                        .get(&idx)
                        .and_then(|p| p.$field.get(&key).cloned())
                        .unwrap_or_default())
                })?,
            )?;
        }};
    }

    archive_get_api!("MsGetCommonArchive", common_archive);
    archive_get_api!("MsGetReadArchive", read_archive);
    archive_get_api!("MsGetCfgArchive", cfg_archive);

    let s = shared.clone();
    let ad_common = archive_dir.clone();
    lua.globals().set(
        "MsSetCommonArchive",
        lua.create_function(move |_, (idx, key, value): (i32, String, String)| {
            let result = {
                let mut shared = s.write().unwrap();
                if let Some(p) = shared.players.get_mut(&idx) {
                    p.common_archive.insert(key, value);
                    ERR_OK
                } else {
                    ERR_PLAYER_NOT_EXIST
                }
            };
            if result == ERR_OK && persist_archives_to_disk {
                let shared = s.read().unwrap();
                let _ = crate::storage::save_room_archives(
                    &ad_common,
                    &shared.script_dir.to_string_lossy(),
                    &shared.players,
                );
            }
            Ok(result)
        })?,
    )?;

    let s = shared.clone();
    let ad_read = archive_dir.clone();
    let emit_out = emit_out_event.clone();
    lua.globals().set(
        "MsSetReadArchive",
        lua.create_function(move |_, (idx, key, value): (i32, String, String)| {
            let result = {
                let mut shared = s.write().unwrap();
                if let Some(p) = shared.players.get_mut(&idx) {
                    p.read_archive.insert(key.clone(), value.clone());
                    ERR_OK
                } else {
                    ERR_PLAYER_NOT_EXIST
                }
            };
            if result == ERR_OK {
                let rdata = format!("{}\t{}", key, value);
                emit_out(idx, "_rdata".to_string(), rdata);
                if persist_archives_to_disk {
                    let shared = s.read().unwrap();
                    let _ = crate::storage::save_room_archives(
                        &ad_read,
                        &shared.script_dir.to_string_lossy(),
                        &shared.players,
                    );
                }
            }
            Ok(result)
        })?,
    )?;

    Ok(())
}

fn install_control_api<LogFn, OutFn>(
    lua: &Lua,
    shared: Arc<RwLock<RoomSharedState>>,
    command_tx: CommandSender,
    emit_log: LogFn,
    emit_out_event: OutFn,
    server_response_delivery: Arc<Mutex<ServerResponseDelivery>>,
) -> LuaResult<()>
where
    LogFn: Fn(&str, &str, String) + Clone + Send + 'static,
    OutFn: Fn(i32, String, String) + Clone + Send + 'static,
{
    let s = shared.clone();
    lua.globals().set(
        "MsSendMlEvent",
        lua.create_function(move |_, (idx, ename, evalue): (i32, String, LuaValue)| {
            let evalue = lua_value_to_string(&evalue);
            let err = validate_user_event(&ename, &evalue);
            if err != ERR_OK {
                return Ok(err);
            }
            if idx >= 0 && !s.read().unwrap().players.contains_key(&idx) {
                return Ok(ERR_PLAYER_NOT_EXIST);
            }

            let immediate = server_response_delivery
                .lock()
                .unwrap()
                .schedule(idx, ename, evalue);
            if let Some((idx, ename, evalue)) = immediate {
                emit_out_event(idx, ename, evalue);
            }
            Ok(ERR_OK)
        })?,
    )?;

    lua.globals().set(
        "MsEnd",
        lua.create_function(move |_, (idx, reason): (i32, String)| {
            emit_log(
                "INF",
                "System",
                format!("MsEnd called: player={} reason={}", idx, reason),
            );
            let _ = command_tx.send(RoomCommand::Stop { reason });
            Ok(ERR_OK)
        })?,
    )?;
    Ok(())
}

fn install_md5_api(lua: &Lua) -> LuaResult<()> {
    lua.globals().set(
        "md5",
        lua.create_function(move |_, value: LuaValue| {
            let digest = match &value {
                LuaValue::String(s) => {
                    let bytes: &[u8] = &s.as_bytes();
                    md5::compute(bytes)
                }
                _ => md5::compute(lua_value_to_string(&value).as_bytes()),
            };
            Ok(format!("{:x}", digest))
        })?,
    )?;
    Ok(())
}

fn install_require(lua: &Lua, script_dir: PathBuf) -> LuaResult<()> {
    let script_dir_clone = script_dir.clone();
    let dir_stack: Arc<Mutex<Vec<PathBuf>>> = Arc::new(Mutex::new(vec![script_dir.clone()]));
    let loaded_modules: Arc<Mutex<HashMap<String, mlua::RegistryKey>>> =
        Arc::new(Mutex::new(HashMap::new()));

    let require_fn = lua.create_function(move |lua_ctx: &Lua, modname: String| {
        {
            let modules = loaded_modules.lock().unwrap();
            if let Some(key) = modules.get(&modname) {
                let val: LuaValue = lua_ctx.registry_value(key)?;
                return Ok(val);
            }
        }

        {
            let globals = lua_ctx.globals();
            if let Ok(val) = globals.get::<LuaValue>(&*modname) {
                if matches!(val, LuaValue::Table(_) | LuaValue::Function(_)) {
                    let key = lua_ctx.create_registry_value(val.clone())?;
                    loaded_modules.lock().unwrap().insert(modname, key);
                    return Ok(val);
                }
            }
        }

        let rel_path = if modname.contains('/') || modname.contains('\\') {
            modname.clone()
        } else {
            modname.replace('.', "/")
        };

        let search_dirs = {
            let stack = dir_stack.lock().unwrap();
            let mut dirs = Vec::new();
            if let Some(top) = stack.last() {
                dirs.push(top.clone());
            }
            dirs.push(script_dir_clone.clone());
            dirs
        };

        let mut found_path = None;
        for sdir in &search_dirs {
            for candidate in [
                sdir.join(format!("{}.lua", rel_path)),
                sdir.join(&rel_path).join("init.lua"),
            ] {
                let normalized = candidate
                    .canonicalize()
                    .unwrap_or_else(|_| candidate.clone());
                if normalized.exists() {
                    found_path = Some(normalized);
                    break;
                }
            }
            if found_path.is_some() {
                break;
            }
        }

        let fpath = found_path
            .ok_or_else(|| mlua::Error::RuntimeError(format!("module '{}' not found", modname)))?;

        let code = std::fs::read_to_string(&fpath).map_err(|e| {
            mlua::Error::RuntimeError(format!("failed to read {}: {}", fpath.display(), e))
        })?;

        let mod_dir = fpath.parent().unwrap().to_path_buf();
        dir_stack.lock().unwrap().push(mod_dir);

        let chunk_name = format!("@{}", modname);
        let result = lua_ctx
            .load(&code)
            .set_name(&chunk_name)
            .call::<LuaValue>(());

        dir_stack.lock().unwrap().pop();

        match result {
            Ok(val) => {
                let store_val = if val == LuaValue::Nil {
                    LuaValue::Boolean(true)
                } else {
                    val.clone()
                };
                let key = lua_ctx.create_registry_value(store_val.clone())?;
                loaded_modules.lock().unwrap().insert(modname, key);
                Ok(if val == LuaValue::Nil {
                    LuaValue::Boolean(true)
                } else {
                    val
                })
            }
            Err(e) => Err(e),
        }
    })?;
    lua.globals().set("require", require_fn)?;
    Ok(())
}

fn truncate_log(msg: String) -> String {
    if msg.len() > MAX_LOG_LEN {
        // 日志上限按字节计算，截断点必须退到完整字符边界。
        let mut end = MAX_LOG_LEN;
        while !msg.is_char_boundary(end) {
            end -= 1;
        }
        msg[..end].to_string()
    } else {
        msg
    }
}

#[cfg(test)]
mod tests {
    use super::{truncate_log, MAX_LOG_LEN};

    #[test]
    fn truncate_log_preserves_utf8_boundaries() {
        assert_eq!(truncate_log("短日志".to_string()), "短日志");
        assert_eq!(truncate_log("a".repeat(MAX_LOG_LEN + 1)).len(), MAX_LOG_LEN);
        for character in ["键", "😀"] {
            for remaining in 1..character.len() {
                let prefix = "a".repeat(MAX_LOG_LEN - remaining);
                assert_eq!(truncate_log(format!("{prefix}{character}")), prefix);
            }
            let exact = format!("{}键", "a".repeat(MAX_LOG_LEN - 3));
            assert_eq!(truncate_log(format!("{exact}尾")), exact);
        }
    }
}

fn lua_value_to_string(v: &LuaValue) -> String {
    match v {
        LuaValue::String(s) => match s.to_str() {
            Ok(valid) => valid.to_string(),
            Err(_) => {
                let bytes: &[u8] = &s.as_bytes();
                let mut out = String::with_capacity(bytes.len());
                let mut i = 0;
                while i < bytes.len() {
                    let b = bytes[i];
                    if b < 0x80 {
                        out.push(b as char);
                        i += 1;
                    } else {
                        let seq_len = if b >= 0xF0 {
                            4
                        } else if b >= 0xE0 {
                            3
                        } else if b >= 0xC0 {
                            2
                        } else {
                            0
                        };
                        if seq_len >= 2 && i + seq_len <= bytes.len() {
                            if let Ok(ch) = std::str::from_utf8(&bytes[i..i + seq_len]) {
                                out.push_str(ch);
                                i += seq_len;
                                continue;
                            }
                        }
                        out.push_str(&format!("\\u00{:02x}", b));
                        i += 1;
                    }
                }
                out
            }
        },
        LuaValue::Integer(i) => i.to_string(),
        LuaValue::Number(n) => n.to_string(),
        LuaValue::Boolean(b) => b.to_string(),
        LuaValue::Nil => "nil".to_string(),
        _ => format!("{:?}", v),
    }
}
