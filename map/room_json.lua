local errors = require 'room_errors'

local M = {null = {}}

local MAX_SOURCE_BYTES = 262144
local MAX_DEPTH = 32

local ESCAPES = {
    ['"'] = '"',
    ['\\'] = '\\',
    ['/'] = '/',
    b = '\b',
    f = '\f',
    n = '\n',
    r = '\r',
    t = '\t',
}

local LITERALS = {
    {'true', true},
    {'false', false},
    {'null', M.null},
}

---@class JsonParser
---@field source string
---@field at integer
local Parser = {}
Parser.__index = Parser

---@param source string
---@return JsonParser
local function new_parser(source)
    local o = {source = source, at = 1}
    setmetatable(o, Parser)
    return o
end

function Parser:peek()
    return self.source:sub(self.at, self.at)
end

function Parser:advance()
    local char = self:peek()
    self.at = self.at + 1
    return char
end

function Parser:skip_blank()
    self.at = self.source:find('[^ \t\r\n]', self.at) or (#self.source + 1)
end

function Parser:close_if(closing)
    if self:peek() ~= closing then
        return false
    end
    self.at = self.at + 1
    return true
end

function Parser:finish_member(closing)
    self:skip_blank()
    local char = self:advance()
    if char == closing then
        return true
    end
    if char ~= ',' then
        errors.fail('json_comma')
    end
    self:skip_blank()
    return false
end

function Parser:read_hex4()
    local digits = self.source:match('^%x%x%x%x', self.at) or errors.fail('json_unicode_escape')
    self.at = self.at + 4
    return tonumber(digits, 16)
end

function Parser:read_unicode()
    local code = self:read_hex4()
    if code >= 0xdc00 and code <= 0xdfff then
        errors.fail('json_lone_low_surrogate')
    end
    if code < 0xd800 or code > 0xdbff then
        return code
    end
    if self.source:sub(self.at, self.at + 1) ~= '\\u' then
        errors.fail('json_missing_low_surrogate')
    end
    self.at = self.at + 2
    local low = self:read_hex4()
    if low < 0xdc00 or low > 0xdfff then
        errors.fail('json_bad_low_surrogate')
    end
    return 0x10000 + (code - 0xd800) * 1024 + low - 0xdc00
end

function Parser:read_escape()
    local char = self:advance()
    if char == 'u' then
        return utf8.char(self:read_unicode())
    end
    return ESCAPES[char] or errors.fail('json_escape')
end

function Parser:read_string()
    self.at = self.at + 1
    local parts = {}
    while self.at <= #self.source do
        local char = self:advance()
        if char == '"' then
            return table.concat(parts)
        end
        if char == '\\' then
            char = self:read_escape()
        elseif char:byte() < 32 then
            errors.fail('json_control_char')
        end
        parts[#parts + 1] = char
    end
    errors.fail('json_unterminated_string')
end

---@param text string
---@return number|nil
local function parse_number(text)
    local mantissa, exponent = text:match('^(.-)[eE](.*)$')
    if exponent and not exponent:match('^[+-]?%d+$') then
        return nil
    end
    mantissa = mantissa or text
    local integer, fraction = mantissa:match('^(.-)%.(.*)$')
    if fraction and not fraction:match('^%d+$') then
        return nil
    end
    integer = integer or mantissa
    if not integer:match('^-?%d+$') or integer:match('^-?0%d') then
        return nil
    end
    return tonumber(text)
end

function Parser:read_number()
    local text = self.source:match('^[^%s,%]%}]+', self.at) or errors.fail('json_number')
    local value = parse_number(text) or errors.fail('json_number')
    if math.abs(value) == math.huge then
        errors.fail('json_number_range')
    end
    self.at = self.at + #text
    return value
end

function Parser:read_array(depth)
    self.at = self.at + 1
    self:skip_blank()
    local result = {}
    if self:close_if(']') then
        return result
    end
    while true do
        result[#result + 1] = self:read_value(depth + 1)
        if self:finish_member(']') then
            return result
        end
    end
end

function Parser:read_object(depth)
    self.at = self.at + 1
    self:skip_blank()
    local result = {}
    if self:close_if('}') then
        return result
    end
    while true do
        if self:peek() ~= '"' then
            errors.fail('json_key_expected')
        end
        local key = self:read_string()
        if result[key] ~= nil then
            errors.fail('json_duplicate_key')
        end
        self:skip_blank()
        if not self:close_if(':') then
            errors.fail('json_colon')
        end
        result[key] = self:read_value(depth + 1)
        if self:finish_member('}') then
            return result
        end
    end
end

function Parser:read_value(depth)
    if depth > MAX_DEPTH then
        errors.fail('json_too_deep')
    end
    self:skip_blank()
    local char = self:peek()
    if char == '"' then
        return self:read_string()
    end
    if char == '{' then
        return self:read_object(depth)
    end
    if char == '[' then
        return self:read_array(depth)
    end
    for _, literal in ipairs(LITERALS) do
        local text = literal[1]
        if self.source:sub(self.at, self.at + #text - 1) == text then
            self.at = self.at + #text
            return literal[2]
        end
    end
    return self:read_number()
end

---@param source string
---@return any value
function M.decode(source)
    if #source > MAX_SOURCE_BYTES then
        errors.fail('json_too_large')
    end
    local parser = new_parser(source)
    local result = parser:read_value(0)
    parser:skip_blank()
    if parser.at <= #source then
        errors.fail('json_trailing')
    end
    return result
end

return M
