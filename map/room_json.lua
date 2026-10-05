-- 网络边界的 JSON 解码器；null 用独立值保留，禁止执行来自网络的 Lua。
local M = {null = {}}
function M.decode(source)
    assert(type(source) == 'string' and #source <= 262144, 'invalid JSON size')
    local at = 1
    local function skip() at = source:find('[^ \t\r\n]', at) or (#source + 1) end
    local function hex4()
        local value = source:sub(at, at + 3)
        assert(value:match('^%x%x%x%x$'), 'invalid unicode escape')
        at = at + 4
        return tonumber(value, 16)
    end
    local escapes = {['"'] = '"', ['\\'] = '\\', ['/'] = '/', b = '\b', f = '\f', n = '\n', r = '\r', t = '\t'}
    local function quoted()
        at = at + 1
        local result = {}
        while at <= #source do
            local char = source:sub(at, at)
            at = at + 1
            if char == '"' then return table.concat(result) end
            if char == '\\' then
                char = source:sub(at, at)
                at = at + 1
                if char == 'u' then
                    local code = hex4()
                    if code >= 0xd800 and code <= 0xdbff then
                        assert(source:sub(at, at + 1) == '\\u', 'missing low surrogate')
                        at = at + 2
                        local low = hex4()
                        assert(low >= 0xdc00 and low <= 0xdfff, 'invalid low surrogate')
                        code = 0x10000 + (code - 0xd800) * 1024 + low - 0xdc00
                    else
                        assert(code < 0xdc00 or code > 0xdfff, 'unexpected low surrogate')
                    end
                    char = utf8.char(code)
                else
                    char = assert(escapes[char], 'invalid escape')
                end
            else
                assert(char:byte() >= 32, 'control character in string')
            end
            result[#result + 1] = char
        end
        error('unterminated string')
    end
    local parse
    local function container(depth, object)
        at = at + 1
        skip()
        local result, seen = {}, {}
        local last = object and '}' or ']'
        if source:sub(at, at) == last then at = at + 1; return result end
        while true do
            local key = #result + 1
            if object then
                assert(source:sub(at, at) == '"', 'object key expected')
                key = quoted()
                assert(not seen[key], 'duplicate key')
                seen[key] = true
                skip()
                assert(source:sub(at, at) == ':', 'colon expected')
                at = at + 1
            end
            result[key] = parse(depth + 1)
            skip()
            local char = source:sub(at, at)
            at = at + 1
            if char == last then return result end
            assert(char == ',', 'comma expected')
            skip()
        end
    end
    function parse(depth)
        assert(depth <= 32, 'JSON nesting too deep')
        skip()
        local char = source:sub(at, at)
        if char == '"' then return quoted() end
        if char == '{' or char == '[' then return container(depth, char == '{') end
        for literal, value in pairs({['true'] = true, ['false'] = false, ['null'] = M.null}) do
            if source:sub(at, at + #literal - 1) == literal then at = at + #literal; return value end
        end
        local number = source:sub(at):match('^[^%s,%]%}]+')
        assert(number, 'invalid number')
        local mantissa, exponent = number:match('^(.-)[eE](.*)$')
        if exponent then assert(exponent:match('^[+-]?%d+$'), 'invalid exponent') end
        mantissa = mantissa or number
        local integer, fraction = mantissa:match('^(.-)%.(.*)$')
        if fraction then assert(fraction:match('^%d+$'), 'invalid fraction') end
        integer = integer or mantissa
        assert(integer:match('^-?%d+$') and not integer:match('^-?0%d'), 'invalid integer')
        local value = assert(tonumber(number), 'invalid number')
        assert(value == value and math.abs(value) < math.huge, 'nonfinite number')
        at = at + #number
        return value
    end
    local result = parse(0)
    skip()
    assert(at > #source, 'trailing JSON data')
    return result
end
return M
