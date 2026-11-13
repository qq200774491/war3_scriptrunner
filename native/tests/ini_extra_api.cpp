#include <Windows.h>
// 模拟新版平台新增未适配的 Profile API，验证会拒绝启动而非部分回退到共用 INI。
extern "C" __declspec(dllexport) DWORD __cdecl fixture_section(LPSTR output, DWORD size, LPCSTR path) {
    return GetPrivateProfileSectionA("DzAPI", output, size, path);
}
