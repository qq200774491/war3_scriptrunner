use crate::config::{self, AutoRoomConfig, PlayerConfig, ReporterKind, TestArgs};
use crate::room::{
    LogEntry, OutEvent, RoomExecutionControl, RoomLaunchOptions, RoomManager, RoomStatus,
};
use crate::simulation::RankingEntry;
use mlua::prelude::*;
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet, VecDeque};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tempfile::TempDir;
use thiserror::Error;
use walkdir::{DirEntry, WalkDir};

const TEST_DSL: &str = include_str!("dsl.lua");
const TEST_HOOK_INTERVAL: u32 = 10_000;
const TEST_TIMEOUT_MESSAGE: &str = "MLS test case deadline exceeded";
const MAX_SECTION_INVOCATIONS: usize = 1024;

#[derive(Debug, Error)]
#[error("{0}")]
struct RunnerError(String);

type RunnerResult<T> = Result<T, RunnerError>;

#[derive(Debug, Clone, Deserialize, Default)]
#[serde(default, deny_unknown_fields)]
struct TestManifest {
    script_dir: Option<PathBuf>,
    cloud_bootstrap: Vec<PathBuf>,
    room: TestRoomConfig,
    timeouts: TestTimeouts,
}

#[derive(Debug, Clone, Deserialize, Default)]
#[serde(default, deny_unknown_fields)]
struct TestRoomConfig {
    mode_id: i32,
    map_version: Option<String>,
    env_type: Option<i32>,
    prebook_count: Option<i32>,
    rankings_loaded: Option<bool>,
    rankings: Option<HashMap<i32, Vec<RankingEntry>>>,
    players: Vec<PlayerConfig>,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(default, deny_unknown_fields)]
struct TestTimeouts {
    discovery: f64,
    startup: f64,
    case: f64,
    event: f64,
    shutdown: f64,
}

impl Default for TestTimeouts {
    fn default() -> Self {
        Self {
            discovery: 2.0,
            startup: 5.0,
            case: 15.0,
            event: 2.0,
            shutdown: 1.0,
        }
    }
}

#[derive(Debug, Clone)]
struct ResolvedSuite {
    test_dir: PathBuf,
    script_dir: PathBuf,
    bootstrap_files: Vec<PathBuf>,
    room: TestRoomConfig,
    timeouts: TestTimeouts,
    test_files: Vec<TestFile>,
}

#[derive(Debug, Clone)]
struct TestFile {
    absolute: PathBuf,
    relative: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct CaseMeta {
    index: usize,
    id: String,
    name: String,
    tags: Vec<String>,
    source: String,
    line: i32,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize)]
struct SectionKey {
    id: String,
    name: String,
    source: String,
    line: i32,
}

#[derive(Debug, Clone, Serialize)]
struct FailureRecord {
    kind: String,
    source: String,
    line: i32,
    message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    actual: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    expected: Option<String>,
    context: Vec<String>,
}

#[derive(Debug, Clone, Serialize)]
struct CloudLogRecord {
    level: String,
    source: String,
    message: String,
    player_index: i32,
}

impl From<&LogEntry> for CloudLogRecord {
    fn from(value: &LogEntry) -> Self {
        Self {
            level: value.level.clone(),
            source: value.source.clone(),
            message: value.message.clone(),
            player_index: value.player_index,
        }
    }
}

#[derive(Debug, Clone, Serialize)]
struct InvocationReport {
    section_path: Vec<String>,
    status: String,
    duration_ms: u128,
    assertions: usize,
    failures: Vec<FailureRecord>,
    cloud_logs: Vec<CloudLogRecord>,
    output: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    skip_reason: Option<String>,
}

#[derive(Debug, Clone, Serialize)]
struct TestCaseReport {
    id: String,
    name: String,
    tags: Vec<String>,
    source: String,
    line: i32,
    status: String,
    invocations: Vec<InvocationReport>,
}

#[derive(Debug, Clone, Serialize, Default)]
struct CountSummary {
    total: usize,
    passed: usize,
    failed: usize,
    skipped: usize,
}

#[derive(Debug, Clone, Serialize, Default)]
struct AssertionSummary {
    total: usize,
    failed: usize,
}

#[derive(Debug, Clone, Serialize, Default)]
struct ReportSummary {
    test_cases: CountSummary,
    invocations: CountSummary,
    assertions: AssertionSummary,
    duration_ms: u128,
}

#[derive(Debug, Clone, Serialize)]
struct SuiteReport {
    schema_version: u32,
    status: String,
    test_dir: String,
    script_dir: String,
    tests: Vec<TestCaseReport>,
    summary: ReportSummary,
    errors: Vec<String>,
}

#[derive(Debug, Default)]
struct RuntimeState {
    cases: Vec<CaseMeta>,
    source_ordinals: HashMap<String, usize>,
    assertions: usize,
    failures: Vec<FailureRecord>,
    info: Vec<String>,
    output: Vec<String>,
    skipped: Option<String>,
    selected_sections: Vec<SectionKey>,
    current_sections: Vec<SectionKey>,
    actual_path: Vec<SectionKey>,
    children: HashMap<Vec<String>, Vec<SectionKey>>,
    child_ordinals: HashMap<Vec<String>, usize>,
    auto_choice: HashMap<Vec<String>, String>,
}

#[derive(Debug, Default)]
struct ClientState {
    inboxes: HashMap<i32, VecDeque<OutEvent>>,
}

#[derive(Clone, Copy, Debug)]
struct TestClock {
    origin: Instant,
}

impl TestClock {
    fn start() -> Self {
        Self {
            origin: Instant::now(),
        }
    }

    fn now(self) -> f64 {
        self.origin.elapsed().as_secs_f64()
    }
}

pub fn run(args: &TestArgs, cli_script_dir: Option<&str>) -> u8 {
    let started = Instant::now();
    if let Err(error) = prepare_report_output(args) {
        eprintln!("mls-sim test: {error}");
        return 2;
    }
    let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        run_inner(args, cli_script_dir, started)
    }));

    match outcome {
        Ok(Ok((report, code))) => match write_report(args, &report) {
            Ok(()) => code,
            Err(error) => {
                eprintln!("mls-sim test: {error}");
                2
            }
        },
        Ok(Err(error)) => {
            let report = error_report(args, cli_script_dir, started, error.to_string());
            let _ = write_report(args, &report);
            2
        }
        Err(payload) => {
            let message = panic_payload_message(payload.as_ref());
            let report = error_report(
                args,
                cli_script_dir,
                started,
                format!("internal runner error: {message}"),
            );
            let _ = write_report(args, &report);
            3
        }
    }
}

fn run_inner(
    args: &TestArgs,
    cli_script_dir: Option<&str>,
    started: Instant,
) -> RunnerResult<(SuiteReport, u8)> {
    let suite = resolve_suite(args, cli_script_dir)?;
    let discovered = discover_cases(&suite)?;
    let selected: Vec<CaseMeta> = discovered
        .into_iter()
        .filter(|case| case_matches(case, &args.test_filters, &args.tags))
        .collect();
    if selected.is_empty() {
        return Err(RunnerError("no tests matched the requested filters".into()));
    }

    if args.list_tests {
        let tests = selected
            .into_iter()
            .map(|case| TestCaseReport {
                id: case.id,
                name: case.name,
                tags: case.tags,
                source: case.source,
                line: case.line,
                status: "not_run".into(),
                invocations: Vec::new(),
            })
            .collect::<Vec<_>>();
        let mut report = SuiteReport {
            schema_version: 1,
            status: "listed".into(),
            test_dir: path_text(&suite.test_dir),
            script_dir: path_text(&suite.script_dir),
            tests,
            summary: ReportSummary::default(),
            errors: Vec::new(),
        };
        report.summary.test_cases.total = report.tests.len();
        report.summary.duration_ms = started.elapsed().as_millis();
        return Ok((report, 0));
    }

    let tests = if args.fail_fast {
        run_cases_fail_fast(&suite, &selected, args.log_tail)?
    } else {
        run_cases(&suite, &selected, args.log_tail, args.jobs)?
    };

    let mut report = SuiteReport {
        schema_version: 1,
        status: "passed".into(),
        test_dir: path_text(&suite.test_dir),
        script_dir: path_text(&suite.script_dir),
        tests,
        summary: ReportSummary::default(),
        errors: Vec::new(),
    };
    summarize(&mut report, started.elapsed().as_millis());
    let code = if report.status == "passed" { 0 } else { 1 };
    Ok((report, code))
}

