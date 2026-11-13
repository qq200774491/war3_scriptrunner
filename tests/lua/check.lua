---@diagnostic disable: undefined-global, missing-fields, redundant-parameter
local M = {}

---@param value any
---@param message string|nil
function M.is_true(value, message)
    if not value then
        error(message or '断言失败', 0)
    end
end

---@param actual any
---@param expected any
---@param message string|nil
function M.equal(actual, expected, message)
    if actual ~= expected then
        error(message or ('期望 %s，实际 %s'):format(tostring(expected), tostring(actual)), 0)
    end
end

---@param callback fun()
---@param label string
function M.fails(callback, label)
    if pcall(callback) then
        error('应当报错但没有报错：' .. label, 0)
    end
end

return M
