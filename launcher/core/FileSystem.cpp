#include "FileSystem.h"

#include "Error.h"
#include "Text.h"

#include <format>
#include <system_error>

namespace scriptroom {

namespace {

[[noreturn]] void throw_io(const std::string &Action, const fs::path &Path) {
    throw StorageError(std::format("{} {}：{}", Action, path_to_utf8(Path), last_error_text()));
}

UniqueHandle open_file(const fs::path &Path, DWORD Access, DWORD Share, DWORD Disposition) {
    HANDLE Raw = CreateFileW(Path.c_str(), Access, Share, nullptr, Disposition,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    return UniqueHandle(Raw);
}

void write_all(HANDLE Handle, std::string_view Bytes, const fs::path &Path) {
    std::size_t Done = 0;
    while (Done < Bytes.size()) {
        const DWORD Chunk = static_cast<DWORD>(std::min<std::size_t>(Bytes.size() - Done, 1u << 20));
        DWORD Written = 0;
        if (!WriteFile(Handle, Bytes.data() + Done, Chunk, &Written, nullptr)) {
            throw_io("写入文件", Path);
        }
        Done += Written;
    }
}

} // namespace

void atomic_write(const fs::path &Path, std::string_view Bytes) {
    const fs::path Absolute = absolute_path(Path);
    const fs::path Parent = Absolute.parent_path();
    require(!Parent.empty(), "没有文件目录");
    std::error_code Code;
    fs::create_directories(Parent, Code);
    require(!Code, std::format("创建目录 {} 失败：{}", path_to_utf8(Parent), Code.message()));

    const fs::path Pending = Parent / (L".tmp-" + to_wide(random_hex(8)));
    {
        UniqueHandle File = open_file(Pending, GENERIC_WRITE, 0, CREATE_NEW);
        if (!File) {
            throw_io("创建临时文件", Pending);
        }
        try {
            write_all(File.get(), Bytes, Pending);
            if (!FlushFileBuffers(File.get())) {
                throw_io("落盘", Pending);
            }
        } catch (...) {
            File.reset();
            DeleteFileW(Pending.c_str());
            throw;
        }
    }
    if (!MoveFileExW(Pending.c_str(), Absolute.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const std::string Reason = last_error_text();
        DeleteFileW(Pending.c_str());
        throw StorageError(std::format("替换文件 {} 失败：{}", path_to_utf8(Absolute), Reason));
    }
}

std::string read_file(const fs::path &Path) {
    UniqueHandle File =
        open_file(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  OPEN_EXISTING);
    if (!File) {
        throw_io("读取文件", Path);
    }
    std::string Content;
    char Buffer[65536];
    while (true) {
        DWORD Count = 0;
        if (!ReadFile(File.get(), Buffer, sizeof(Buffer), &Count, nullptr)) {
            throw_io("读取文件", Path);
        }
        if (Count == 0) {
            break;
        }
        Content.append(Buffer, Count);
    }
    return Content;
}

std::string read_utf8_file(const fs::path &Path) {
    std::string Content = read_file(Path);
    require(is_valid_utf8(Content), std::format("文件不是有效的 UTF-8：{}", path_to_utf8(Path)));
    return std::string(strip_bom(Content));
}

Sha256Digest file_digest(const fs::path &Path) {
    UniqueHandle File =
        open_file(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  OPEN_EXISTING);
    if (!File) {
        throw_io("读取文件", Path);
    }
    Sha256Stream Stream;
    std::vector<std::uint8_t> Buffer(65536);
    while (true) {
        DWORD Count = 0;
        if (!ReadFile(File.get(), Buffer.data(), static_cast<DWORD>(Buffer.size()), &Count, nullptr)) {
            throw_io("读取文件", Path);
        }
        if (Count == 0) {
            break;
        }
        Stream.update(std::span<const std::uint8_t>(Buffer.data(), Count));
    }
    return Stream.finish();
}

FileLock exclusive_file(const fs::path &Path) {
    UniqueHandle File = open_file(Path, GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS);
    if (!File) {
        throw Error(std::format("文件被占用或无法写入：{}", path_to_utf8(Path)));
    }
    return FileLock(std::move(File));
}

FileLock launch_lock(const fs::path &Path) {
    UniqueHandle File = open_file(Path, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  OPEN_ALWAYS);
    if (!File) {
        throw_io("打开启动锁", Path);
    }
    OVERLAPPED Region{};
    if (!LockFileEx(File.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                    &Region)) {
        throw Error(std::format("已有启动器运行或启动锁不可用：{}", last_error_text()));
    }
    return FileLock(std::move(File));
}

void probe_writable(const fs::path &Path) {
    UniqueHandle File =
        open_file(Path, GENERIC_READ | GENERIC_WRITE,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING);
    if (!File) {
        throw Error(std::format("请关闭旧测试客户端：{}", path_to_utf8(Path)));
    }
}

fs::path absolute_path(const fs::path &Path) {
    std::error_code Code;
    fs::path Result = fs::absolute(Path, Code);
    require(!Code, std::format("无法取得绝对路径 {}：{}", path_to_utf8(Path), Code.message()));
    return Result;
}

std::string comparable_path(const fs::path &Path) {
    std::string Text = path_to_utf8(Path);
    constexpr std::string_view Prefix = "\\\\?\\";
    if (Text.starts_with(Prefix)) {
        Text.erase(0, Prefix.size());
    }
    return to_lower(Text);
}

} // namespace scriptroom
