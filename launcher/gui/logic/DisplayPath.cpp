#include "DisplayPath.h"

#include <algorithm>

namespace scriptroom::gui {

std::string display_path(std::string_view Path) {
    std::string Result(Path);
    std::replace(Result.begin(), Result.end(), '/', '\\');
    return Result;
}

std::string edited_path(std::string_view Shown, std::string_view Original) {
    return Shown == display_path(Original) ? std::string(Original) : std::string(Shown);
}

} // namespace scriptroom::gui
