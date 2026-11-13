---@alias RoomErrorKey
---| 'json_too_large'
---| 'json_comma'
---| 'json_unicode_escape'
---| 'json_lone_low_surrogate'
---| 'json_missing_low_surrogate'
---| 'json_bad_low_surrogate'
---| 'json_escape'
---| 'json_control_char'
---| 'json_unterminated_string'
---| 'json_number'
---| 'json_number_range'
---| 'json_key_expected'
---| 'json_duplicate_key'
---| 'json_colon'
---| 'json_too_deep'
---| 'json_trailing'
---| 'event_wrong_room'
---| 'event_unknown_kind'
---| 'event_delivery_gap'
---| 'event_bad_target'
---| 'event_malformed'
---| 'event_bad_payload'
---| 'event_unhandled_channel'
---| 'transport_bad_room'
---| 'transport_bad_chunk'
---| 'transport_too_large'
---| 'transport_queue_full'
---| 'transport_response_too_large'
---| 'transport_sequence_gap'
---| 'transport_fragment_conflict'
---| 'transport_packet_size'
---| 'transport_bad_fragment'
---| 'client_closed'
---| 'client_queue_full'
---| 'client_player_left'
---| 'client_relay_failed'
---| 'sync_unavailable'
---| 'load_native'
---| 'native'
---| 'log_open'
---| 'demo_counter'
---| 'connection_lost_notice'

local MESSAGES = {
    json_too_large = 'JSON 文本过大',
    json_comma = 'JSON 缺少逗号',
    json_unicode_escape = 'JSON 的 Unicode 转义不合法',
    json_lone_low_surrogate = 'JSON 出现孤立的低位代理项',
    json_missing_low_surrogate = 'JSON 缺少低位代理项',
    json_bad_low_surrogate = 'JSON 的低位代理项不合法',
    json_escape = 'JSON 的转义字符不合法',
    json_control_char = 'JSON 字符串里有控制字符',
    json_unterminated_string = 'JSON 字符串没有结束',
    json_number = 'JSON 数字不合法',
    json_number_range = 'JSON 数字超出范围',
    json_key_expected = 'JSON 对象缺少键名',
    json_duplicate_key = 'JSON 对象有重复的键',
    json_colon = 'JSON 对象缺少冒号',
    json_too_deep = 'JSON 嵌套太深',
    json_trailing = 'JSON 末尾有多余内容',

    event_wrong_room = '同步消息不属于当前房间',
    event_unknown_kind = '未知的同步消息类型',
    event_delivery_gap = '云端事件序号不连续',
    event_bad_target = '云端事件的目标玩家无效',
    event_malformed = '云端事件格式不正确',
    event_bad_payload = '云端事件的二进制载荷不合法',
    event_unhandled_channel = '没有处理云端通道：%s',

    transport_bad_room = '房间 ID 不合法',
    transport_bad_chunk = '不支持的同步分片大小',
    transport_too_large = '同步响应超过 16 KiB 上限',
    transport_queue_full = '同步发送队列已满',
    transport_response_too_large = '同步响应过大',
    transport_sequence_gap = '同步序号不连续',
    transport_fragment_conflict = '同步分片内容冲突',
    transport_packet_size = '同步包长度不合法',
    transport_bad_fragment = '同步分片不合法',

    client_closed = '连接已关闭',
    client_queue_full = '待发送请求队列已满',
    client_player_left = '游戏玩家 %d 已离开，需要重新建房',
    client_relay_failed = '中继传输失败',

    sync_unavailable = '当前环境没有立即同步接口',
    load_native = '无法加载桥接库：%s',
    native = '桥接库报错：%s',
    log_open = '无法打开日志：%s',
    demo_counter = '计数器不是数字：%s',

    connection_lost_notice = '|cffff5555本地联机或云脚本连接已中断，存档加载不能继续。' ..
        '请退出两端后重新启动测试。详情见 mls-player 日志。|r',
}

local M = {}

---@param key RoomErrorKey
---@param ... any
---@return string
function M.text(key, ...)
    return MESSAGES[key]:format(...)
end

---@param key RoomErrorKey
---@param ... any
---@return any
function M.fail(key, ...)
    error(M.text(key, ...), 0)
end

---@param message string
---@return any
function M.rethrow(message)
    error(message, 0)
end

return M
