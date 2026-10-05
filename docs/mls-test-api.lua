---@meta

-- 测试 VM 内置工具；由宿主提供，不需要 require，也不要执行本声明文件。
json = {}

---@param value any
---@return string
function json.encode(value) end

---JSON null、nil 和空字符串解码为 nil；非法 JSON 抛出错误。
---@param text string?
---@return any
function json.decode(text) end

---@param value any
---@return string hex
function md5(value) end

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
---仅表示平台接收结果，不代表业务成功；名称最多32字节，数据最多900字节。
function MlsTestClient:send(eventName, eventData) end

---@return MlsTestEvent[]
---取出并清空当前已到达的所有事件。
function MlsTestClient:poll() end

---@param options? MlsTestWaitOptions
---@return MlsTestEvent?
---等待并消费匹配事件；保留不匹配事件，超时返回nil。
function MlsTestClient:waitEvent(options) end

---@return integer removedCount
function MlsTestClient:clearEvents() end

---@class MlsTestContext
---@field roomId string 当前用例内房间编号
local MlsTestContext = {}

---@class MlsTestRoomOptions
---@field players? table[] 完整替换新房间玩家配置；其余配置继承当前用例清单

---在同一用例中新建独立云VM，共享GA；返回完整房间上下文。
---@param options? MlsTestRoomOptions
---@return MlsTestContext
function MlsTestContext:createRoom(options) end

---关闭本房间；重复关闭返回false，其他房间继续运行。
---@return boolean closed
function MlsTestContext:close() end

---暂扣本房间接下来count个指定操作回包；存储操作仍然立即执行。
---@param op string
---@param count? integer
function MlsTestContext:holdGaReplies(op, count) end

---@return {held:integer,armed:boolean,calls:table<string,integer>}
function MlsTestContext:gaReplyState() end

---解除闸门并投递暂扣结果，不重做存储操作。
---@return integer count
function MlsTestContext:releaseGaReplies() end

---解除闸门并丢弃暂扣结果，模拟已写入但回包丢失。
---@return integer count
function MlsTestContext:dropGaReplies() end

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
---Shared by all rooms in this invocation; does not delay storage execution.
---@param seconds number
function MlsTestContext:setGaReplyDelay(seconds) end

---@class MlsTestGaFault
---@field op? string Platform op name such as 'MsGAGet'; nil matches every op.
---@field errnu? integer Non-zero errnu to inject, default 2.
---@field count? integer Number of calls to fail; nil keeps failing until cleared.

---Inject platform failures into subsequent MsGA* calls; pass nil to clear.
---Shared by all rooms in this invocation; rejects operations before storage changes.
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
