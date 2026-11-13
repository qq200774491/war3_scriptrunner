---@diagnostic disable: undefined-global, missing-fields, redundant-parameter
local check = require 'check'

local callbacks
local sent = {}
local registered = {}
local native = {}
local native_request_count = 0
local notices = 0

local cj = {
    GetPlayerId = function(player)
        return player
    end,
    GetLocalPlayer = function()
        return 0
    end,
    Player = function(player)
        return player
    end,
    TriggerExecute = function(trigger)
        trigger()
    end,
    DisplayTimedTextToPlayer = function()
        notices = notices + 1
    end,
}
local japi = {
    DzTriggerRegisterSyncData = function(trigger, name, server)
        registered[#registered + 1] = {trigger, name, server}
    end,
    DzGetTriggerSyncData = function()
        return 'native data'
    end,
    DzGetTriggerSyncPlayer = function()
        return 9
    end,
    RequestExtraBooleanData = function()
        native_request_count = native_request_count + 1
        return false
    end,
}

package.loaded['jass.common'] = cj
package.loaded['jass.japi'] = japi
package.loaded.room_ffi = {
    load = function()
        return native
    end,
}
package.loaded.shared_room = {
    start = function(_, handlers, injected)
        callbacks = handlers
        check.equal(injected.native, native)
        check.equal(injected.common, cj)
        return {
            send = function(...)
                sent[#sent + 1] = {...}
            end,
        }
    end,
}

local legacy_mls = require 'legacy_mls'
legacy_mls.install(profile)

local received = {}
japi.DzTriggerRegisterSyncData(
    function()
        received[#received + 1] = {japi.DzGetTriggerSyncData(), japi.DzGetTriggerSyncPlayer()}
    end,
    'war3_data',
    true
)
japi.DzTriggerRegisterSyncData(
    function()
    end,
    'unrelated',
    true
)
check.equal(#registered, 1)
check.equal(registered[1][2], 'unrelated')

check.equal(japi.RequestExtraBooleanData(93, 0, '', ''), false)
check.equal(native_request_count, 1)
check.equal(japi.RequestExtraBooleanData(1009, 1, 'war3_data', 'foreign'), false)
check.equal(#sent, 0)
check.is_true(japi.RequestExtraBooleanData(1009, 0, 'war3_data', 'own'))
check.equal(#sent, 1)
check.equal(sent[1][1], 0)
check.equal(sent[1][4], 'own')

local half_character = string.char(0xe4, 0xb8)
callbacks.on_event('war3_data', half_character, 1)
check.equal(#received, 1)
check.equal(received[1][1], half_character)
check.equal(received[1][2], 1)
check.equal(japi.DzGetTriggerSyncData(), 'native data')
check.equal(japi.DzGetTriggerSyncPlayer(), 9)

japi.DzTriggerRegisterSyncData(
    function()
        error('业务触发器失败', 0)
    end,
    'war3_data_page',
    true
)
check.fails(
    function()
        callbacks.on_event('war3_data_page', 'bad', 1)
    end,
    '业务触发器报错应向外传播'
)
check.equal(japi.DzGetTriggerSyncData(), 'native data')

cj.DisplayTimedTextToPlayer = function()
    error('地图替换了显示接口', 0)
end
callbacks.on_local_status('room_failed', {reason = 'host stopped'})
callbacks.on_failure('host stopped')
check.equal(notices, 1)

print('LEGACY_OK owner filtering, synchronized target delivery, context restoration')
