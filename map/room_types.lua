---@meta

---@class RoomPlayer
---@field port integer
---@field token string

---@class RoomProfile
---@field room string
---@field relay integer
---@field players table<integer, RoomPlayer>
---@field dll string
---@field logs string|nil
---@field sync_immediate boolean|nil
---@field sync_chunk 80|350|nil

---@class RoomMessage
---@field kind 'joined'|'ready'|'event'|'ack'|'error'|'room_failed'
---@field room string|nil
---@field delivery integer|nil
---@field player integer|nil
---@field event string|nil
---@field payload string|nil
---@field payload_encoding 'hex'|nil
---@field reason string|nil

---@class RoomCallbacks
---@field on_ready fun()|nil
---@field on_event fun(name: string, payload: string, player: integer)
---@field on_failure fun(reason: string|nil)|nil
---@field on_local_status fun(kind: string, detail: any)|nil

---@class RoomHandle
---@field send fun(player: integer, request_id: string, event: string, payload: string)
---@field close fun()

---@class NativeConnection
---@field join fun(self: NativeConnection, room: string, player: integer, token: string)
---@field send fun(self: NativeConnection, id: string, event: string, payload: string)
---@field poll fun(self: NativeConnection): string|nil
---@field close fun(self: NativeConnection)

---@class NativeRoom
---@field open fun(port: integer): NativeConnection

---@class RoomDependencies
---@field common table
---@field japi table
---@field native NativeRoom
