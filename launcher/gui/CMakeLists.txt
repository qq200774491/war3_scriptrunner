# ScriptRoom 图形界面。
#
# scriptroom_gui_logic：与界面库无关的状态与逻辑（日志模型、配置草稿、主题、
#                       关闭魔兽状态机、后台任务、PNG 编码），由测试直接链接。
# ScriptRoom：BUI（Blender 界面库，C API）实现的 Windows GUI 程序。
#
# BUI 通过 FetchContent 获取；离线或想复用本地克隆时有两种等价方式：
#   -DSCRIPTROOM_BUI_SOURCE_DIR=<BUI 源码目录>
#   -DFETCHCONTENT_SOURCE_DIR_BUI=<BUI 源码目录>

file(GLOB SCRIPTROOM_GUI_LOGIC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/logic/*.cpp")
add_library(scriptroom_gui_logic STATIC ${SCRIPTROOM_GUI_LOGIC_SOURCES})
target_include_directories(scriptroom_gui_logic PUBLIC logic)
target_link_libraries(scriptroom_gui_logic PUBLIC scriptroom_core)

include(FetchContent)

set(SCRIPTROOM_BUI_SOURCE_DIR "" CACHE PATH "已有的 BUI 源码目录；为空则从 GitHub 获取")
if(SCRIPTROOM_BUI_SOURCE_DIR)
  set(FETCHCONTENT_SOURCE_DIR_BUI "${SCRIPTROOM_BUI_SOURCE_DIR}" CACHE PATH "" FORCE)
endif()

# 只要 C 内核、软件渲染和系统窗口：关掉测试、示例、绑定，以及用不到的图片格式、视频、
# GPU 后端和文件浏览器（文件选择走系统对话框）。
foreach(Option
    BUI_BUILD_TESTS BUI_BUILD_PYTHON_BINDINGS BUI_BUILD_IMGUI BUI_BUILD_EXAMPLES BUI_BUILD_QT_BACKEND
    BUI_BUILD_LUA_BINDINGS
    BUI_IMAGE_PNG BUI_IMAGE_GIF BUI_IMAGE_BMP BUI_IMAGE_TGA BUI_IMAGE_QOI BUI_IMAGE_JPEG BUI_IMAGE_WEBP
    BUI_IMAGE_HDR BUI_IMAGE_PNM BUI_VIDEO BUI_FILEBROWSER)
  set(${Option} OFF CACHE BOOL "ScriptRoom 不需要" FORCE)
endforeach()

FetchContent_Declare(bui
  GIT_REPOSITORY https://github.com/ppmpreetham/BUI
  GIT_TAG e9783edceeac595302c630ebbb650391bc8bb41d)
FetchContent_MakeAvailable(bui)

file(GLOB SCRIPTROOM_GUI_VIEW_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/view/*.cpp")
add_executable(ScriptRoom WIN32 Main.cpp ${SCRIPTROOM_GUI_VIEW_SOURCES} "${SCRIPTROOM_RESOURCE_DIR}/ScriptRoom.rc")
target_include_directories(ScriptRoom PRIVATE view "${SCRIPTROOM_RESOURCE_DIR}")
target_compile_definitions(ScriptRoom PRIVATE SCRIPTROOM_VERSION="${PROJECT_VERSION}")
target_link_libraries(ScriptRoom PRIVATE scriptroom_gui_logic bui::kit shell32 ole32 uuid user32 comctl32 dwmapi)
target_link_options(ScriptRoom PRIVATE /MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/../app.manifest)
