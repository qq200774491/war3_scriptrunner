use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::time::{Duration, Instant};
use tempfile::TempDir;

fn simulator() -> Command {
    Command::new(env!("CARGO_BIN_EXE_mls-sim"))
}

fn example_tests() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("examples")
        .join("cloud-tests")
        .join("tests")
}

fn run_json(arguments: &[&str]) -> (Output, Value) {
    let output = simulator().args(arguments).output().unwrap();
    let json = serde_json::from_slice(&output.stdout).unwrap_or_else(|error| {
        panic!(
            "stdout was not a single JSON document: {error}\n{}",
            String::from_utf8_lossy(&output.stdout)
        )
    });
    (output, json)
}

struct TestProject {
    _root: TempDir,
    tests: PathBuf,
}

impl TestProject {
    fn new(test_code: &str) -> Self {
        Self::with_main("-- test cloud\n", test_code)
    }

    fn with_main(main_code: &str, test_code: &str) -> Self {
        let root = tempfile::tempdir().unwrap();
        let script = root.path().join("script");
        let tests = root.path().join("tests");
        fs::create_dir_all(&script).unwrap();
        fs::create_dir_all(&tests).unwrap();
        fs::write(script.join("main.lua"), main_code).unwrap();
        fs::write(tests.join("runner_test.lua"), test_code).unwrap();
        fs::write(
            tests.join("mls-test.json"),
            r#"{
  "script_dir": "../script",
  "timeouts": {
    "discovery": 1.0,
    "startup": 1.0,
    "case": 1.0,
    "event": 0.1,
    "shutdown": 1.0
  }
}"#,
        )
        .unwrap();
        Self { _root: root, tests }
    }
}

#[test]
fn jobs_zero_is_a_cli_usage_error() {
    let project = TestProject::new("TEST_CASE('unused', function() end)\n");
    let output = simulator()
        .args(["test", project.tests.to_str().unwrap(), "--jobs", "0"])
        .output()
        .unwrap();

    assert_eq!(output.status.code(), Some(2));
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(stderr.contains("--jobs"), "{stderr}");
    assert!(stderr.contains('0'), "{stderr}");
}

#[test]
fn unicode_and_ascii_question_mark_filters_match_one_character() {
    let project = TestProject::new(
        r#"
TEST_CASE('中文用例', '[filter]', function() CHECK(true) end)
TEST_CASE('abc', '[filter]', function() CHECK(true) end)
TEST_CASE('abbc', '[filter]', function() FAIL('should not match') end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
        "--test",
        "中?用例",
        "--test",
        "a?c",
    ]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["summary"]["test_cases"]["total"], 2);
    assert_eq!(report["tests"][0]["name"], "中文用例");
    assert_eq!(report["tests"][1]["name"], "abc");
}

#[test]
fn invalid_output_path_is_rejected_before_cloud_startup() {
    let project = TestProject::with_main(
        "error('cloud startup must not run')\n",
        "TEST_CASE('unused', function() FAIL('test must not run') end)\n",
    );
    let output = simulator()
        .args([
            "test",
            project.tests.to_str().unwrap(),
            "--reporter",
            "json",
            "--out",
            project.tests.to_str().unwrap(),
        ])
        .output()
        .unwrap();

    assert_eq!(output.status.code(), Some(2));
    assert!(output.stdout.is_empty());
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("failed to prepare report output"),
        "{stderr}"
    );
    assert!(!stderr.contains("cloud startup must not run"), "{stderr}");
}

#[test]
fn valid_output_path_is_prepared_and_receives_the_report() {
    let project = TestProject::new("TEST_CASE('written report', function() CHECK(true) end)\n");
    let output_path = project.tests.join("reports").join("result.json");
    let output = simulator()
        .args([
            "test",
            project.tests.to_str().unwrap(),
            "--reporter",
            "json",
            "--out",
            output_path.to_str().unwrap(),
        ])
        .output()
        .unwrap();

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(output.stdout.is_empty());
    let report: Value = serde_json::from_slice(&fs::read(output_path).unwrap()).unwrap();
    assert_eq!(report["status"], "passed");
    assert_eq!(report["summary"]["test_cases"]["passed"], 1);
}

