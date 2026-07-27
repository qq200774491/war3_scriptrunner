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
