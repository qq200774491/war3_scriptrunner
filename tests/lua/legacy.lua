local callbacks, sent, registered = nil, {}, {}
local native = {}
local native_request_count = 0
local notices = 0
local cj = {
    GetPlayerId = function(player) return player end,
    GetLocalPlayer = function() return 0 end,
    Player = function(player) return player end,
    TriggerExecute = function(trigger) trigger() end,
    DisplayTimedTextToPlayer = function() notices = notices + 1 end,
}
local japi = {
    DzTriggerRegisterSyncData = function(trigger, name, server)
        registered[#registered + 1] = {trigger, name, server}
    end,
    DzGetTriggerSyncData = function() return 'native data' end,
    DzGetTriggerSyncPlayer = function() return 9 end,
    RequestExtraBooleanData = function()
        native_request_count = native_request_count + 1
        return false
    end,
}
package.loaded['jass.common'] = cj
package.loaded['jass.japi'] = japi
package.loaded.room_ffi = {load = function() return native end}
package.loaded.shared_room = {
    start = function(_, handlers, injected)
        callbacks = handlers
        assert(injected.native == native and injected.common == cj)
        return {send = function(...) sent[#sent + 1] = {...} end}
    end,
}
require('legacy_mls').install(profile)
local received = {}
japi.DzTriggerRegisterSyncData(function()
    received[#received + 1] = {japi.DzGetTriggerSyncData(), japi.DzGetTriggerSyncPlayer()}
end, 'war3_data', true)
japi.DzTriggerRegisterSyncData(function() end, 'unrelated', true)
assert(#registered == 1 and registered[1][2] == 'unrelated')
assert(japi.RequestExtraBooleanData(93, 0, '', '') == false)
assert(native_request_count == 1)
assert(japi.RequestExtraBooleanData(1009, 1, 'war3_data', 'foreign') == false)
assert(#sent == 0)
assert(japi.RequestExtraBooleanData(1009, 0, 'war3_data', 'own'))
assert(#sent == 1 and sent[1][1] == 0 and sent[1][4] == 'own')
callbacks.on_event('war3_data', string.char(0xe4, 0xb8), 1)
assert(#received == 1 and received[1][1] == string.char(0xe4, 0xb8) and received[1][2] == 1)
assert(japi.DzGetTriggerSyncData() == 'native data')
assert(japi.DzGetTriggerSyncPlayer() == 9)
japi.DzTriggerRegisterSyncData(function() error('business failure') end, 'war3_data_page', true)
assert(not pcall(callbacks.on_event, 'war3_data_page', 'bad', 1))
assert(japi.DzGetTriggerSyncData() == 'native data')
cj.DisplayTimedTextToPlayer = function() error('map replaced display native') end
callbacks.on_local_status('room_failed', {reason='host stopped'})
callbacks.on_failure('host stopped')
assert(notices == 1)
print('LEGACY_OK owner filtering, synchronized target delivery, context restoration')
