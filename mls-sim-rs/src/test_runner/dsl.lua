local getinfo = debug.getinfo
debug = nil

local cases = {}
local aborted = false
local fatal_marker = '__MLS_TEST_FATAL__'
local skip_marker = '__MLS_TEST_SKIP__'

local function callsite(level)
    local info = getinfo(level + 1, 'Sl') or {}
    local source = info.source or '<unknown>'
    if source:sub(1, 1) == '@' then
        source = source:sub(2)
    end
    return source, info.currentline or 0
end

local function parse_case(name, tags_or_body, body)
    if type(name) ~= 'string' or name == '' then
        error('TEST_CASE name must be a non-empty string', 3)
    end
    if type(tags_or_body) == 'function' and body == nil then
        return '', tags_or_body
    end
    if type(tags_or_body) ~= 'string' or type(body) ~= 'function' then
        error('TEST_CASE expects (name, body) or (name, tags, body)', 3)
    end
    return tags_or_body, body
end

function TEST_CASE(name, tags_or_body, body)
    local tags, callback = parse_case(name, tags_or_body, body)
    local source, line = callsite(2)
    local index = __mls_test_register(name, tags, source, line)
    cases[index] = callback
end

local function stable_key(value)
    local kind = type(value)
    if kind == 'number' then
        return '1:' .. string.format('%.17g', value)
    end
    if kind == 'string' then
        return '2:' .. value
    end
    if kind == 'boolean' then
        return value and '3:1' or '3:0'
    end
    return '9:' .. kind .. ':' .. tostring(value)
end

local function stable_format(value, active)
    local kind = type(value)
    if kind == 'string' then
        return string.format('%q', value)
    end
    if kind ~= 'table' then
        return tostring(value)
    end
    active = active or {}
    if active[value] then
        return '<cycle>'
    end
    active[value] = true
    local keys = {}
    for key in pairs(value) do
        keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b)
        return stable_key(a) < stable_key(b)
    end)
    local parts = {}
    for _, key in ipairs(keys) do
        parts[#parts + 1] = '[' .. stable_format(key, active) .. ']=' .. stable_format(value[key], active)
    end
    active[value] = nil
    return '{' .. table.concat(parts, ', ') .. '}'
end

local function deep_equal(left, right, seen)
    if rawequal(left, right) then
        return true
    end
    if type(left) ~= type(right) or type(left) ~= 'table' then
        return false
    end
    seen = seen or {}
    local right_seen = seen[left]
    if right_seen and right_seen[right] then
        return true
    end
    if not right_seen then
        right_seen = {}
        seen[left] = right_seen
    end
    right_seen[right] = true
    for key, value in pairs(left) do
        if not deep_equal(value, right[key], seen) then
            return false
        end
    end
    for key in pairs(right) do
        if left[key] == nil then
            return false
        end
    end
    return true
end

local function record(kind, passed, actual, expected, message, fatal, level)
    if aborted then
        return
    end
    local source, line = callsite(level + 1)
    local actualText
    if actual ~= nil then
        actualText = stable_format(actual)
    end
    local expectedText
    if expected ~= nil then
        expectedText = stable_format(expected)
    end
    local messageText
    if message ~= nil then
        messageText = tostring(message)
    end
    __mls_test_assertion(
        kind,
        passed,
        actualText,
        expectedText,
        messageText,
        source,
        line
    )
    if fatal and not passed then
        error(fatal_marker, 0)
    end
end

function CHECK(value, message)
    record('CHECK', not not value, value, true, message, false, 2)
end

function REQUIRE(value, message)
    record('REQUIRE', not not value, value, true, message, true, 2)
end

function CHECK_EQ(actual, expected, message)
    record('CHECK_EQ', deep_equal(actual, expected), actual, expected, message, false, 2)
end

function REQUIRE_EQ(actual, expected, message)
    record('REQUIRE_EQ', deep_equal(actual, expected), actual, expected, message, true, 2)
end

function CHECK_NE(actual, expected, message)
    record('CHECK_NE', not deep_equal(actual, expected), actual, expected, message, false, 2)
end

function REQUIRE_NE(actual, expected, message)
    record('REQUIRE_NE', not deep_equal(actual, expected), actual, expected, message, true, 2)
end

local function near(actual, expected, margin)
    return type(actual) == 'number'
        and type(expected) == 'number'
        and type(margin) == 'number'
        and margin >= 0
        and math.abs(actual - expected) <= margin
end

function CHECK_NEAR(actual, expected, margin, message)
    record('CHECK_NEAR', near(actual, expected, margin), actual, expected, message, false, 2)
end

function REQUIRE_NEAR(actual, expected, margin, message)
    record('REQUIRE_NEAR', near(actual, expected, margin), actual, expected, message, true, 2)
end

local function check_call(kind, callback, expect_error, fatal)
    if type(callback) ~= 'function' then
        error(kind .. ' expects a function', 3)
    end
    local success, err = pcall(callback)
    local passed = expect_error and not success or not expect_error and success
    local actual = success and 'no error' or tostring(err)
    local expected = expect_error and 'an error' or 'no error'
    record(kind, passed, actual, expected, nil, fatal, 3)
end

function CHECK_THROWS(callback) check_call('CHECK_THROWS', callback, true, false) end
function REQUIRE_THROWS(callback) check_call('REQUIRE_THROWS', callback, true, true) end
function CHECK_NOTHROW(callback) check_call('CHECK_NOTHROW', callback, false, false) end
function REQUIRE_NOTHROW(callback) check_call('REQUIRE_NOTHROW', callback, false, true) end

function FAIL_CHECK(message)
    record('FAIL_CHECK', false, nil, nil, message or 'explicit failure', false, 2)
end

function FAIL(message)
    record('FAIL', false, nil, nil, message or 'explicit failure', true, 2)
end

function SUCCEED(message)
    record('SUCCEED', true, nil, nil, message, false, 2)
end

function SKIP(message)
    local source, line = callsite(2)
    __mls_test_skip(message == nil and nil or tostring(message), source, line)
    error(skip_marker, 0)
end

function INFO(...)
    local count = select('#', ...)
    local values = {}
    for index = 1, count do
        values[index] = stable_format(select(index, ...))
    end
    __mls_test_info(table.concat(values, '\t'))
end

function CAPTURE(name, value)
    __mls_test_info(tostring(name) .. ' = ' .. stable_format(value))
end

function SECTION(name, callback)
    if type(name) ~= 'string' or name == '' or type(callback) ~= 'function' then
        error('SECTION expects a non-empty name and function', 2)
    end
    local source, line = callsite(2)
    local entered = __mls_test_section_enter(name, source, line, aborted)
    if not entered then
        return
    end
    local success, err = pcall(callback)
    __mls_test_section_leave()
    if not success then
        local text = tostring(err)
        if not text:find(fatal_marker, 1, true) and not text:find(skip_marker, 1, true) then
            __mls_test_error(text, source, line)
        end
        aborted = true
    end
end

function __mls_test_get_case(index)
    return cases[index]
end

function __mls_test_run_case(index, context)
    aborted = false
    local callback = cases[index]
    if type(callback) ~= 'function' then
        error('registered test callback not found: ' .. tostring(index), 0)
    end
    return callback(context)
end