fn run_cases_fail_fast(
    suite: &ResolvedSuite,
    selected: &[CaseMeta],
    log_tail: usize,
) -> RunnerResult<Vec<TestCaseReport>> {
    let mut reports = Vec::new();
    for case in selected {
        let report = run_case(suite, case, log_tail)?;
        let failed = report.status == "failed";
        reports.push(report);
        if failed {
            break;
        }
    }
    Ok(reports)
}

fn run_cases(
    suite: &ResolvedSuite,
    selected: &[CaseMeta],
    log_tail: usize,
    jobs: usize,
) -> RunnerResult<Vec<TestCaseReport>> {
    let worker_count = jobs.min(selected.len());
    if worker_count <= 1 {
        return selected
            .iter()
            .map(|case| run_case(suite, case, log_tail))
            .collect();
    }

    let next_case = AtomicUsize::new(0);
    let results = Mutex::new(
        (0..selected.len())
            .map(|_| None)
            .collect::<Vec<Option<RunnerResult<TestCaseReport>>>>(),
    );
    let panics = Mutex::new(Vec::<(usize, String)>::new());

    std::thread::scope(|scope| {
        for _ in 0..worker_count {
            scope.spawn(|| loop {
                let index = next_case.fetch_add(1, Ordering::Relaxed);
                let Some(case) = selected.get(index) else {
                    break;
                };
                let outcome = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                    run_case(suite, case, log_tail)
                }));
                match outcome {
                    Ok(result) => results.lock().unwrap()[index] = Some(result),
                    Err(payload) => panics
                        .lock()
                        .unwrap()
                        .push((index, panic_payload_message(payload.as_ref()))),
                }
            });
        }
    });

    let mut panics = panics.into_inner().unwrap();
    if !panics.is_empty() {
        panics.sort_by_key(|(index, _)| *index);
        let (index, message) = panics.remove(0);
        panic!(
            "test worker panicked while running {}: {message}",
            selected[index].id
        );
    }

    results
        .into_inner()
        .unwrap()
        .into_iter()
        .enumerate()
        .map(|(index, result)| {
            result.unwrap_or_else(|| {
                panic!("test worker returned no result for {}", selected[index].id)
            })
        })
        .collect()
}

fn panic_payload_message(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(value) = payload.downcast_ref::<&str>() {
        (*value).to_string()
    } else if let Some(value) = payload.downcast_ref::<String>() {
        value.clone()
    } else {
        "unknown runner panic".to_string()
    }
}

fn resolve_suite(args: &TestArgs, cli_script_dir: Option<&str>) -> RunnerResult<ResolvedSuite> {
    let test_dir = canonical_directory(&args.test_dir, "test directory")?;
    let manifest_path = args
        .manifest
        .as_ref()
        .map(|path| absolute_from_current(path))
        .unwrap_or_else(|| test_dir.join("mls-test.json"));
    let (manifest, manifest_dir) = if manifest_path.is_file() {
        let text = fs::read_to_string(&manifest_path).map_err(|error| {
            RunnerError(format!(
                "failed to read manifest {}: {error}",
                manifest_path.display()
            ))
        })?;
        let manifest_value = serde_json::from_str::<serde_json::Value>(&text).map_err(|error| {
            RunnerError(format!(
                "failed to parse manifest {}: {error}",
                manifest_path.display()
            ))
        })?;
        validate_manifest_keys(&manifest_value)?;
        let parsed = serde_json::from_value::<TestManifest>(manifest_value).map_err(|error| {
            RunnerError(format!(
                "failed to parse manifest {}: {error}",
                manifest_path.display()
            ))
        })?;
        let base = manifest_path.parent().unwrap_or(&test_dir).to_path_buf();
        (parsed, base)
    } else if args.manifest.is_some() {
        return Err(RunnerError(format!(
            "manifest not found: {}",
            manifest_path.display()
        )));
    } else {
        (TestManifest::default(), test_dir.clone())
    };

    let script_candidate = if let Some(value) = cli_script_dir {
        absolute_from_current(Path::new(value))
    } else if let Some(value) = &manifest.script_dir {
        manifest_dir.join(value)
    } else if let Some(ancestor) = test_dir
        .ancestors()
        .find(|ancestor| ancestor.join("main.lua").is_file())
    {
        ancestor.to_path_buf()
    } else {
        return Err(RunnerError(
            "cloud script directory is not configured and no main.lua was found in the test directory or its ancestors".into(),
        ));
    };
    let script_dir = canonical_directory(&script_candidate, "cloud script directory")?;
    if !script_dir.join("main.lua").is_file() {
        return Err(RunnerError(format!(
            "main.lua not found in cloud script directory {}",
            script_dir.display()
        )));
    }

    let mut bootstrap_files = Vec::new();
    for relative in &manifest.cloud_bootstrap {
        let candidate = if relative.is_absolute() {
            relative.clone()
        } else {
            manifest_dir.join(relative)
        };
        let path = candidate.canonicalize().map_err(|error| {
            RunnerError(format!(
                "invalid cloud bootstrap path {}: {error}",
                candidate.display()
            ))
        })?;
        if !path.is_file() {
            return Err(RunnerError(format!(
                "cloud bootstrap is not a file: {}",
                path.display()
            )));
        }
        bootstrap_files.push(path);
    }

    let mut timeouts = manifest.timeouts;
    if let Some(value) = args.discovery_timeout {
        timeouts.discovery = value;
    }
    if let Some(value) = args.startup_timeout {
        timeouts.startup = value;
    }
    if let Some(value) = args.case_timeout {
        timeouts.case = value;
    }
    if let Some(value) = args.event_timeout {
        timeouts.event = value;
    }
    validate_timeouts(&timeouts)?;
    validate_room_config(&manifest.room)?;

    let test_files = discover_files(&test_dir)?;
    if test_files.is_empty() {
        return Err(RunnerError(format!(
            "no *_test.lua files found in {}",
            test_dir.display()
        )));
    }

    Ok(ResolvedSuite {
        test_dir,
        script_dir,
        bootstrap_files,
        room: manifest.room,
        timeouts,
        test_files,
    })
}

fn absolute_from_current(path: &Path) -> PathBuf {
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()
            .unwrap_or_else(|_| PathBuf::from("."))
            .join(path)
    }
}

fn canonical_directory(path: &Path, label: &str) -> RunnerResult<PathBuf> {
    let resolved = absolute_from_current(path)
        .canonicalize()
        .map_err(|error| RunnerError(format!("invalid {label} {}: {error}", path.display())))?;
    if !resolved.is_dir() {
        return Err(RunnerError(format!(
            "{label} is not a directory: {}",
            resolved.display()
        )));
    }
    Ok(resolved)
}

fn validate_timeouts(timeouts: &TestTimeouts) -> RunnerResult<()> {
    for (name, value) in [
        ("discovery", timeouts.discovery),
        ("startup", timeouts.startup),
        ("case", timeouts.case),
        ("event", timeouts.event),
        ("shutdown", timeouts.shutdown),
    ] {
        if !value.is_finite() || value <= 0.0 {
            return Err(RunnerError(format!(
                "timeouts.{name} must be a positive finite number"
            )));
        }
    }
    Ok(())
}

fn validate_room_config(room: &TestRoomConfig) -> RunnerResult<()> {
    let mut indices = HashSet::new();
    for player in &room.players {
        if player.index < 0 {
            return Err(RunnerError(format!(
                "player index must be non-negative: {}",
                player.index
            )));
        }
        if !indices.insert(player.index) {
            return Err(RunnerError(format!(
                "duplicate player index in manifest: {}",
                player.index
            )));
        }
    }
    Ok(())
}

