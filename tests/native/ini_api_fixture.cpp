#include <Windows.h>
extern "C" DWORD __cdecl fixture_get(LPCSTR section, LPCSTR key, LPCSTR fallback,
                                    LPSTR output, DWORD size, LPCSTR path) {
    return GetPrivateProfileStringA(section, key, fallback, output, size, path);
}
extern "C" UINT __cdecl fixture_int(LPCSTR section, LPCSTR key, INT fallback, LPCSTR path) {
    return GetPrivateProfileIntA(section, key, fallback, path);
}
extern "C" BOOL __cdecl fixture_write(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR path) {
    return WritePrivateProfileStringA(section, key, value, path);
}
