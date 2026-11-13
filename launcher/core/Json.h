#pragma once

#include "Error.h"

#include "nlohmann/json.hpp"

#include <string>
#include <string_view>

namespace scriptroom {

/// 键按字典序排列，与既有存档、协议输出保持一致。
using Json = nlohmann::json;

/// 严格解析 JSON；格式错误统一转换为 Error。
inline Json parse_json(std::string_view Text) {
    try {
        return Json::parse(Text.begin(), Text.end());
    } catch (const Json::exception &Failure) {
        throw Error(std::string("JSON 格式错误：") + Failure.what());
    }
}

/// 紧凑序列化；字符串中的非法 UTF-8 以替换字符输出，不抛异常。
inline std::string dump_compact(const Json &Value) {
    return Value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

/// 两空格缩进序列化。
inline std::string dump_pretty(const Json &Value) {
    return Value.dump(2, ' ', false, Json::error_handler_t::replace);
}

} // namespace scriptroom