fn validate_manifest_keys(value: &serde_json::Value) -> RunnerResult<()> {
    validate_object_keys(
        value,
        "manifest",
        &["script_dir", "cloud_bootstrap", "room", "timeouts"],
    )?;
    let Some(root) = value.as_object() else {
        return Err(RunnerError("manifest must be a JSON object".into()));
    };
    if let Some(room) = root.get("room") {
        validate_object_keys(
            room,
            "room",
            &[
                "mode_id",
                "map_version",
                "env_type",
                "prebook_count",
                "rankings_loaded",
                "rankings",
                "players",
            ],
        )?;
        if let Some(players) = room.get("players").and_then(|item| item.as_array()) {
            for (index, player) in players.iter().enumerate() {
                let path = format!("room.players[{index}]");
                validate_object_keys(
                    player,
                    &path,
                    &[
                        "index",
                        "name",
                        "items",
                        "map_level",
                        "map_exp",
                        "played_count",
                        "script_archive",
                        "common_archive",
                        "read_archive",
                        "cfg_archive",
                        "platform",
                        "activity",
                        "achievements",
                        "tasks",
                        "sign_in",
                        "community",
                        "guild",
                    ],
                )?;
                validate_optional_child(
                    player,
                    &path,
                    "platform",
                    &[
                        "guid",
                        "plat_level",
                        "vip_level",
                        "map_vip_level",
                        "vip_types",
                        "is_author",
                        "is_collected",
                        "is_backflow",
                    ],
                )?;
                validate_optional_child(
                    player,
                    &path,
                    "activity",
                    &["day_rounds", "since_last_game", "lottery_counts"],
                )?;
                validate_optional_child(player, &path, "achievements", &["point", "done"])?;
                validate_optional_child(player, &path, "tasks", &["progress"])?;
                if let Some(progress) = player
                    .get("tasks")
                    .and_then(|tasks| tasks.get("progress"))
                    .and_then(|progress| progress.as_object())
                {
                    for (task_id, task) in progress {
                        validate_object_keys(
                            task,
                            &format!("{path}.tasks.progress.{task_id}"),
                            &["total", "current", "done"],
                        )?;
                    }
                }
                validate_optional_child(
                    player,
                    &path,
                    "sign_in",
                    &["total", "cont_max", "cont_cur"],
                )?;
                validate_optional_child(
                    player,
                    &path,
                    "community",
                    &[
                        "has_topic",
                        "is_manager",
                        "topic_count",
                        "comment_count",
                        "happy_count",
                        "best_count",
                        "appraise_count",
                        "is_pinned",
                        "pet_adv_time",
                    ],
                )?;
                validate_optional_child(player, &path, "guild", &["level"])?;
            }
        }
        if let Some(rankings) = room.get("rankings").and_then(|item| item.as_object()) {
            for (ranking_id, entries) in rankings {
                if let Some(entries) = entries.as_array() {
                    for (index, entry) in entries.iter().enumerate() {
                        validate_object_keys(
                            entry,
                            &format!("room.rankings.{ranking_id}[{index}]"),
                            &["player_index", "player_name", "value"],
                        )?;
                    }
                }
            }
        }
    }
    if let Some(timeouts) = root.get("timeouts") {
        validate_object_keys(
            timeouts,
            "timeouts",
            &["discovery", "startup", "case", "event", "shutdown"],
        )?;
    }
    Ok(())
}

fn validate_optional_child(
    parent: &serde_json::Value,
    parent_path: &str,
    field: &str,
    allowed: &[&str],
) -> RunnerResult<()> {
    if let Some(child) = parent.get(field) {
        if child.is_null() {
            return Ok(());
        }
        validate_object_keys(child, &format!("{parent_path}.{field}"), allowed)?;
    }
    Ok(())
}

fn validate_object_keys(
    value: &serde_json::Value,
    path: &str,
    allowed: &[&str],
) -> RunnerResult<()> {
    let Some(object) = value.as_object() else {
        return Err(RunnerError(format!("{path} must be a JSON object")));
    };
    for key in object.keys() {
        if !allowed.contains(&key.as_str()) {
            return Err(RunnerError(format!("unknown manifest field: {path}.{key}")));
        }
    }
    Ok(())
}

fn discover_files(test_dir: &Path) -> RunnerResult<Vec<TestFile>> {
    let mut files = Vec::new();
    let walker = WalkDir::new(test_dir)
        .follow_links(false)
        .into_iter()
        .filter_entry(|entry| should_visit(entry));
    for entry in walker {
        let entry =
            entry.map_err(|error| RunnerError(format!("test discovery failed: {error}")))?;
        if !entry.file_type().is_file() {
            continue;
        }
        let name = entry.file_name().to_string_lossy();
        if !name.ends_with("_test.lua") {
            continue;
        }
        let relative = entry
            .path()
            .strip_prefix(test_dir)
            .map_err(|error| RunnerError(format!("test path error: {error}")))?;
        files.push(TestFile {
            absolute: entry.path().to_path_buf(),
            relative: normalize_path(relative),
        });
    }
    files.sort_by(|left, right| left.relative.cmp(&right.relative));
    Ok(files)
}

fn should_visit(entry: &DirEntry) -> bool {
    if entry.depth() == 0 {
        return true;
    }
    let name = entry.file_name().to_string_lossy();
    if name.starts_with('.') {
        return false;
    }
    if entry.file_type().is_dir() {
        !matches!(name.as_ref(), "helpers" | "fixtures" | "meta")
    } else {
        true
    }
}

fn normalize_path(path: &Path) -> String {
    path.to_string_lossy().replace('\\', "/")
}

