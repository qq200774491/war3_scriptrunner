cmake_minimum_required(VERSION 3.24)
project(scriptroom_launcher LANGUAGES C CXX VERSION 0.3.0)

# 启动器是 64 位程序；32 位的脚本宿主、魔兽插件由上级目录的 CMakeLists 单独构建（Win32）。
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "ScriptRoom launcher must be configured for Windows x64.")
endif()

# 与原生组件一致：静态运行库，便携包不依赖 VC++ 运行库安装。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCRIPTROOM_BUILD_GUI "Build the BUI based ScriptRoom.exe" ON)
option(SCRIPTROOM_BUILD_TESTS "Build the launcher tests" ON)

set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")

# SQLite 合并源码：全局存档使用，无需动态加载扩展。
add_library(sqlite3_static STATIC "${THIRD_PARTY_DIR}/sqlite3/sqlite3.c")
target_include_directories(sqlite3_static PUBLIC "${THIRD_PARTY_DIR}")
target_compile_definitions(sqlite3_static PRIVATE
  SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_USE_URI=0)
target_compile_options(sqlite3_static PRIVATE /w)

file(GLOB SCRIPTROOM_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp")
add_library(scriptroom_core STATIC ${SCRIPTROOM_CORE_SOURCES})
target_include_directories(scriptroom_core PUBLIC core "${THIRD_PARTY_DIR}")
target_compile_definitions(scriptroom_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(scriptroom_core PUBLIC /W4 /WX /EHsc /utf-8)
target_link_libraries(scriptroom_core PUBLIC sqlite3_static ws2_32 bcrypt shell32)

# 两个 EXE 共用的 Windows 资源（程序图标）；图标的矢量源文件是同目录的 ScriptRoom.svg。
set(SCRIPTROOM_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")

add_executable(ScriptRoomCLI cli/main.cpp "${SCRIPTROOM_RESOURCE_DIR}/ScriptRoom.rc")
target_link_libraries(ScriptRoomCLI PRIVATE scriptroom_core)
target_link_options(ScriptRoomCLI PRIVATE /MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest)

# 界面先于测试：测试要链接界面的纯逻辑库 scriptroom_gui_logic。
if(SCRIPTROOM_BUILD_GUI)
  add_subdirectory(gui)
endif()

if(SCRIPTROOM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
