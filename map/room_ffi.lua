local ffi = require 'ffi'
local errors = require 'room_errors'

ffi.cdef [[
const char* __cdecl sr_error(void);
int __cdecl sr_open(unsigned port);
int __cdecl sr_join(unsigned handle, const char* room, unsigned room_length, int player,
    const char* token, unsigned token_length);
int __cdecl sr_send(unsigned handle, const char* id, unsigned id_length,
    const char* event, unsigned event_length, const char* payload, unsigned payload_length);
int __cdecl sr_poll(unsigned handle, const char** data, unsigned* length);
void __cdecl sr_close(unsigned handle);
]]

local M = {}

local function check(library, result)
    if result < 0 then
        errors.fail('native', ffi.string(library.sr_error()))
    end
end

---@class FfiConnection : NativeConnection
---@field _library table
---@field _owner userdata
---@field _data userdata
---@field _length userdata
local Connection = {}
Connection.__index = Connection

local function new_connection(library, handle)
    local owner = ffi.gc(
        ffi.new('unsigned[1]', handle),
        function(value)
            library.sr_close(value[0])
        end
    )
    local o = {
        _library = library,
        _owner = owner,
        _data = ffi.new('const char*[1]'),
        _length = ffi.new('unsigned[1]'),
    }
    setmetatable(o, Connection)
    return o
end

function Connection:join(room, player, token)
    local library = self._library
    local result = library.sr_join(self._owner[0], room, #room, player, token, #token)
    check(library, result)
end

function Connection:send(id, event, payload)
    local library = self._library
    local result = library.sr_send(self._owner[0], id, #id, event, #event, payload, #payload)
    check(library, result)
end

function Connection:poll()
    local library = self._library
    local result = library.sr_poll(self._owner[0], self._data, self._length)
    check(library, result)
    if result == 1 then
        return ffi.string(self._data[0], self._length[0])
    end
end

function Connection:close()
    self._library.sr_close(self._owner[0])
    self._owner[0] = 0
end

---@param path string
---@return NativeRoom
function M.load(path)
    local library = ffi.load(path)
    local native = {}

    function native.open(port)
        local handle = library.sr_open(port)
        check(library, handle)
        return new_connection(library, handle)
    end

    return native
end

return M
