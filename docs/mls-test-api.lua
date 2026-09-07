---@meta

---@class MlsTestEvent
---@field timestamp number
---@field playerIndex integer
---@field name string
---@field data string

---@class MlsTestWaitOptions
---@field name? string
---@field timeout? number

---@class MlsTestLifecycleOptions
---@field index integer
---@field name? string
---@field reason? string

---@class MlsTestClient
---@field eventTimeout number Effective default waitEvent timeout in seconds.
local MlsTestClient = {}

---Return monotonic seconds elapsed in the current test invocation.
---@return number seconds
function MlsTestClient:now() end

---@param eventName string
---@param eventData string
---@return integer errnum
function MlsTestClient:send(eventName, eventData) end

---@return MlsTestEvent[]
function MlsTestClient:poll() end

---@param options? MlsTestWaitOptions
---@return MlsTestEvent?
function MlsTestClient:waitEvent(options) end

---@return integer removedCount
function MlsTestClient:clearEvents() end

---@class MlsTestContext
local MlsTestContext = {}

---@param playerIndex integer
---@return MlsTestClient
function MlsTestContext:client(playerIndex) end

---Set a non-blocking delay for every subsequent MsSendMlEvent response in this room.
---Responses remain in their original send order. The default is 0.
---@param seconds number Non-negative finite seconds.
function MlsTestContext:setServerResponseDelay(seconds) end

---@param options MlsTestLifecycleOptions
---@return integer errnum
function MlsTestContext:playerJoin(options) end

---@param options MlsTestLifecycleOptions
---@return integer errnum
function MlsTestContext:playerLeave(options) end

---@param options MlsTestLifecycleOptions
---@return integer errnum
function MlsTestContext:playerExit(options) end

---Read a global archive KV entry directly from the store (nil when absent).
---@param key string
---@return string? value
function MlsTestContext:gaGet(key) end

---Write a global archive KV entry directly; tables are JSON-encoded.
---@param key string
---@param value string|integer|table
---@return boolean accepted
function MlsTestContext:gaSet(key, value) end

---@param key string
---@return integer removed
function MlsTestContext:gaDel(key) end

---@param key string
---@return table<string, string> fields
function MlsTestContext:gaHGetAll(key) end

---@param key string
---@param kvs table<string, string|integer|table>
---@return boolean accepted
function MlsTestContext:gaHmSet(key, kvs) end

---@param key string
---@param fields string[]
---@return integer removed
function MlsTestContext:gaHDel(key, fields) end

---Hold a lock as if another room acquired it.
---@param key string
---@param ttlMs integer
---@return boolean acquired
function MlsTestContext:gaLock(key, ttlMs) end

---@param key string
function MlsTestContext:gaUnlock(key) end

---Broadcast to every room of this test as if another room sent it; tables are JSON-encoded.
---@param key string
---@param value string|table
---@return boolean sent
function MlsTestContext:gaBroadcast(key, value) end

---Delay every subsequent _msga_reply by the given seconds (0 restores immediate delivery).
---@param seconds number
function MlsTestContext:setGaReplyDelay(seconds) end

---@class MlsTestGaFault
---@field op? string Platform op name such as 'MsGAGet'; nil matches every op.
---@field errnu? integer Non-zero errnu to inject, default 2.
---@field count? integer Number of calls to fail; nil keeps failing until cleared.

---Inject platform failures into subsequent MsGA* calls; pass nil to clear.
---@param rule MlsTestGaFault?
function MlsTestContext:setGaFault(rule) end

---@return {kv: table<string, string>, hashes: table<string, table<string, string>>, locks: table<string, string>}
function MlsTestContext:gaDump() end

---@alias MlsTestBody fun(context:MlsTestContext)

---@param name string
---@param body MlsTestBody
---@overload fun(name:string, tags:string, body:MlsTestBody)
function TEST_CASE(name, body) end

---@param name string
---@param body fun()
function SECTION(name, body) end

---@param value any
---@param message? string
function CHECK(value, message) end

---@param value any
---@param message? string
function REQUIRE(value, message) end

---@param actual any
---@param expected any
---@param message? string
function CHECK_EQ(actual, expected, message) end

---@param actual any
---@param expected any
---@param message? string
function REQUIRE_EQ(actual, expected, message) end

---@param actual any
---@param expected any
---@param message? string
function CHECK_NE(actual, expected, message) end

---@param actual any
---@param expected any
---@param message? string
function REQUIRE_NE(actual, expected, message) end

---@param actual number
---@param expected number
---@param margin number
---@param message? string
function CHECK_NEAR(actual, expected, margin, message) end

---@param actual number
---@param expected number
---@param margin number
---@param message? string
function REQUIRE_NEAR(actual, expected, margin, message) end

---@param body fun()
function CHECK_THROWS(body) end

---@param body fun()
function REQUIRE_THROWS(body) end

---@param body fun()
function CHECK_NOTHROW(body) end

---@param body fun()
function REQUIRE_NOTHROW(body) end

---@param message? string
function FAIL_CHECK(message) end

---@param message? string
function FAIL(message) end

---@param message? string
function SKIP(message) end

---@param message? string
function SUCCEED(message) end

---@param ... any
function INFO(...) end

---@param name string
---@param value any
function CAPTURE(name, value) end
