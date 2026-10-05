local adapter = require 'shared_room'
for local_player = 0, 1 do
    for _, before_connection in ipairs({true, false}) do
        local left, tick, opened, closed = false, nil, 0, 0
        local statuses, packets = {}, {}
        local cj = {
            GetPlayerId = function(value) return value end,
            GetLocalPlayer = function() return local_player end,
            Player = function(value) return value end,
            GetPlayerSlotState = function(player)
                return left and player == 1 and 2 or 1
            end,
            PLAYER_SLOT_STATE_PLAYING = 1,
            CreateTrigger = function() return {} end,
            TriggerAddAction = function() end,
            CreateTimer = function() return {} end,
            TimerStart = function(_, _, _, callback) tick = callback end,
        }
        local connection = {
            join = function() end,
            poll = function() end,
            close = function() closed = closed + 1 end,
        }
        adapter.start({room = 'roster-test', relay = 0, players = {
            [0] = {port = 1, token = 'a'}, [1] = {port = 2, token = 'b'},
        }}, {
            on_local_status = function(kind, reason)
                statuses[#statuses + 1] = {kind, reason}
            end,
            on_failure = function() error('local roster check escaped into synchronized callback') end,
        }, {common = cj, japi = {
            DzTriggerRegisterSyncData = function() end,
            DzSyncData = function(_, packet) packets[#packets + 1] = packet end,
        }, native = {open = function()
            opened = opened + 1
            return connection
        end}})
        if not before_connection then
            tick()
            assert(opened == 1 and closed == 0 and #statuses == 0)
        end
        left = true
        tick()
        tick()
        assert(#statuses == 1 and statuses[1][1] == 'failed')
        assert(statuses[1][2]:find('game player 1 left', 1, true))
        assert(opened == (before_connection and 0 or 1))
        assert(closed == (before_connection and 0 or 1))
        assert(local_player == 0 and #packets > 0 or local_player == 1 and #packets == 0)
    end
end
print('ROSTER_OK local game disconnect closes live cloud transport once')
