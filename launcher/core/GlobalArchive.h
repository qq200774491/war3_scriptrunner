#pragma once

#include "Json.h"
#include "Sqlite.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace scriptroom {

/// 全局存档回包：要回传给云脚本的事件名与已序列化前的结果。
struct GlobalReply {
    std::string EventName;
    Json Result;
};

/// 全局哈希存档与租约锁，对应云脚本的 MsGA* 接口。
///
/// 校验失败（非法键、超限、类型冲突等）会以 errnu=-1 的回包交给脚本；
/// 存储故障抛出 StorageError，由房间视为致命错误。
class GlobalArchive {
public:
    /// 在目录下打开（或创建）global.sqlite3。
    explicit GlobalArchive(const std::filesystem::path &Directory);

    /// 处理一次请求，永远返回回包，除非发生存储故障。
    GlobalReply request(std::uint32_t Transaction, std::string_view Operation, std::string_view Key,
                        std::string_view Argument);

private:
    using Clock = std::chrono::steady_clock;

    Json checked_request(std::string_view Operation, std::string_view Key, std::string_view Argument);
    Json lease(std::string_view Operation, const std::string &Key, std::string_view Argument);
    Json hash(std::string_view Operation, const std::string &Key, std::string_view Argument);

    bool lock_active(const std::string &Key) const;

    Database Store;
    std::map<std::string, Clock::time_point> Locks;
};

} // namespace scriptroom
