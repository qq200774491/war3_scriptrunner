#include <Windows.h>
#include <detours.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// 仅启动给定的测试游戏，并记录它的 DLL/异常事件；超时结束自己创建的进程。
int wmain(int argc, wchar_t** argv) {
    if (argc != 5) return 2;
    std::ofstream log(std::filesystem::path(argv[4]), std::ios::out);
    auto record = [&](const std::string& value) { log << GetTickCount64() << ' ' << value << '\n'; log.flush(); };
    std::wstring command(argv[2]);
    const auto loader = std::filesystem::path(argv[3]).string();
    const auto directory = std::filesystem::path(argv[1]).parent_path();
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!DetourCreateProcessWithDllExW(argv[1], command.data(), nullptr, nullptr, FALSE,
        DEBUG_ONLY_THIS_PROCESS, nullptr, directory.c_str(), &startup, &process, loader.c_str(), nullptr)) {
        record("CREATE_FAILED " + std::to_string(GetLastError()));
        return 1;
    }
    record("PID " + std::to_string(process.dwProcessId));
    const auto started = GetTickCount64();
    bool terminated = false;
    for (;;) {
        if (!terminated && GetTickCount64() - started > 20000) {
            record("TIMEOUT terminating diagnostic child");
            TerminateProcess(process.hProcess, 124);
            terminated = true;
        }
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 100)) {
            if (GetLastError() != ERROR_SEM_TIMEOUT) break;
            continue;
        }
        DWORD action = DBG_CONTINUE;
        HANDLE file = nullptr;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) file = event.u.CreateProcessInfo.hFile;
        if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) file = event.u.LoadDll.hFile;
        if (file) {
            std::vector<wchar_t> path(32768);
            const auto count = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), 0);
            if (count && count < path.size()) record("LOAD " + std::filesystem::path(std::wstring(path.data(), count)).u8string());
            CloseHandle(file);
        }
        if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) CloseHandle(event.u.CreateThread.hThread);
        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const auto code = event.u.Exception.ExceptionRecord.ExceptionCode;
            record("EXCEPTION " + std::to_string(code) + " first=" + std::to_string(event.u.Exception.dwFirstChance) +
                " address=" + std::to_string(reinterpret_cast<std::uintptr_t>(event.u.Exception.ExceptionRecord.ExceptionAddress)));
            if (code != EXCEPTION_BREAKPOINT) action = DBG_EXCEPTION_NOT_HANDLED;
        }
        const auto exiting = event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT;
        if (exiting) record("EXIT " + std::to_string(event.u.ExitProcess.dwExitCode));
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, action);
        if (exiting) break;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
