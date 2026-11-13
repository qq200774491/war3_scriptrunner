#pragma once
#include <Windows.h>
#include <string>

namespace quicktest {
// 只修改指定平台模块的三个 Profile API 导入；安装后随游戏进程存活。
// 目标 INI 必须已由启动器准备完成，安装失败时抛出，由插件入口统一终止测试。
void install_ini_redirect(HMODULE platform, const std::wstring& destination,
                          const std::wstring& audit_path = {});
}
