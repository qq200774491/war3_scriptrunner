#pragma once

#include "Win32.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace scriptroom {

namespace fs = std::filesystem;

enum class IoStatus {
    Ok,        ///< 完成了请求的传输
    Eof,       ///< 对端已关闭
    Timeout,   ///< 超时，未完成
    Cancelled, ///< 被 cancel() 中止
};

/// 重叠 I/O 命名管道的一端，支持超时和跨线程取消。
/// 同一方向同时只允许一个线程读或写。
class PipeStream {
public:
    explicit PipeStream(UniqueHandle Pipe);
    PipeStream(const PipeStream &) = delete;
    PipeStream &operator=(const PipeStream &) = delete;

    /// 读取至少一个字节。Timeout 为负表示一直等待。
    IoStatus read_some(void *Buffer, std::size_t Capacity, std::size_t &Count,
                       std::chrono::milliseconds Timeout);

    /// 写完全部数据或失败；对端关闭按 Eof 返回。
    IoStatus write_all(const void *Data, std::size_t Size, std::chrono::milliseconds Timeout);

    /// 令当前及之后的读写尽快以 Cancelled 返回。
    void cancel() { Cancelled.store(true); }

private:
    UniqueHandle Pipe;
    UniqueHandle ReadEvent;
    UniqueHandle WriteEvent;
    std::atomic<bool> Cancelled{false};
};

/// 子进程创建参数。
struct ChildSpec {
    fs::path Executable;
    std::vector<std::string> Arguments;                          ///< UTF-8，自动按 Windows 规则加引号
    fs::path WorkingDirectory;                                   ///< 为空则继承
    std::vector<std::pair<std::string, std::string>> Environment; ///< 覆盖或新增的环境变量
    bool PipeStdin = false;
    bool PipeStdout = false;
    fs::path StderrFile; ///< 为空则丢弃标准错误
    bool KillOnDestroy = false;
};

/// 无窗口子进程。默认只继承它自己的三个标准句柄，不泄漏其他句柄。
class ChildProcess {
public:
    static ChildProcess spawn(const ChildSpec &Spec);

    ChildProcess(ChildProcess &&Other) noexcept = default;
    ChildProcess &operator=(ChildProcess &&Other) noexcept;
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;
    ~ChildProcess();

    std::uint32_t pid() const { return Pid; }

    PipeStream &stdin_pipe() { return *Stdin; }
    PipeStream &stdout_pipe() { return *Stdout; }

    /// 已退出则返回退出码，仍在运行返回 nullopt。
    std::optional<std::uint32_t> try_wait();
    std::optional<std::uint32_t> wait_for(std::chrono::milliseconds Timeout);

    /// 强制结束；进程已退出时无副作用。
    void kill();

    /// 放弃对子进程的所有权：不再在析构时结束它，也不再持有管道。
    void detach();

private:
    ChildProcess() = default;

    UniqueHandle Process;
    std::uint32_t Pid = 0;
    std::unique_ptr<PipeStream> Stdin;
    std::unique_ptr<PipeStream> Stdout;
    bool KillOnDestroy = false;
};

/// 按 CommandLineToArgvW 规则给单个参数加引号；无需引号时原样返回。
std::string windows_argument(std::string_view Value);

} // namespace scriptroom
