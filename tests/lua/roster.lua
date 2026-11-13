---@diagnostic disable: undefined-global, missing-fields, redundant-parameter
local check = require 'check'
local errors = require 'room_errors'
local shared_room = require 'shared_room'

local PLAYING = 1
local LEFT = 2

local function run_case(local_player, before_connection)
    local left = false
    local tick
    local opened = 0
    local closed = 0
    local statuses = {}
    local packets = {}

    local cj = {
        GetPlayerId = function(value)
            return value
        end,
        GetLocalPlayer = function()
            return local_player
        end,
        Player = function(value)
            return value
        end,
        GetPlayerSlotState = function(player)
            if left and player == 1 then
                return LEFT
            end
            return PLAYING
        end,
        PLAYER_SLOT_STATE_PLAYING = PLAYING,
        CreateTrigger = function()
            return {}
        end,
        TriggerAddAction = function()
        end,
        CreateTimer = function()
            return {}
        end,
        TimerStart = function(_, _, _, callback)
            tick = callback
        end,
    }
    local japi = {
        DzTriggerRegisterSyncData = function()
        end,
        DzSyncData = function(_, packet)
            packets[#packets + 1] = packet
        end,
    }
    local connection = {
        join = function()
        end,
        poll = function()
        end,
        close = function()
            closed = closed + 1
        end,
    }
    local native = {
        open = function()
            opened = opened + 1
            return connection
        end,
    }

    local profile = {
        room = 'roster-test',
        relay = 0,
        players = {
            [0] = {port = 1, token = 'a'},
            [1] = {port = 2, token = 'b'},
        },
    }
    local callbacks = {
        on_local_status = function(kind, reason)
            statuses[#statuses + 1] = {kind, reason}
        end,
        on_failure = function()
            error('本机名单检查不应进入同步回调', 0)
        end,
    }
    shared_room.start(profile, callbacks, {common = cj, japi = japi, native = native})

    if not before_connection then
        tick()
        check.equal(opened, 1)
        check.equal(closed, 0)
        check.equal(#statuses, 0)
    end

    left = true
    tick()
    tick()
    check.equal(#statuses, 1)
    check.equal(statuses[1][1], 'failed')
    check.equal(statuses[1][2], errors.text('client_player_left', 1))

    local expected_opens = 1
    if before_connection then
        expected_opens = 0
    end
    check.equal(opened, expected_opens)
    check.equal(closed, expected_opens)

    if local_player == 0 then
        check.is_true(#packets > 0)
    else
        check.equal(#packets, 0)
    end
end

for local_player = 0, 1 do
    run_case(local_player, true)
    run_case(local_player, false)
end

print('ROSTER_OK local game disconnect closes live cloud transport once')
