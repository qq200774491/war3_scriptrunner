---@meta

-- 仅覆盖示例使用的云脚本 API。声明供编辑器读取，不能加载进测试 VM。
---@param name string
---@param callback fun(eventId:integer, eventName:string, eventData:string, playerIndex:integer)
---@return integer
function RegisterEvent(name, callback) end

---@param playerIndex integer
---@param name string
---@param data string
---@return integer
function MsSendMlEvent(playerIndex, name, data) end

---@param playerIndex integer
---@return string
function MsGetScriptArchive(playerIndex) end

---@param playerIndex integer
---@param data string
---@return integer
function MsSaveScriptArchive(playerIndex, data) end

---@class ExampleGaReply
---@field errnu integer
---@field op string
---@field trans_id integer
---@field data table

---@param key string
---@param values table<string, string>
---@param callback fun(result:ExampleGaReply)
function MsGAHmSet(key, values, callback) end

---@param key string
---@param callback fun(result:ExampleGaReply)
function MsGAHGetAll(key, callback) end
