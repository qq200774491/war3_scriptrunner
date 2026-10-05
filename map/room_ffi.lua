local ffi = require 'ffi'
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

function M.load(path)
    local library = ffi.load(path)
    local function checked(result)
        if result < 0 then
            error(ffi.string(library.sr_error()))
        end
        return result
    end
    local native = {}
    function native.open(port)
        local handle = checked(library.sr_open(port))
        local owner = ffi.gc(ffi.new('unsigned[1]', handle), function(value)
            library.sr_close(value[0])
        end)
        local data = ffi.new('const char*[1]')
        local length = ffi.new('unsigned[1]')
        local object = {}
        function object:join(room, player, token)
            checked(library.sr_join(owner[0], room, #room, player, token, #token))
        end
        function object:send(id, event, payload)
            checked(library.sr_send(owner[0], id, #id, event, #event, payload, #payload))
        end
        function object:poll()
            if checked(library.sr_poll(owner[0], data, length)) == 1 then
                return ffi.string(data[0], length[0])
            end
        end
        function object:close()
            library.sr_close(owner[0])
            owner[0] = 0
        end
        return object
    end
    return native
end

return M