#[test]
fn script_directory_is_found_across_all_test_directory_ancestors() {
    let root = tempfile::tempdir().unwrap();
    let tests = root.path().join("one").join("two").join("tests");
    fs::create_dir_all(&tests).unwrap();
    fs::write(root.path().join("main.lua"), "-- ancestor cloud\n").unwrap();
    fs::write(
        tests.join("ancestor_test.lua"),
        "TEST_CASE('ancestor main', function() CHECK(true) end)\n",
    )
    .unwrap();

    let (output, report) = run_json(&["test", tests.to_str().unwrap(), "--reporter", "json"]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["status"], "passed");
    assert_eq!(
        Path::new(report["script_dir"].as_str().unwrap())
            .canonicalize()
            .unwrap(),
        root.path().canonicalize().unwrap()
    );
}

#[test]
fn test_context_delays_all_server_responses_without_reordering_them() {
    let project = TestProject::with_main(
        r#"
RegisterEvent('delayed_req', function(_, _, eventData, playerIndex)
    MsSendMlEvent(playerIndex, 'delayed_res', eventData .. ':first')
    MsSendMlEvent(playerIndex, 'delayed_res', eventData .. ':second')
end)
"#,
        r#"
TEST_CASE('server response delay', '[runner][delay]', function(ctx)
    local client = ctx:client(0)
    ctx:setServerResponseDelay(0.1)

    REQUIRE_EQ(client:send('delayed_req', 'value'), 0)
    CHECK(client:waitEvent({name = 'delayed_res', timeout = 0.03}) == nil)

    local first = client:waitEvent({name = 'delayed_res', timeout = 0.3})
    local second = client:waitEvent({name = 'delayed_res', timeout = 0.3})
    REQUIRE(first)
    REQUIRE(second)
    CHECK_EQ(first.data, 'value:first')
    CHECK_EQ(second.data, 'value:second')

    CHECK_THROWS(function() ctx:setServerResponseDelay(-0.1) end)
    CHECK_THROWS(function() ctx:setServerResponseDelay(math.huge) end)
    CHECK_THROWS(function() ctx:setServerResponseDelay(0 / 0) end)
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["status"], "passed");
    assert_eq!(report["summary"]["test_cases"]["passed"], 1);
    assert_eq!(report["summary"]["assertions"]["failed"], 0);
}

#[test]
fn test_client_exposes_a_monotonic_clock() {
    let project = TestProject::new(
        r#"
TEST_CASE('monotonic clock', '[runner][time]', function(ctx)
    local client = ctx:client(0)
    local started = client:now()
    CHECK(started >= 0)
    CHECK_EQ(client.eventTimeout, 0.1)
    CHECK(client:waitEvent({name = 'never', timeout = 0.02}) == nil)
    CHECK(client:now() > started)
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["status"], "passed");
    assert_eq!(report["summary"]["test_cases"]["passed"], 1);
    assert_eq!(report["summary"]["assertions"]["failed"], 0);
}

#[test]
fn parallel_cases_keep_discovery_order_and_isolated_cloud_state() {
    let project = TestProject::with_main(
        r#"
local counter = 0

RegisterEvent('isolation_req', function(_, _, eventData, playerIndex)
    counter = counter + 1
    MsSendMlEvent(playerIndex, 'isolation_res', tostring(counter) .. ':' .. eventData)
end)
"#,
        r#"
local function check_isolated(ctx, value)
    local client = ctx:client(0)
    REQUIRE_EQ(client:send('isolation_req', value), 0)
    local response = client:waitEvent({name = 'isolation_res'})
    REQUIRE(response)
    CHECK_EQ(response.data, '1:' .. value)
end

TEST_CASE('parallel first', '[parallel]', function(ctx)
    print('captured output from parallel first')
    local client = ctx:client(0)
    CHECK(client:waitEvent({name = 'never', timeout = 0.05}) == nil)
    check_isolated(ctx, 'first')
end)

TEST_CASE('parallel second', '[parallel]', function(ctx)
    check_isolated(ctx, 'second')
end)

TEST_CASE('parallel third', '[parallel]', function(ctx)
    check_isolated(ctx, 'third')
end)

TEST_CASE('parallel fourth', '[parallel]', function(ctx)
    check_isolated(ctx, 'fourth')
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
        "--jobs",
        "4",
    ]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["status"], "passed");
    assert_eq!(report["summary"]["test_cases"]["total"], 4);
    assert_eq!(report["summary"]["test_cases"]["passed"], 4);
    let names = report["tests"]
        .as_array()
        .unwrap()
        .iter()
        .map(|test| test["name"].as_str().unwrap())
        .collect::<Vec<_>>();
    assert_eq!(
        names,
        [
            "parallel first",
            "parallel second",
            "parallel third",
            "parallel fourth"
        ]
    );
}

#[test]
fn fail_fast_with_parallel_jobs_reports_through_first_failure_only() {
    let project = TestProject::new(
        r#"
TEST_CASE('before failure', '[parallel]', function()
    CHECK(true)
end)

TEST_CASE('first failure', '[parallel]', function()
    REQUIRE(false, 'intentional failure')
end)

TEST_CASE('must not run', '[parallel]', function()
    FAIL('fail-fast scheduled a later test')
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
        "--jobs",
        "4",
        "--fail-fast",
    ]);

    assert_eq!(output.status.code(), Some(1));
    assert_eq!(report["status"], "failed");
    assert_eq!(report["summary"]["test_cases"]["total"], 2);
    assert_eq!(report["summary"]["test_cases"]["passed"], 1);
    assert_eq!(report["summary"]["test_cases"]["failed"], 1);
    let names = report["tests"]
        .as_array()
        .unwrap()
        .iter()
        .map(|test| test["name"].as_str().unwrap())
        .collect::<Vec<_>>();
    assert_eq!(names, ["before failure", "first failure"]);
}

#[test]
fn json_report_is_clean_and_filters_example_tests() {
    let tests = example_tests();
    let (output, report) = run_json(&[
        "test",
        tests.to_str().unwrap(),
        "--reporter",
        "json",
        "--test",
        "*round trip",
    ]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["schema_version"], 1);
    assert_eq!(report["status"], "passed");
    assert_eq!(report["summary"]["test_cases"]["total"], 1);
    assert_eq!(report["tests"][0]["name"], "raw event round trip");
}

#[test]
fn check_failure_continues_and_returns_test_failure_exit_code() {
    let project = TestProject::new(
        r#"
TEST_CASE('non-fatal assertion', '[runner]', function()
    CHECK(false, 'first failure')
    CHECK_EQ(2 + 2, 4)
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert_eq!(output.status.code(), Some(1));
    assert_eq!(report["status"], "failed");
    assert_eq!(report["summary"]["assertions"]["total"], 2);
    assert_eq!(report["summary"]["assertions"]["failed"], 1);
    assert_eq!(
        report["tests"][0]["invocations"][0]["failures"][0]["message"],
        "first failure"
    );
}

#[test]
fn test_lua_infinite_loop_is_interrupted() {
    let project = TestProject::new(
        r#"
TEST_CASE('timeout', '[runner]', function()
    while true do end
end)
"#,
    );
    let start = Instant::now();
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
        "--case-timeout",
        "0.05",
    ]);

    assert_eq!(output.status.code(), Some(1));
    assert_eq!(report["status"], "failed");
    assert!(start.elapsed() < Duration::from_secs(3));
    assert!(
        report["tests"][0]["invocations"][0]["failures"][0]["message"]
            .as_str()
            .unwrap()
            .contains("deadline")
    );
}

#[test]
fn section_example_runs_both_isolated_paths() {
    let tests = example_tests();
    let (output, report) = run_json(&[
        "test",
        tests.to_str().unwrap(),
        "--reporter",
        "json",
        "--tag",
        "section",
    ]);

    assert!(output.status.success());
    assert_eq!(report["summary"]["test_cases"]["total"], 1);
    assert_eq!(report["summary"]["invocations"]["total"], 2);
    assert_eq!(
        report["tests"][0]["invocations"][0]["section_path"][0],
        "first path"
    );
    assert_eq!(
        report["tests"][0]["invocations"][1]["section_path"][0],
        "second path"
    );
}

#[test]
fn fatal_section_still_discovers_and_runs_its_sibling() {
    let project = TestProject::new(
        r#"
TEST_CASE('fatal section', '[runner]', function()
    SECTION('failing path', function()
        REQUIRE(false, 'stop this path')
    end)

    SECTION('sibling path', function()
        CHECK(true)
    end)
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert_eq!(output.status.code(), Some(1));
    assert_eq!(report["summary"]["invocations"]["total"], 2);
    assert_eq!(report["tests"][0]["invocations"][0]["status"], "failed");
    assert_eq!(report["tests"][0]["invocations"][1]["status"], "passed");
}

#[test]
fn nested_sections_run_each_leaf_path() {
    let project = TestProject::new(
        r#"
TEST_CASE('nested sections', '[runner]', function()
    SECTION('outer', function()
        SECTION('first leaf', function() CHECK(true) end)
        SECTION('second leaf', function() CHECK(true) end)
    end)
    SECTION('third leaf', function() CHECK(true) end)
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert!(output.status.success());
    assert_eq!(report["summary"]["invocations"]["total"], 3);
    assert_eq!(report["summary"]["assertions"]["total"], 3);
}

#[test]
fn section_leaf_paths_have_independent_cloud_globals_and_archives() {
    let project = TestProject::with_main(
        r#"
local counter = 0

RegisterEvent('section_isolation_req', function(_, _, eventData, playerIndex)
    counter = counter + 1
    local previous = MsGetCommonArchive(playerIndex, 'section_state')
    local result = MsSetCommonArchive(playerIndex, 'section_state', eventData)
    MsSendMlEvent(
        playerIndex,
        'section_isolation_res',
        tostring(counter) .. ':' .. previous .. ':' .. tostring(result)
    )
end)
"#,
        r#"
local function check_isolated_path(ctx, label)
    local client = ctx:client(0)

    REQUIRE_EQ(client:send('section_isolation_req', label), 0)
    local first = client:waitEvent({name = 'section_isolation_res'})
    REQUIRE(first)
    CHECK_EQ(first.data, '1::0')

    REQUIRE_EQ(client:send('section_isolation_req', label .. '-next'), 0)
    local second = client:waitEvent({name = 'section_isolation_res'})
    REQUIRE(second)
    CHECK_EQ(second.data, '2:' .. label .. ':0')
end

TEST_CASE('section cloud and archive isolation', '[runner][section]', function(ctx)
    SECTION('first leaf', function()
        check_isolated_path(ctx, 'first')
    end)
    SECTION('second leaf', function()
        check_isolated_path(ctx, 'second')
    end)
end)
"#,
    );
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(report["summary"]["test_cases"]["passed"], 1);
    assert_eq!(report["summary"]["invocations"]["total"], 2);
    assert_eq!(report["summary"]["invocations"]["passed"], 2);
    assert_eq!(report["summary"]["assertions"]["failed"], 0);
}

#[test]
fn unknown_nested_manifest_field_is_a_preflight_error() {
    let project = TestProject::new("TEST_CASE('unused', function() end)\n");
    fs::write(
        project.tests.join("mls-test.json"),
        r#"{
  "script_dir": "../script",
  "room": {
    "players": [{"index": 0, "typo_field": true}]
  }
}"#,
    )
    .unwrap();
    let (output, report) = run_json(&[
        "test",
        project.tests.to_str().unwrap(),
        "--reporter",
        "json",
    ]);

    assert_eq!(output.status.code(), Some(2));
    assert_eq!(report["status"], "error");
    assert!(report["errors"][0].as_str().unwrap().contains("typo_field"));
}
