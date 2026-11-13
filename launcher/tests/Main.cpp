// 测试入口：无参数运行全部用例；传入名称片段只运行匹配的用例。
// 同一可执行文件兼作进程测试夹具：`--fixture-wait [就绪文件]`。

#include "Config.h"
#include "Text.h"
#include "TestHarness.h"
#include "Win32.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>

#include <aclapi.h>

namespace testing {

std::vector<Case> &registry() {
    static std::vector<Case> Cases;
    return Cases;
}

std::filesystem::path project_root() {
    return scriptroom::find_root();
}

TempDirectory::TempDirectory() {
    static std::atomic<unsigned> Counter{0};
    std::random_device Device;
    Path = std::filesystem::temp_directory_path() /
           (L"scriptroom-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(Counter++) + L"-" +
            std::to_wstring(Device()));
    std::filesystem::create_directories(Path);
}

TempDirectory::~TempDirectory() {
    std::error_code Ignored;
    std::filesystem::remove_all(Path, Ignored);
}

} // namespace testing

namespace {

/// 夹具：收紧自身进程的访问控制（外部无法再 OpenProcess），写入就绪文件后等待。
int run_fixture(int Argc, wchar_t **Argv) {
    if (Argc >= 3) {
        // 空（非 NULL）DACL 拒绝一切外部访问，用来复现“直接 OpenProcess 失败（错误码 5）”。
        ACL Acl{};
        SECURITY_DESCRIPTOR Descriptor{};
        const bool Denied = InitializeAcl(&Acl, sizeof(Acl), ACL_REVISION) &&
                            InitializeSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION) &&
                            SetSecurityDescriptorDacl(&Descriptor, TRUE, &Acl, FALSE) &&
                            SetKernelObjectSecurity(GetCurrentProcess(), DACL_SECURITY_INFORMATION, &Descriptor);
        if (!Denied) {
            return 3;
        }
        {
            std::ofstream Ready(Argv[2], std::ios::binary | std::ios::trunc);
            Ready << GetCurrentProcessId();
        } // 先关闭文件再等待，确保就绪文件内容已落盘
    }
    Sleep(60000);
    return 0;
}

} // namespace

int wmain(int Argc, wchar_t **Argv) {
    if (Argc >= 2 && std::wstring_view(Argv[1]) == L"--fixture-wait") {
        return run_fixture(Argc, Argv);
    }
    const std::string Filter = Argc >= 2 ? scriptroom::to_utf8(Argv[1]) : std::string();
    SetConsoleOutputCP(CP_UTF8);

    int Passed = 0;
    int Failed = 0;
    int Skipped = 0;
    for (const testing::Case &Test : testing::registry()) {
        if (!Filter.empty() && Test.Name.find(Filter) == std::string::npos) {
            continue;
        }
        const auto Start = std::chrono::steady_clock::now();
        try {
            Test.Body();
            ++Passed;
            std::printf("[ 通过 ] %s (%lld ms)\n", Test.Name.c_str(),
                        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   std::chrono::steady_clock::now() - Start)
                                                   .count()));
        } catch (const testing::Skipped &Reason) {
            ++Skipped;
            std::printf("[ 跳过 ] %s：%s\n", Test.Name.c_str(), Reason.what());
        } catch (const std::exception &Reason) {
            ++Failed;
            std::printf("[ 失败 ] %s：%s\n", Test.Name.c_str(), Reason.what());
        }
        std::fflush(stdout);
    }
    std::printf("\n通过 %d，失败 %d，跳过 %d\n", Passed, Failed, Skipped);
    return Failed == 0 ? 0 : 1;
}
