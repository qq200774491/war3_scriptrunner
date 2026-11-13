#include "Fonts.h"

#include "BUI_kit.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <windows.h>

namespace scriptroom::gui {

namespace {

namespace fs = std::filesystem;

fs::path windows_fonts_directory() {
    std::wstring Buffer(MAX_PATH, L'\0');
    const UINT Length = GetWindowsDirectoryW(Buffer.data(), static_cast<UINT>(Buffer.size()));
    if (Length == 0 || Length >= Buffer.size()) {
        return fs::path(L"C:\\Windows") / L"Fonts";
    }
    Buffer.resize(Length);
    return fs::path(Buffer) / L"Fonts";
}

/// 读取候选字体中第一个存在且能读完的文件；都没有时返回空。
std::vector<char> read_first_font(const fs::path &Directory, std::initializer_list<const wchar_t *> Candidates) {
    for (const wchar_t *Name : Candidates) {
        std::ifstream File(Directory / Name, std::ios::binary);
        if (File) {
            return std::vector<char>(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
        }
    }
    return {};
}

bool register_face(const char *Family, const std::vector<char> &Data, int Style) {
    return !Data.empty() && BUI_text_add_font(Family, Style, Data.data(), static_cast<int>(Data.size())) != 0;
}

} // namespace

bool use_system_cjk_font() {
    const fs::path Directory = windows_fonts_directory();
    const bool Regular = register_face("", read_first_font(Directory, {L"msyh.ttc", L"simhei.ttf", L"simsun.ttc"}), 0);
    // 没有粗体字体时 BUI 会用常规字体合成粗体，所以粗体注册失败不算失败。
    register_face("", read_first_font(Directory, {L"msyhbd.ttc"}), BUI_FONT_BOLD);
    register_face(MonoFamily, read_first_font(Directory, {L"CascadiaMono.ttf", L"consola.ttf"}), 0);
    return Regular;
}

} // namespace scriptroom::gui
