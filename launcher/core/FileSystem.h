#pragma once

#include "Crypto.h"
#include "Win32.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace scriptroom {

namespace fs = std::filesystem;

/// 先写同目录临时文件、落盘，再原子替换目标；中途失败不会留下半截文件。
void atomic_write(const fs::path &Path, std::string_view Bytes);

/// 读取整个文件；失败时抛出 StorageError。
std::string read_file(const fs::path &Path);

/// 以 UTF-8 文本读取，去掉 BOM；内容不是合法 UTF-8 时抛出 Error。
std::string read_utf8_file(const fs::path &Path);

/// 文件的 SHA-256。
Sha256Digest file_digest(const fs::path &Path);

/// 持有期间阻止其他进程打开同一文件的独占句柄。
class FileLock {
public:
    FileLock() = default;
    explicit FileLock(UniqueHandle Handle) : Held(std::move(Handle)) {}

private:
    UniqueHandle Held;
};

/// 以不共享模式创建/打开文件；被占用时抛出 Error。
FileLock exclusive_file(const fs::path &Path);

/// 对文件首字节加非阻塞排他锁，防止多个启动器同时启动。
FileLock launch_lock(const fs::path &Path);

/// 以读写方式、共享所有访问模式试开文件；用于判断可执行文件是否仍被运行中的进程占用。
void probe_writable(const fs::path &Path);

/// 绝对路径（不要求存在）。
fs::path absolute_path(const fs::path &Path);

/// 去掉 \\?\ 前缀并转小写，用于路径比较。
std::string comparable_path(const fs::path &Path);

} // namespace scriptroom
