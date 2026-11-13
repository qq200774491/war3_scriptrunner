#include "ChildProcess.h"

#include "Error.h"
#include "Text.h"

#include <algorithm>
#include <format>
#include <map>

namespace scriptroom {

namespace {

using Millis = std::chrono::milliseconds;
constexpr Millis WaitSlice{100};

/// 等待重叠操作完成，同时响应取消与超时。返回 Ok 表示已完成（成败由 GetOverlappedResult 判断）。
IoStatus wait_overlapped(HANDLE Pipe, OVERLAPPED &Request, const std::atomic<bool> &Cancelled,
                         Millis Timeout) {
    const bool Forever = Timeout.count() < 0;
    const auto Deadline = std::chrono::steady_clock::now() + (Forever ? Millis(0) : Timeout);
    while (true) {
        Millis Slice = WaitSlice;
        if (!Forever) {
            const auto Left = std::chrono::duration_cast<Millis>(Deadline - std::chrono::steady_clock::now());
            Slice = std::clamp(Left, Millis(0), WaitSlice);
        }
        if (WaitForSingleObject(Request.hEvent, static_cast<DWORD>(Slice.count())) == WAIT_OBJECT_0) {
            return IoStatus::Ok;
        }
        const bool Expired = !Forever && std::chrono::steady_clock::now() >= Deadline;
        if (Cancelled.load() || Expired) {
            CancelIoEx(Pipe, &Request);
            DWORD Ignored = 0;
            GetOverlappedResult(Pipe, &Request, &Ignored, TRUE);
            return Cancelled.load() ? IoStatus::Cancelled : IoStatus::Timeout;
        }
    }
}

bool is_closed_pipe(DWORD Code) {
    return Code == ERROR_BROKEN_PIPE || Code == ERROR_HANDLE_EOF || Code == ERROR_PIPE_NOT_CONNECTED ||
           Code == ERROR_NO_DATA;
}

UniqueHandle make_event() {
    UniqueHandle Event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    require(Event.valid(), "创建事件失败：" + last_error_text());
    return Event;
}

} // namespace

PipeStream::PipeStream(UniqueHandle Handle)
    : Pipe(std::move(Handle)), ReadEvent(make_event()), WriteEvent(make_event()) {}

IoStatus PipeStream::read_some(void *Buffer, std::size_t Capacity, std::size_t &Count, Millis Timeout) {
    Count = 0;
    if (Cancelled.load()) {
        return IoStatus::Cancelled;
    }
    OVERLAPPED Request{};
    Request.hEvent = ReadEvent.get();
    ResetEvent(Request.hEvent);
    DWORD Done = 0;
    const DWORD Want = static_cast<DWORD>(std::min<std::size_t>(Capacity, 1u << 20));
    if (!ReadFile(Pipe.get(), Buffer, Want, &Done, &Request)) {
        const DWORD Code = GetLastError();
        if (is_closed_pipe(Code)) {
            return IoStatus::Eof;
        }
        if (Code != ERROR_IO_PENDING) {
            throw StorageError("读取管道失败：" + system_error_text(Code));
        }
        const IoStatus Waited = wait_overlapped(Pipe.get(), Request, Cancelled, Timeout);
        if (Waited != IoStatus::Ok) {
            // 取消的一瞬间数据可能已经到达；有数据就交给调用方。
            DWORD Late = 0;
            if (GetOverlappedResult(Pipe.get(), &Request, &Late, FALSE) && Late > 0) {
                Count = Late;
                return IoStatus::Ok;
            }
            return Waited;
        }
        if (!GetOverlappedResult(Pipe.get(), &Request, &Done, FALSE)) {
            const DWORD Failure = GetLastError();
            if (is_closed_pipe(Failure)) {
                return IoStatus::Eof;
            }
            throw StorageError("读取管道失败：" + system_error_text(Failure));
        }
    }
    if (Done == 0) {
        return IoStatus::Eof;
    }
    Count = Done;
    return IoStatus::Ok;
}

IoStatus PipeStream::write_all(const void *Data, std::size_t Size, Millis Timeout) {
    const auto *Bytes = static_cast<const char *>(Data);
    std::size_t Sent = 0;
    const bool Forever = Timeout.count() < 0;
    const auto Deadline = std::chrono::steady_clock::now() + (Forever ? Millis(0) : Timeout);
    while (Sent < Size) {
        if (Cancelled.load()) {
            return IoStatus::Cancelled;
        }
        OVERLAPPED Request{};
        Request.hEvent = WriteEvent.get();
        ResetEvent(Request.hEvent);
        DWORD Done = 0;
        const DWORD Chunk = static_cast<DWORD>(std::min<std::size_t>(Size - Sent, 1u << 16));
        if (!WriteFile(Pipe.get(), Bytes + Sent, Chunk, &Done, &Request)) {
            const DWORD Code = GetLastError();
            if (is_closed_pipe(Code)) {
                return IoStatus::Eof;
            }
            if (Code != ERROR_IO_PENDING) {
                throw StorageError("写入管道失败：" + system_error_text(Code));
            }
            const Millis Left =
                Forever ? Millis(-1)
                        : std::max(Millis(0), std::chrono::duration_cast<Millis>(
                                                  Deadline - std::chrono::steady_clock::now()));
            const IoStatus Waited = wait_overlapped(Pipe.get(), Request, Cancelled, Left);
            if (Waited != IoStatus::Ok) {
                return Waited;
            }
            if (!GetOverlappedResult(Pipe.get(), &Request, &Done, FALSE)) {
                const DWORD Failure = GetLastError();
                if (is_closed_pipe(Failure)) {
                    return IoStatus::Eof;
                }
                throw StorageError("写入管道失败：" + system_error_text(Failure));
            }
        }
        Sent += Done;
    }
    return IoStatus::Ok;
}

namespace {

/// 创建一对命名管道：父进程端使用重叠 I/O，子进程端同步且可继承。
void make_pipe(bool ParentReads, UniqueHandle &Parent, UniqueHandle &Child) {
    static std::atomic<std::uint32_t> Counter{0};
    const std::wstring Name = std::format(L"\\\\.\\pipe\\ScriptRoom-{}-{}-{}", GetCurrentProcessId(),
                                          Counter.fetch_add(1), GetTickCount64());
    const DWORD Direction = ParentReads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND;
    Parent.reset(CreateNamedPipeW(Name.c_str(), Direction | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                  1, 65536, 65536, 0, nullptr));
    require(Parent.valid(), "创建管道失败：" + last_error_text());
    SECURITY_ATTRIBUTES Inherit{sizeof(Inherit), nullptr, TRUE};
    Child.reset(CreateFileW(Name.c_str(), ParentReads ? GENERIC_WRITE : GENERIC_READ, 0, &Inherit,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(Child.valid(), "连接管道失败：" + last_error_text());
}

UniqueHandle open_null(bool ForWriting) {
    SECURITY_ATTRIBUTES Inherit{sizeof(Inherit), nullptr, TRUE};
    UniqueHandle Null(CreateFileW(L"NUL", ForWriting ? GENERIC_WRITE : GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, &Inherit, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
    require(Null.valid(), "打开 NUL 失败：" + last_error_text());
    return Null;
}

UniqueHandle open_stderr_file(const fs::path &Path) {
    SECURITY_ATTRIBUTES Inherit{sizeof(Inherit), nullptr, TRUE};
    UniqueHandle File(CreateFileW(Path.c_str(), GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &Inherit,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(File.valid(), std::format("无法创建日志 {}：{}", path_to_utf8(Path), last_error_text()));
    return File;
}

std::wstring upper_ascii(std::wstring Text) {
    for (wchar_t &Unit : Text) {
        if (Unit >= L'a' && Unit <= L'z') {
            Unit = static_cast<wchar_t>(Unit - 32);
        }
    }
    return Text;
}

/// 当前环境加上覆盖项，按名称不区分大小写去重并排序，生成 CreateProcess 需要的环境块。
std::wstring build_environment(const std::vector<std::pair<std::string, std::string>> &Overrides) {
    std::map<std::wstring, std::wstring> Entries; // 大写名称 -> “原名=值”
    wchar_t *Block = GetEnvironmentStringsW();
    if (Block != nullptr) {
        for (const wchar_t *Cursor = Block; *Cursor != L'\0'; Cursor += wcslen(Cursor) + 1) {
            const std::wstring Line(Cursor);
            const std::size_t Equals = Line.find(L'=', 1);
            if (Equals != std::wstring::npos) {
                Entries[upper_ascii(Line.substr(0, Equals))] = Line;
            }
        }
        FreeEnvironmentStringsW(Block);
    }
    for (const auto &Override : Overrides) {
        const std::wstring Name = to_wide(Override.first);
        Entries[upper_ascii(Name)] = Name + L"=" + to_wide(Override.second);
    }
    std::wstring Result;
    for (const auto &Entry : Entries) {
        Result += Entry.second;
        Result.push_back(L'\0');
    }
    Result.push_back(L'\0');
    return Result;
}

struct AttributeList {
    std::vector<char> Storage;
    LPPROC_THREAD_ATTRIBUTE_LIST List = nullptr;

    explicit AttributeList(std::vector<HANDLE> &Handles) {
        SIZE_T Size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &Size);
        Storage.resize(Size);
        List = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(Storage.data());
        require(InitializeProcThreadAttributeList(List, 1, 0, &Size) != FALSE,
                "初始化进程属性失败：" + last_error_text());
        if (!UpdateProcThreadAttribute(List, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Handles.data(),
                                       Handles.size() * sizeof(HANDLE), nullptr, nullptr)) {
            const std::string Reason = last_error_text();
            DeleteProcThreadAttributeList(List);
            throw Error("设置继承句柄失败：" + Reason);
        }
    }
    ~AttributeList() { DeleteProcThreadAttributeList(List); }
    AttributeList(const AttributeList &) = delete;
    AttributeList &operator=(const AttributeList &) = delete;
};

} // namespace

std::string windows_argument(std::string_view Value) {
    if (!Value.empty() && Value.find_first_of(" \t\"") == std::string_view::npos) {
        return std::string(Value);
    }
    std::string Result = "\"";
    std::size_t Slashes = 0;
    for (const char Unit : Value) {
        if (Unit == '\\') {
            ++Slashes;
            continue;
        }
        if (Unit == '"') {
            Result.append(Slashes * 2 + 1, '\\');
        } else {
            Result.append(Slashes, '\\');
        }
        Slashes = 0;
        Result.push_back(Unit);
    }
    Result.append(Slashes * 2, '\\');
    Result.push_back('"');
    return Result;
}

ChildProcess ChildProcess::spawn(const ChildSpec &Spec) {
    UniqueHandle StdinChild;
    UniqueHandle StdoutChild;
    UniqueHandle StderrChild;
    ChildProcess Result;
    UniqueHandle StdinParent;
    UniqueHandle StdoutParent;

    if (Spec.PipeStdin) {
        make_pipe(false, StdinParent, StdinChild);
    } else {
        StdinChild = open_null(false);
    }
    if (Spec.PipeStdout) {
        make_pipe(true, StdoutParent, StdoutChild);
    } else {
        StdoutChild = open_null(true);
    }
    StderrChild = Spec.StderrFile.empty() ? open_null(true) : open_stderr_file(Spec.StderrFile);

    std::vector<HANDLE> Inherited = {StdinChild.get(), StdoutChild.get(), StderrChild.get()};
    AttributeList Attributes(Inherited);

    STARTUPINFOEXW Startup{};
    Startup.StartupInfo.cb = sizeof(Startup);
    Startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    Startup.StartupInfo.hStdInput = StdinChild.get();
    Startup.StartupInfo.hStdOutput = StdoutChild.get();
    Startup.StartupInfo.hStdError = StderrChild.get();
    Startup.lpAttributeList = Attributes.List;

    std::string CommandLine = windows_argument(path_to_utf8(Spec.Executable));
    for (const std::string &Argument : Spec.Arguments) {
        CommandLine += ' ' + windows_argument(Argument);
    }
    std::wstring Command = to_wide(CommandLine);
    std::wstring Environment = build_environment(Spec.Environment);
    const std::wstring Directory = Spec.WorkingDirectory.native();

    PROCESS_INFORMATION Info{};
    const BOOL Created = CreateProcessW(
        Spec.Executable.c_str(), Command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, Environment.data(),
        Directory.empty() ? nullptr : Directory.c_str(), &Startup.StartupInfo, &Info);
    if (!Created) {
        throw Error(std::format("无法启动 {}：{}", path_to_utf8(Spec.Executable), last_error_text()));
    }
    CloseHandle(Info.hThread);
    Result.Process.reset(Info.hProcess);
    Result.Pid = Info.dwProcessId;
    Result.KillOnDestroy = Spec.KillOnDestroy;
    if (StdinParent.valid()) {
        Result.Stdin = std::make_unique<PipeStream>(std::move(StdinParent));
    }
    if (StdoutParent.valid()) {
        Result.Stdout = std::make_unique<PipeStream>(std::move(StdoutParent));
    }
    return Result;
}

ChildProcess &ChildProcess::operator=(ChildProcess &&Other) noexcept {
    if (this != &Other) {
        if (KillOnDestroy) {
            kill();
        }
        Process = std::move(Other.Process);
        Pid = Other.Pid;
        Stdin = std::move(Other.Stdin);
        Stdout = std::move(Other.Stdout);
        KillOnDestroy = Other.KillOnDestroy;
        Other.KillOnDestroy = false;
    }
    return *this;
}

ChildProcess::~ChildProcess() {
    if (KillOnDestroy && Process.valid()) {
        kill();
        WaitForSingleObject(Process.get(), 2000);
    }
}

std::optional<std::uint32_t> ChildProcess::try_wait() {
    return wait_for(Millis(0));
}

std::optional<std::uint32_t> ChildProcess::wait_for(Millis Timeout) {
    if (!Process.valid()) {
        return std::nullopt;
    }
    const DWORD Waited = WaitForSingleObject(Process.get(), static_cast<DWORD>(Timeout.count()));
    if (Waited == WAIT_TIMEOUT) {
        return std::nullopt;
    }
    require(Waited == WAIT_OBJECT_0, "等待子进程失败：" + last_error_text());
    DWORD Code = 0;
    require(GetExitCodeProcess(Process.get(), &Code) != FALSE, "读取退出码失败：" + last_error_text());
    return Code;
}

void ChildProcess::kill() {
    if (Process.valid() && WaitForSingleObject(Process.get(), 0) == WAIT_TIMEOUT) {
        TerminateProcess(Process.get(), 1);
    }
}

void ChildProcess::detach() {
    KillOnDestroy = false;
    Stdin.reset();
    Stdout.reset();
    Process.reset();
}

} // namespace scriptroom