fn path_text(path: &Path) -> String {
    let text = path.to_string_lossy();
    text.strip_prefix(r"\\?\").unwrap_or(&text).to_string()
}

fn discover_cases(suite: &ResolvedSuite) -> RunnerResult<Vec<CaseMeta>> {
    let deadline = Instant::now() + seconds(suite.timeouts.discovery);
    let (lua, state) = create_test_lua(&suite.test_dir, deadline, Vec::new())?;
    for file in &suite.test_files {
        let before = state.lock().unwrap().cases.len();
        execute_test_file(&lua, file)?;
        let after = state.lock().unwrap().cases.len();
        if before == after {
            return Err(RunnerError(format!(
                "test file registered no TEST_CASE entries: {}",
                file.relative
            )));
        }
    }
    let cases = state.lock().unwrap().cases.clone();
    if cases.is_empty() {
        return Err(RunnerError("no test cases were registered".into()));
    }
    Ok(cases)
}

fn case_matches(case: &CaseMeta, filters: &[String], tags: &[String]) -> bool {
    let filter_matches = filters.is_empty()
        || filters.iter().any(|pattern| {
            wildcard_match(pattern, &case.name) || wildcard_match(pattern, &case.id)
        });
    filter_matches && tags.iter().all(|tag| case.tags.contains(tag))
}

fn wildcard_match(pattern: &str, value: &str) -> bool {
    let pattern = pattern.chars().collect::<Vec<_>>();
    let value = value.chars().collect::<Vec<_>>();
    let (mut p, mut v, mut star, mut checkpoint) = (0usize, 0usize, None, 0usize);
    while v < value.len() {
        if p < pattern.len() && (pattern[p] == '?' || pattern[p] == value[v]) {
            p += 1;
            v += 1;
        } else if p < pattern.len() && pattern[p] == '*' {
            star = Some(p);
            p += 1;
            checkpoint = v;
        } else if let Some(star_index) = star {
            p = star_index + 1;
            checkpoint += 1;
            v = checkpoint;
        } else {
            return false;
        }
    }
    while p < pattern.len() && pattern[p] == '*' {
        p += 1;
    }
    p == pattern.len()
}

fn seconds(value: f64) -> Duration {
    Duration::from_secs_f64(value)
}

fn create_test_lua(
    test_root: &Path,
    deadline: Instant,
    selected_sections: Vec<SectionKey>,
) -> RunnerResult<(Lua, Arc<Mutex<RuntimeState>>)> {
    // The DSL captures debug.getinfo for source locations and immediately hides
    // the debug table before any user test file is loaded.
    let lua = unsafe {
        Lua::unsafe_new_with(
            (LuaStdLib::ALL_SAFE ^ LuaStdLib::IO) | LuaStdLib::DEBUG,
            LuaOptions::default(),
        )
    };
    lua.set_memory_limit(64 * 1024 * 1024).map_err(lua_error)?;
    lua.set_hook(
        mlua::HookTriggers::new().every_nth_instruction(TEST_HOOK_INTERVAL),
        move |_lua, _debug| {
            if Instant::now() >= deadline {
                Err(mlua::Error::RuntimeError(TEST_TIMEOUT_MESSAGE.into()))
            } else {
                Ok(mlua::VmState::Continue)
            }
        },
    );

    {
        let globals = lua.globals();
        let os_table: LuaTable = globals.get("os").map_err(lua_error)?;
        for name in [
            "execute",
            "exit",
            "getenv",
            "remove",
            "rename",
            "setlocale",
            "tmpname",
        ] {
            os_table.set(name, LuaNil).map_err(lua_error)?;
        }
        globals.set("io", LuaNil).map_err(lua_error)?;
    }

    crate::room::json_lua::install_json_lib(&lua).map_err(lua_error)?;
    install_test_md5(&lua)?;
    install_test_require(&lua, test_root)?;

    let state = Arc::new(Mutex::new(RuntimeState {
        selected_sections,
        ..RuntimeState::default()
    }));
    install_test_host_api(&lua, state.clone())?;
    lua.load(TEST_DSL)
        .set_name("@<mls-test-dsl>")
        .exec()
        .map_err(lua_error)?;
    Ok((lua, state))
}

fn install_test_md5(lua: &Lua) -> RunnerResult<()> {
    let function = lua
        .create_function(|_, value: LuaValue| {
            let bytes = match value {
                LuaValue::String(value) => value.as_bytes().to_vec(),
                other => other.to_string()?.into_bytes(),
            };
            Ok(format!("{:x}", md5::compute(bytes)))
        })
        .map_err(lua_error)?;
    lua.globals().set("md5", function).map_err(lua_error)
}

fn install_test_require(lua: &Lua, test_root: &Path) -> RunnerResult<()> {
    let root = test_root
        .canonicalize()
        .map_err(|error| RunnerError(format!("invalid test require root: {error}")))?;
    let loaded: Arc<Mutex<HashMap<String, mlua::RegistryKey>>> =
        Arc::new(Mutex::new(HashMap::new()));
    let require = lua
        .create_function(move |lua, module_name: String| {
            if module_name.is_empty()
                || module_name.contains("..")
                || Path::new(&module_name).is_absolute()
            {
                return Err(mlua::Error::RuntimeError(format!(
                    "invalid test module name: {module_name}"
                )));
            }
            if let Some(key) = loaded.lock().unwrap().get(&module_name) {
                return lua.registry_value::<LuaValue>(key);
            }

            let relative = if module_name.contains('/') || module_name.contains('\\') {
                module_name.replace('\\', "/")
            } else {
                module_name.replace('.', "/")
            };
            let mut found = None;
            for candidate in [
                root.join(format!("{relative}.lua")),
                root.join(&relative).join("init.lua"),
            ] {
                if let Ok(path) = candidate.canonicalize() {
                    if path.starts_with(&root) && path.is_file() {
                        found = Some(path);
                        break;
                    }
                }
            }
            let path = found.ok_or_else(|| {
                mlua::Error::RuntimeError(format!("test module '{module_name}' not found"))
            })?;
            let code = fs::read_to_string(&path).map_err(mlua::Error::external)?;
            let chunk_name = format!(
                "@{}",
                normalize_path(path.strip_prefix(&root).unwrap_or(&path))
            );
            let value = lua.load(&code).set_name(&chunk_name).call::<LuaValue>(())?;
            let stored = if value == LuaValue::Nil {
                LuaValue::Boolean(true)
            } else {
                value
            };
            let key = lua.create_registry_value(stored.clone())?;
            loaded.lock().unwrap().insert(module_name, key);
            Ok(stored)
        })
        .map_err(lua_error)?;
    lua.globals().set("require", require).map_err(lua_error)
}

fn install_test_host_api(lua: &Lua, state: Arc<Mutex<RuntimeState>>) -> RunnerResult<()> {
    let globals = lua.globals();

    let register_state = state.clone();
    globals
        .set(
            "__mls_test_register",
            lua.create_function(
                move |_, (name, raw_tags, source, line): (String, String, String, i32)| {
                    let tags = parse_tags(&raw_tags).map_err(mlua::Error::RuntimeError)?;
                    let source = normalize_source(&source);
                    let mut state = register_state.lock().unwrap();
                    let ordinal = state.source_ordinals.entry(source.clone()).or_default();
                    *ordinal += 1;
                    let ordinal = *ordinal;
                    let index = state.cases.len() + 1;
                    let id = format!("{source}:{line}#{ordinal}");
                    state.cases.push(CaseMeta {
                        index,
                        id,
                        name,
                        tags,
                        source,
                        line,
                    });
                    Ok(index)
                },
            )
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let assertion_state = state.clone();
    globals
        .set(
            "__mls_test_assertion",
            lua.create_function(
                move |_,
                      (kind, passed, actual, expected, message, source, line): (
                    String,
                    bool,
                    Option<String>,
                    Option<String>,
                    Option<String>,
                    String,
                    i32,
                )| {
                    let mut state = assertion_state.lock().unwrap();
                    state.assertions += 1;
                    if !passed {
                        let context = state.info.clone();
                        state.failures.push(FailureRecord {
                            kind,
                            source: normalize_source(&source),
                            line,
                            message: message.unwrap_or_else(|| "assertion failed".into()),
                            actual,
                            expected,
                            context,
                        });
                    }
                    Ok(())
                },
            )
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let info_state = state.clone();
    globals
        .set(
            "__mls_test_info",
            lua.create_function(move |_, value: String| {
                info_state.lock().unwrap().info.push(value);
                Ok(())
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let skip_state = state.clone();
    globals
        .set(
            "__mls_test_skip",
            lua.create_function(
                move |_, (message, _source, _line): (Option<String>, String, i32)| {
                    skip_state.lock().unwrap().skipped =
                        Some(message.unwrap_or_else(|| "skipped".into()));
                    Ok(())
                },
            )
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let error_state = state.clone();
    globals
        .set(
            "__mls_test_error",
            lua.create_function(move |_, (message, source, line): (String, String, i32)| {
                let mut state = error_state.lock().unwrap();
                let context = state.info.clone();
                state.failures.push(FailureRecord {
                    kind: "LUA_ERROR".into(),
                    source: normalize_source(&source),
                    line,
                    message,
                    actual: None,
                    expected: None,
                    context,
                });
                Ok(())
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let enter_state = state.clone();
    globals
        .set(
            "__mls_test_section_enter",
            lua.create_function(
                move |_, (name, source, line, aborted): (String, String, i32, bool)| {
                    let source = normalize_source(&source);
                    let mut state = enter_state.lock().unwrap();
                    let parent: Vec<String> = state
                        .current_sections
                        .iter()
                        .map(|section| section.id.clone())
                        .collect();
                    let ordinal = state.child_ordinals.entry(parent.clone()).or_default();
                    *ordinal += 1;
                    let key = SectionKey {
                        id: format!("{source}:{line}:{name}#{}", *ordinal),
                        name,
                        source,
                        line,
                    };
                    state
                        .children
                        .entry(parent.clone())
                        .or_default()
                        .push(key.clone());
                    if aborted {
                        return Ok(false);
                    }
                    let depth = state.current_sections.len();
                    let enter = if let Some(selected) = state.selected_sections.get(depth) {
                        selected.id == key.id
                    } else if let Some(choice) = state.auto_choice.get(&parent) {
                        choice == &key.id
                    } else {
                        state.auto_choice.insert(parent, key.id.clone());
                        true
                    };
                    if enter {
                        state.current_sections.push(key);
                        if state.current_sections.len() > state.actual_path.len() {
                            state.actual_path = state.current_sections.clone();
                        }
                    }
                    Ok(enter)
                },
            )
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let leave_state = state.clone();
    globals
        .set(
            "__mls_test_section_leave",
            lua.create_function(move |_, ()| {
                leave_state.lock().unwrap().current_sections.pop();
                Ok(())
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let print_state = state;
    globals
        .set(
            "print",
            lua.create_function(move |_, values: LuaMultiValue| {
                let line = values
                    .iter()
                    .map(lua_display)
                    .collect::<Vec<_>>()
                    .join("\t");
                print_state.lock().unwrap().output.push(line);
                Ok(())
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    Ok(())
}

fn parse_tags(raw: &str) -> Result<Vec<String>, String> {
    if raw.is_empty() {
        return Ok(Vec::new());
    }
    let mut rest = raw;
    let mut tags = Vec::new();
    while !rest.is_empty() {
        if !rest.starts_with('[') {
            return Err(format!("invalid Catch2 tag expression: {raw}"));
        }
        let end = rest
            .find(']')
            .ok_or_else(|| format!("invalid Catch2 tag expression: {raw}"))?;
        let tag = &rest[1..end];
        if tag.is_empty() || tag.contains('[') {
            return Err(format!("invalid Catch2 tag expression: {raw}"));
        }
        if !tags.iter().any(|existing| existing == tag) {
            tags.push(tag.to_string());
        }
        rest = &rest[end + 1..];
    }
    Ok(tags)
}

fn normalize_source(source: &str) -> String {
    source.trim_start_matches('@').replace('\\', "/")
}

fn lua_display(value: &LuaValue) -> String {
    match value {
        LuaValue::Nil => "nil".into(),
        LuaValue::Boolean(value) => value.to_string(),
        LuaValue::Integer(value) => value.to_string(),
        LuaValue::Number(value) => value.to_string(),
        LuaValue::String(value) => String::from_utf8_lossy(&value.as_bytes()).into_owned(),
        other => other.type_name().to_string(),
    }
}

fn execute_test_file(lua: &Lua, file: &TestFile) -> RunnerResult<()> {
    let code = fs::read_to_string(&file.absolute).map_err(|error| {
        RunnerError(format!(
            "failed to read test file {}: {error}",
            file.relative
        ))
    })?;
    lua.load(&code)
        .set_name(format!("@{}", file.relative))
        .exec()
        .map_err(|error| RunnerError(format!("failed to load {}: {error}", file.relative)))
}

fn lua_error(error: mlua::Error) -> RunnerError {
    RunnerError(error.to_string())
}

struct InvocationOutcome {
    report: InvocationReport,
    actual_path: Vec<SectionKey>,
    children: HashMap<Vec<String>, Vec<SectionKey>>,
}

fn run_case(
    suite: &ResolvedSuite,
    case: &CaseMeta,
    log_tail: usize,
) -> RunnerResult<TestCaseReport> {
    let mut pending = VecDeque::from([Vec::<SectionKey>::new()]);
    let mut scheduled = HashSet::from([Vec::<String>::new()]);
    let mut visited = HashSet::<Vec<String>>::new();
    let mut known_children = HashMap::<Vec<String>, Vec<SectionKey>>::new();
    let mut invocations = Vec::new();

    while let Some(selected_path) = pending.pop_front() {
        if invocations.len() >= MAX_SECTION_INVOCATIONS {
            invocations.push(InvocationReport {
                section_path: selected_path.iter().map(|item| item.name.clone()).collect(),
                status: "failed".into(),
                duration_ms: 0,
                assertions: 0,
                failures: vec![FailureRecord {
                    kind: "SECTION_LIMIT".into(),
                    source: case.source.clone(),
                    line: case.line,
                    message: format!(
                        "TEST_CASE expanded beyond {MAX_SECTION_INVOCATIONS} SECTION invocations"
                    ),
                    actual: None,
                    expected: None,
                    context: Vec::new(),
                }],
                cloud_logs: Vec::new(),
                output: Vec::new(),
                skip_reason: None,
            });
            break;
        }

        let mut outcome = run_invocation(suite, case, selected_path, log_tail)?;
        let actual_ids = section_ids(&outcome.actual_path);
        visited.insert(actual_ids);

        for (parent, children) in &outcome.children {
            if let Some(expected) = known_children.get(parent) {
                if section_ids(expected) != section_ids(children) {
                    outcome.report.status = "failed".into();
                    outcome.report.failures.push(FailureRecord {
                        kind: "NON_DETERMINISTIC_SECTION".into(),
                        source: case.source.clone(),
                        line: case.line,
                        message: format!(
                            "SECTION layout changed while replaying parent path {}",
                            parent.join(" / ")
                        ),
                        actual: Some(format!("{:?}", section_ids(children))),
                        expected: Some(format!("{:?}", section_ids(expected))),
                        context: Vec::new(),
                    });
                }
            } else {
                known_children.insert(parent.clone(), children.clone());
            }
        }
        invocations.push(outcome.report);

        let leaf_paths = known_leaf_paths(&known_children);
        for path in leaf_paths {
            let ids = section_ids(&path);
            if !visited.contains(&ids) && scheduled.insert(ids) {
                pending.push_back(path);
            }
        }
        if known_children.is_empty() {
            break;
        }
    }

    let status = if invocations.iter().any(|item| item.status == "failed") {
        "failed"
    } else if invocations.iter().all(|item| item.status == "skipped") {
        "skipped"
    } else {
        "passed"
    };
    Ok(TestCaseReport {
        id: case.id.clone(),
        name: case.name.clone(),
        tags: case.tags.clone(),
        source: case.source.clone(),
        line: case.line,
        status: status.into(),
        invocations,
    })
}

fn section_ids(path: &[SectionKey]) -> Vec<String> {
    path.iter().map(|item| item.id.clone()).collect()
}

fn known_leaf_paths(children: &HashMap<Vec<String>, Vec<SectionKey>>) -> Vec<Vec<SectionKey>> {
    fn visit(
        parent_ids: Vec<String>,
        parent_path: Vec<SectionKey>,
        children: &HashMap<Vec<String>, Vec<SectionKey>>,
        output: &mut Vec<Vec<SectionKey>>,
    ) {
        let Some(items) = children.get(&parent_ids) else {
            if !parent_path.is_empty() {
                output.push(parent_path);
            }
            return;
        };
        if items.is_empty() {
            if !parent_path.is_empty() {
                output.push(parent_path);
            }
            return;
        }
        for item in items {
            let mut ids = parent_ids.clone();
            ids.push(item.id.clone());
            let mut path = parent_path.clone();
            path.push(item.clone());
            if children.contains_key(&ids) {
                visit(ids, path, children, output);
            } else {
                output.push(path);
            }
        }
    }

    let mut output = Vec::new();
    visit(Vec::new(), Vec::new(), children, &mut output);
    output
}

fn run_invocation(
    suite: &ResolvedSuite,
    case: &CaseMeta,
    selected_sections: Vec<SectionKey>,
    log_tail: usize,
) -> RunnerResult<InvocationOutcome> {
    let started = Instant::now();
    let archive_dir = Arc::new(TempDir::new().map_err(|error| {
        RunnerError(format!(
            "failed to create temporary archive directory: {error}"
        ))
    })?);
    let players = config::build_players_from_config(&suite.room.players);
    let player_indices = players.keys().copied().collect::<Vec<_>>();
    let auto_config = AutoRoomConfig {
        script_dir: path_text(&suite.script_dir),
        mode_id: suite.room.mode_id,
        map_version: suite.room.map_version.clone(),
        env_type: suite.room.env_type,
        prebook_count: suite.room.prebook_count,
        rankings_loaded: suite.room.rankings_loaded,
        rankings: suite.room.rankings.clone(),
        players: suite.room.players.clone(),
    };
    let simulation = config::build_room_simulation_from_config(Some(&auto_config), &players);
    let control = Arc::new(RoomExecutionControl::new());
    let startup_deadline = Instant::now() + seconds(suite.timeouts.startup);
    control.set_deadline(startup_deadline);
    let manager = Arc::new(Mutex::new(RoomManager::new()));
    let room_id = manager.lock().unwrap().create_room_with_options(
        suite.script_dir.clone(),
        suite.room.mode_id,
        players,
        simulation,
        archive_dir.path().to_string_lossy().into_owned(),
        RoomLaunchOptions {
            bootstrap_files: suite.bootstrap_files.clone(),
            execution_control: Some(control.clone()),
            archive_guard: Some(archive_dir.clone()),
            // 每个用例都是全新房间，存档只在内存中往返，不需要也不读取磁盘文件。
            persist_archives_to_disk: false,
        },
    );

    let mut early_failures = Vec::new();
    if let Err(message) = wait_for_room_start(&manager, &room_id, startup_deadline) {
        early_failures.push(FailureRecord {
            kind: "ROOM_STARTUP".into(),
            source: case.source.clone(),
            line: case.line,
            message,
            actual: None,
            expected: None,
            context: Vec::new(),
        });
    }

    if early_failures.is_empty() {
        let remaining = startup_deadline.saturating_duration_since(Instant::now());
        let barrier_ok = manager
            .lock()
            .unwrap()
            .get_room(&room_id)
            .is_some_and(|room| room.barrier(remaining));
        if !barrier_ok {
            early_failures.push(FailureRecord {
                kind: "ROOM_STARTUP_TIMEOUT".into(),
                source: case.source.clone(),
                line: case.line,
                message: "cloud room did not finish _roomloaded before startup timeout".into(),
                actual: None,
                expected: None,
                context: Vec::new(),
            });
        }
    }

    if early_failures.is_empty() {
        let manager = manager.lock().unwrap();
        let room = manager.get_room(&room_id).unwrap();
        for player_index in &player_indices {
            room.drain_events(*player_index);
        }
    }

    let case_deadline = Instant::now() + seconds(suite.timeouts.case);
    control.set_deadline(case_deadline);
    let state = if early_failures.is_empty() {
        match execute_case_in_test_vm(
            suite,
            case,
            selected_sections,
            manager.clone(),
            room_id.clone(),
            case_deadline,
        ) {
            Ok(state) => state,
            Err(error) => {
                let mut state = RuntimeState::default();
                state.failures.push(FailureRecord {
                    kind: if error.0.contains(TEST_TIMEOUT_MESSAGE) {
                        "TEST_TIMEOUT".into()
                    } else {
                        "TEST_RUNTIME".into()
                    },
                    source: case.source.clone(),
                    line: case.line,
                    message: error.to_string(),
                    actual: None,
                    expected: None,
                    context: Vec::new(),
                });
                Arc::new(Mutex::new(state))
            }
        }
    } else {
        let mut state = RuntimeState::default();
        state.selected_sections = selected_sections;
        state.failures = early_failures;
        Arc::new(Mutex::new(state))
    };

    if let Some(message) = room_failure_after_case(&manager, &room_id) {
        let mut state = state.lock().unwrap();
        let context = state.info.clone();
        state.failures.push(FailureRecord {
            kind: "ROOM_STOPPED".into(),
            source: case.source.clone(),
            line: case.line,
            message,
            actual: None,
            expected: None,
            context,
        });
    }

    let mut logs = Vec::new();
    {
        let state_guard = state.lock().unwrap();
        if !state_guard.failures.is_empty() {
            logs = cloud_log_tail(&manager, &room_id, log_tail);
        }
    }

    control.cancel();
    let shutdown_ok = manager
        .lock()
        .unwrap()
        .destroy_room_and_wait(&room_id, seconds(suite.timeouts.shutdown));
    if !shutdown_ok {
        state.lock().unwrap().failures.push(FailureRecord {
            kind: "ROOM_SHUTDOWN_TIMEOUT".into(),
            source: case.source.clone(),
            line: case.line,
            message: "cloud room did not stop within shutdown timeout".into(),
            actual: None,
            expected: None,
            context: Vec::new(),
        });
    }

    let state = Arc::try_unwrap(state)
        .map_err(|_| RunnerError("test runtime state is still referenced".into()))?
        .into_inner()
        .map_err(|_| RunnerError("test runtime state was poisoned".into()))?;
    let status = if !state.failures.is_empty() {
        "failed"
    } else if state.skipped.is_some() {
        "skipped"
    } else {
        "passed"
    };
    let actual_path = state.actual_path.clone();
    Ok(InvocationOutcome {
        report: InvocationReport {
            section_path: actual_path.iter().map(|item| item.name.clone()).collect(),
            status: status.into(),
            duration_ms: started.elapsed().as_millis(),
            assertions: state.assertions,
            failures: state.failures,
            cloud_logs: logs,
            output: state.output,
            skip_reason: state.skipped,
        },
        actual_path,
        children: state.children,
    })
}

fn wait_for_room_start(
    manager: &Arc<Mutex<RoomManager>>,
    room_id: &str,
    deadline: Instant,
) -> Result<(), String> {
    loop {
        let (status, error_message) = {
            let manager = manager.lock().unwrap();
            let room = manager
                .get_room(room_id)
                .ok_or_else(|| "cloud room disappeared during startup".to_string())?;
            let shared = room.shared.read().unwrap();
            (shared.status.clone(), shared.error_message.clone())
        };
        match status {
            RoomStatus::Running => return Ok(()),
            RoomStatus::Error => {
                return Err(if error_message.is_empty() {
                    "cloud room failed during startup".into()
                } else {
                    error_message
                })
            }
            RoomStatus::Stopped => return Err("cloud room stopped during startup".into()),
            RoomStatus::Created => {}
        }
        if Instant::now() >= deadline {
            return Err("cloud room startup timed out".into());
        }
        std::thread::sleep(Duration::from_millis(5));
    }
}

fn room_failure_after_case(manager: &Arc<Mutex<RoomManager>>, room_id: &str) -> Option<String> {
    let manager = manager.lock().unwrap();
    let room = manager.get_room(room_id)?;
    let shared = room.shared.read().unwrap();
    match shared.status {
        RoomStatus::Running => None,
        _ => Some(if shared.error_message.is_empty() {
            format!("cloud room ended with status {}", shared.status)
        } else {
            shared.error_message.clone()
        }),
    }
}

fn cloud_log_tail(
    manager: &Arc<Mutex<RoomManager>>,
    room_id: &str,
    limit: usize,
) -> Vec<CloudLogRecord> {
    let manager = manager.lock().unwrap();
    let Some(room) = manager.get_room(room_id) else {
        return Vec::new();
    };
    let shared = room.shared.read().unwrap();
    let skip = shared.log_buffer.len().saturating_sub(limit);
    shared
        .log_buffer
        .iter()
        .skip(skip)
        .map(CloudLogRecord::from)
        .collect()
}

fn execute_case_in_test_vm(
    suite: &ResolvedSuite,
    case: &CaseMeta,
    selected_sections: Vec<SectionKey>,
    manager: Arc<Mutex<RoomManager>>,
    room_id: String,
    deadline: Instant,
) -> RunnerResult<Arc<Mutex<RuntimeState>>> {
    let (lua, state) = create_test_lua(&suite.test_dir, deadline, selected_sections)?;
    for file in &suite.test_files {
        execute_test_file(&lua, file)?;
    }
    {
        let state = state.lock().unwrap();
        let loaded = state.cases.get(case.index - 1).ok_or_else(|| {
            RunnerError(format!("test case disappeared during replay: {}", case.id))
        })?;
        if loaded != case {
            return Err(RunnerError(format!(
                "test registration changed during replay: expected {}, got {}",
                case.id, loaded.id
            )));
        }
    }

    let client_state = Arc::new(Mutex::new(ClientState::default()));
    let context = create_test_context(
        &lua,
        manager,
        room_id,
        client_state,
        deadline,
        seconds(suite.timeouts.event),
    )?;
    let run_case: LuaFunction = lua
        .globals()
        .get("__mls_test_run_case")
        .map_err(lua_error)?;
    if let Err(error) = run_case.call::<()>((case.index, context)) {
        let message = error.to_string();
        if !message.contains("__MLS_TEST_FATAL__") && !message.contains("__MLS_TEST_SKIP__") {
            let mut state = state.lock().unwrap();
            let context = state.info.clone();
            state.failures.push(FailureRecord {
                kind: if message.contains(TEST_TIMEOUT_MESSAGE) {
                    "TEST_TIMEOUT".into()
                } else {
                    "LUA_ERROR".into()
                },
                source: case.source.clone(),
                line: case.line,
                message,
                actual: None,
                expected: None,
                context,
            });
        }
    }
    drop(lua);
    Ok(state)
}

fn create_test_context(
    lua: &Lua,
    manager: Arc<Mutex<RoomManager>>,
    room_id: String,
    client_state: Arc<Mutex<ClientState>>,
    deadline: Instant,
    event_timeout: Duration,
) -> RunnerResult<LuaTable> {
    let context = lua.create_table().map_err(lua_error)?;
    let cache = lua.create_table().map_err(lua_error)?;
    let cache_key = lua.create_registry_value(cache).map_err(lua_error)?;
    let clock = TestClock::start();

    let client_manager = manager.clone();
    let client_room_id = room_id.clone();
    let client_shared = client_state.clone();
    context
        .set(
            "client",
            lua.create_function(move |lua, (_self, player_index): (LuaTable, i32)| {
                let cache: LuaTable = lua.registry_value(&cache_key)?;
                if let Ok(existing) = cache.raw_get::<LuaTable>(player_index) {
                    return Ok(existing);
                }
                let exists = client_manager
                    .lock()
                    .unwrap()
                    .get_room(&client_room_id)
                    .is_some_and(|room| room.has_player(player_index));
                if !exists {
                    return Err(mlua::Error::RuntimeError(format!(
                        "player {player_index} does not exist in the test room"
                    )));
                }
                let client = create_client_table(
                    lua,
                    client_manager.clone(),
                    client_room_id.clone(),
                    client_shared.clone(),
                    player_index,
                    clock,
                    deadline,
                    event_timeout,
                )?;
                cache.raw_set(player_index, client.clone())?;
                Ok(client)
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    let delay_manager = manager.clone();
    let delay_room_id = room_id.clone();
    context
        .set(
            "setServerResponseDelay",
            lua.create_function(move |_, (_self, seconds): (LuaTable, f64)| {
                let manager = delay_manager.lock().unwrap();
                let room = manager.get_room(&delay_room_id).ok_or_else(|| {
                    mlua::Error::RuntimeError("cloud room no longer exists".into())
                })?;
                room.set_server_response_delay(seconds)
                    .map_err(mlua::Error::RuntimeError)
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)?;

    install_lifecycle_method(
        lua,
        &context,
        "playerJoin",
        manager.clone(),
        room_id.clone(),
        deadline,
        LifecycleAction::Join,
    )?;
    install_lifecycle_method(
        lua,
        &context,
        "playerLeave",
        manager.clone(),
        room_id.clone(),
        deadline,
        LifecycleAction::Leave,
    )?;
    install_lifecycle_method(
        lua,
        &context,
        "playerExit",
        manager,
        room_id,
        deadline,
        LifecycleAction::Exit,
    )?;
    Ok(context)
}

fn create_client_table(
    lua: &Lua,
    manager: Arc<Mutex<RoomManager>>,
    room_id: String,
    client_state: Arc<Mutex<ClientState>>,
    player_index: i32,
    clock: TestClock,
    deadline: Instant,
    event_timeout: Duration,
) -> LuaResult<LuaTable> {
    let client = lua.create_table()?;
    client.set("eventTimeout", event_timeout.as_secs_f64())?;

    client.set(
        "now",
        lua.create_function(move |_, _self: LuaTable| Ok(clock.now()))?,
    )?;

    let send_manager = manager.clone();
    let send_room_id = room_id.clone();
    client.set(
        "send",
        lua.create_function(
            move |_, (_self, event_name, event_data): (LuaTable, String, String)| {
                let manager = send_manager.lock().unwrap();
                let room = manager.get_room(&send_room_id).ok_or_else(|| {
                    mlua::Error::RuntimeError("cloud room no longer exists".into())
                })?;
                Ok(room.send_event(event_name, event_data, player_index))
            },
        )?,
    )?;

    let poll_manager = manager.clone();
    let poll_room_id = room_id.clone();
    let poll_state = client_state.clone();
    client.set(
        "poll",
        lua.create_function(move |lua, _self: LuaTable| {
            drain_room_events(&poll_manager, &poll_room_id, player_index, &poll_state)?;
            let events = {
                let mut state = poll_state.lock().unwrap();
                state
                    .inboxes
                    .entry(player_index)
                    .or_default()
                    .drain(..)
                    .collect::<Vec<_>>()
            };
            events_to_lua(lua, events)
        })?,
    )?;

    let wait_manager = manager.clone();
    let wait_room_id = room_id.clone();
    let wait_state = client_state.clone();
    client.set(
        "waitEvent",
        lua.create_function(move |lua, (_self, options): (LuaTable, Option<LuaTable>)| {
            let name = options
                .as_ref()
                .and_then(|value| value.get::<Option<String>>("name").ok())
                .flatten();
            let requested_timeout = options
                .as_ref()
                .and_then(|value| value.get::<Option<f64>>("timeout").ok())
                .flatten();
            let requested_timeout = match requested_timeout {
                Some(value) if value.is_finite() && value >= 0.0 => Duration::from_secs_f64(value),
                Some(_) => {
                    return Err(mlua::Error::RuntimeError(
                        "waitEvent timeout must be a non-negative finite number".into(),
                    ))
                }
                None => event_timeout,
            };
            let wait_deadline = std::cmp::min(Instant::now() + requested_timeout, deadline);
            loop {
                drain_room_events(&wait_manager, &wait_room_id, player_index, &wait_state)?;
                let event = {
                    let mut state = wait_state.lock().unwrap();
                    let inbox = state.inboxes.entry(player_index).or_default();
                    let position = inbox
                        .iter()
                        .position(|event| name.as_ref().is_none_or(|name| &event.ename == name));
                    position.and_then(|position| inbox.remove(position))
                };
                if let Some(event) = event {
                    return Ok(LuaValue::Table(event_to_lua(lua, event)?));
                }
                ensure_room_running(&wait_manager, &wait_room_id)?;
                if Instant::now() >= wait_deadline {
                    return Ok(LuaValue::Nil);
                }
                // 房间线程已按命令即时唤醒，这里的轮询间隔决定单次请求往返的下限。
                std::thread::sleep(Duration::from_millis(1));
            }
        })?,
    )?;

    let clear_manager = manager;
    let clear_room_id = room_id;
    let clear_state = client_state;
    client.set(
        "clearEvents",
        lua.create_function(move |_, _self: LuaTable| {
            let queued = {
                let mut state = clear_state.lock().unwrap();
                state
                    .inboxes
                    .entry(player_index)
                    .or_default()
                    .drain(..)
                    .count()
            };
            let drained = {
                let manager = clear_manager.lock().unwrap();
                manager
                    .get_room(&clear_room_id)
                    .map(|room| room.drain_events(player_index).len())
                    .unwrap_or_default()
            };
            Ok(queued + drained)
        })?,
    )?;

    Ok(client)
}

fn drain_room_events(
    manager: &Arc<Mutex<RoomManager>>,
    room_id: &str,
    player_index: i32,
    client_state: &Arc<Mutex<ClientState>>,
) -> LuaResult<()> {
    let events = {
        let manager = manager.lock().unwrap();
        let room = manager
            .get_room(room_id)
            .ok_or_else(|| mlua::Error::RuntimeError("cloud room no longer exists".into()))?;
        room.drain_events(player_index)
    };
    client_state
        .lock()
        .unwrap()
        .inboxes
        .entry(player_index)
        .or_default()
        .extend(events);
    Ok(())
}

fn ensure_room_running(manager: &Arc<Mutex<RoomManager>>, room_id: &str) -> LuaResult<()> {
    let manager = manager.lock().unwrap();
    let room = manager
        .get_room(room_id)
        .ok_or_else(|| mlua::Error::RuntimeError("cloud room no longer exists".into()))?;
    let shared = room.shared.read().unwrap();
    if shared.status != RoomStatus::Running {
        return Err(mlua::Error::RuntimeError(format!(
            "cloud room is {}: {}",
            shared.status, shared.error_message
        )));
    }
    Ok(())
}

fn events_to_lua(lua: &Lua, events: Vec<OutEvent>) -> LuaResult<LuaTable> {
    let result = lua.create_table()?;
    for (index, event) in events.into_iter().enumerate() {
        result.raw_set(index + 1, event_to_lua(lua, event)?)?;
    }
    Ok(result)
}

fn event_to_lua(lua: &Lua, event: OutEvent) -> LuaResult<LuaTable> {
    let value = lua.create_table()?;
    value.set("timestamp", event.timestamp)?;
    value.set("playerIndex", event.player_index)?;
    value.set("name", event.ename)?;
    value.set("data", event.evalue)?;
    Ok(value)
}

#[derive(Clone, Copy)]
enum LifecycleAction {
    Join,
    Leave,
    Exit,
}

fn install_lifecycle_method(
    lua: &Lua,
    context: &LuaTable,
    name: &str,
    manager: Arc<Mutex<RoomManager>>,
    room_id: String,
    deadline: Instant,
    action: LifecycleAction,
) -> RunnerResult<()> {
    context
        .set(
            name,
            lua.create_function(move |_, (_self, options): (LuaTable, LuaTable)| {
                let index = options.get::<i32>("index")?;
                let reason = options
                    .get::<Option<String>>("reason")?
                    .unwrap_or_else(|| "mls test".into());
                let code = {
                    let manager = manager.lock().unwrap();
                    let room = manager.get_room(&room_id).ok_or_else(|| {
                        mlua::Error::RuntimeError("cloud room no longer exists".into())
                    })?;
                    match action {
                        LifecycleAction::Join => {
                            let player_name = options
                                .get::<Option<String>>("name")?
                                .unwrap_or_else(|| format!("Player_{index}"));
                            room.join_player(index, player_name, reason)
                        }
                        LifecycleAction::Leave => room.leave_player(index, reason),
                        LifecycleAction::Exit => room.exit_player(index, reason),
                    }
                };
                let remaining = deadline.saturating_duration_since(Instant::now());
                let barrier = manager
                    .lock()
                    .unwrap()
                    .get_room(&room_id)
                    .is_some_and(|room| room.barrier(remaining));
                if !barrier {
                    return Err(mlua::Error::RuntimeError(
                        "player lifecycle command timed out".into(),
                    ));
                }
                Ok(code)
            })
            .map_err(lua_error)?,
        )
        .map_err(lua_error)
}

fn summarize(report: &mut SuiteReport, duration_ms: u128) {
    let mut summary = ReportSummary::default();
    summary.test_cases.total = report.tests.len();
    for test in &report.tests {
        match test.status.as_str() {
            "passed" => summary.test_cases.passed += 1,
            "skipped" => summary.test_cases.skipped += 1,
            _ => summary.test_cases.failed += 1,
        }
        for invocation in &test.invocations {
            summary.invocations.total += 1;
            summary.assertions.total += invocation.assertions;
            summary.assertions.failed += invocation.failures.len();
            match invocation.status.as_str() {
                "passed" => summary.invocations.passed += 1,
                "skipped" => summary.invocations.skipped += 1,
                _ => summary.invocations.failed += 1,
            }
        }
    }
    summary.duration_ms = duration_ms;
    if summary.test_cases.failed > 0 {
        report.status = "failed".into();
    }
    report.summary = summary;
}

fn error_report(
    args: &TestArgs,
    script_dir: Option<&str>,
    started: Instant,
    message: String,
) -> SuiteReport {
    SuiteReport {
        schema_version: 1,
        status: "error".into(),
        test_dir: path_text(&absolute_from_current(&args.test_dir)),
        script_dir: script_dir.unwrap_or_default().to_string(),
        tests: Vec::new(),
        summary: ReportSummary {
            duration_ms: started.elapsed().as_millis(),
            ..ReportSummary::default()
        },
        errors: vec![message],
    }
}

fn write_report(args: &TestArgs, report: &SuiteReport) -> RunnerResult<()> {
    let content = match args.reporter {
        ReporterKind::Console => render_console(report, args.list_tests),
        ReporterKind::Compact => render_compact(report, args.list_tests),
        ReporterKind::Json => serde_json::to_string_pretty(report)
            .map_err(|error| RunnerError(format!("failed to encode JSON report: {error}")))?,
    };
    if let Some(path) = &args.out {
        let path = absolute_from_current(path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(|error| {
                RunnerError(format!("failed to create report directory: {error}"))
            })?;
        }
        let mut file = fs::File::create(&path).map_err(|error| {
            RunnerError(format!(
                "failed to create report {}: {error}",
                path.display()
            ))
        })?;
        file.write_all(content.as_bytes()).map_err(|error| {
            RunnerError(format!(
                "failed to write report {}: {error}",
                path.display()
            ))
        })?;
        if !content.ends_with('\n') {
            file.write_all(b"\n").map_err(|error| {
                RunnerError(format!(
                    "failed to finish report {}: {error}",
                    path.display()
                ))
            })?;
        }
    } else {
        println!("{content}");
    }
    Ok(())
}

fn prepare_report_output(args: &TestArgs) -> RunnerResult<()> {
    let Some(path) = &args.out else {
        return Ok(());
    };
    let path = absolute_from_current(path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| {
            RunnerError(format!(
                "failed to create report directory {}: {error}",
                parent.display()
            ))
        })?;
    }
    fs::OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(&path)
        .map_err(|error| {
            RunnerError(format!(
                "failed to prepare report output {}: {error}",
                path.display()
            ))
        })?;
    Ok(())
}

fn render_console(report: &SuiteReport, list_only: bool) -> String {
    let mut lines = Vec::new();
    if !report.errors.is_empty() {
        for error in &report.errors {
            lines.push(format!("ERROR: {error}"));
        }
        return lines.join("\n");
    }
    if list_only {
        lines.push(format!("Discovered {} test case(s):", report.tests.len()));
        for test in &report.tests {
            let tags = test
                .tags
                .iter()
                .map(|tag| format!("[{tag}]"))
                .collect::<String>();
            lines.push(format!("  {} {} ({})", test.name, tags, test.id));
        }
        return lines.join("\n");
    }

    for test in &report.tests {
        for invocation in &test.invocations {
            let path = if invocation.section_path.is_empty() {
                String::new()
            } else {
                format!(" / {}", invocation.section_path.join(" / "))
            };
            lines.push(format!(
                "[{}] {}{} ({} ms)",
                invocation.status.to_uppercase(),
                test.name,
                path,
                invocation.duration_ms
            ));
            if let Some(reason) = &invocation.skip_reason {
                lines.push(format!("  skipped: {reason}"));
            }
            for failure in &invocation.failures {
                lines.push(format!(
                    "  {}:{}: {}: {}",
                    failure.source, failure.line, failure.kind, failure.message
                ));
                if let Some(actual) = &failure.actual {
                    lines.push(format!("    actual:   {actual}"));
                }
                if let Some(expected) = &failure.expected {
                    lines.push(format!("    expected: {expected}"));
                }
                for context in &failure.context {
                    lines.push(format!("    info: {context}"));
                }
            }
            for output in &invocation.output {
                lines.push(format!("  output: {output}"));
            }
            if !invocation.cloud_logs.is_empty() {
                lines.push("  cloud log tail:".into());
                for log in &invocation.cloud_logs {
                    lines.push(format!(
                        "    [{}][{}] {}",
                        log.level, log.source, log.message
                    ));
                }
            }
        }
    }
    lines.push(format!(
        "test cases: {} | {} passed | {} failed | {} skipped",
        report.summary.test_cases.total,
        report.summary.test_cases.passed,
        report.summary.test_cases.failed,
        report.summary.test_cases.skipped
    ));
    lines.push(format!(
        "invocations: {} | assertions: {} | failed assertions/errors: {} | {} ms",
        report.summary.invocations.total,
        report.summary.assertions.total,
        report.summary.assertions.failed,
        report.summary.duration_ms
    ));
    lines.join("\n")
}

fn render_compact(report: &SuiteReport, list_only: bool) -> String {
    if list_only {
        return report
            .tests
            .iter()
            .map(|test| format!("{}:{}: {} [{}]", test.source, test.line, test.name, test.id))
            .collect::<Vec<_>>()
            .join("\n");
    }
    let mut lines = Vec::new();
    for error in &report.errors {
        lines.push(format!("<runner>:0: error: {error}"));
    }
    for test in &report.tests {
        for invocation in &test.invocations {
            for failure in &invocation.failures {
                let path = if invocation.section_path.is_empty() {
                    String::new()
                } else {
                    format!(" [{}]", invocation.section_path.join(" / "))
                };
                lines.push(format!(
                    "{}:{}: {}{}: {}",
                    failure.source, failure.line, test.name, path, failure.message
                ));
            }
        }
    }
    lines.push(format!(
        "{} test case(s), {} failed; {} assertion(s), {} failed",
        report.summary.test_cases.total,
        report.summary.test_cases.failed,
        report.summary.assertions.total,
        report.summary.assertions.failed
    ));
    lines.join("\n")
}

#[cfg(test)]
mod tests {
    use super::{wildcard_match, TestClock};
    use std::time::Duration;

    #[test]
    fn wildcard_question_mark_matches_one_unicode_character() {
        assert!(wildcard_match("a?c", "abc"));
        assert!(!wildcard_match("a?c", "abbc"));
        assert!(wildcard_match("中?用例", "中文用例"));
        assert!(!wildcard_match("中??用例", "中文用例"));
        assert!(wildcard_match("*文?例", "中文用例"));
    }

    #[test]
    fn test_clock_advances_without_moving_backwards() {
        let clock = TestClock::start();
        let first = clock.now();
        std::thread::sleep(Duration::from_millis(5));
        let second = clock.now();

        assert!(first >= 0.0);
        assert!(second > first);
    }
}
