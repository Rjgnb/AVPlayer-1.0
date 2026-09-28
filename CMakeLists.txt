cmake_minimum_required(VERSION 3.21)

project(avplayer
        VERSION 0.1.0
        DESCRIPTION "Layered audio/video player (FFmpeg + pluggable output backends)"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 全局设置
# ---------------------------------------------------------------------------
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 单配置生成器（Ninja/Makefiles）下把可执行文件集中输出，便于找 DLL
if(NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# ---------------------------------------------------------------------------
# 选项
# ---------------------------------------------------------------------------
option(AVPLAYER_BUILD_SDL_BACKEND "Build the SDL2 output backend"        ON)
option(AVPLAYER_BUILD_CONSOLE_APP "Build the console demo application"   ON)
option(AVPLAYER_BUILD_TESTS       "Build unit tests"                     ON)
option(AVPLAYER_WARNINGS_AS_ERRORS "Treat warnings as errors"            OFF)

# 第三方 SDK：默认使用仓库内自带的 FFmpeg + SDL2（免安装、可复现）
set(AVPLAYER_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg-dev-sdk"
    CACHE PATH "Path to the vendored FFmpeg/SDL2 development SDK")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(CompilerWarnings)
include(CopyRuntimeDlls)

# ---------------------------------------------------------------------------
# 分层子目录：顺序即依赖方向（只能向下依赖）
#   core  <-  media        (FFmpeg)
#   core  <-  output       (接口 + null 后端)
#   output <- output/sdl  (SDL2)
#   media + output <- player
#   core + output  <- ui
#   player + ui + 具体后端 <- app
#
# 用 Visual Studio 打开 CMake 生成的解决方案时，每个 target 会变成一个
# vcxproj：avcore / avmedia / avoutput / avoutput_sdl / avplayer / avui /
# av_console / av_tests。项目多不是为了让工程变复杂，而是为了让依赖边界
# 可由构建系统证明。完整阅读/调试路线见 docs/VS_PROJECT_GUIDE.md。
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/media)
add_subdirectory(src/output)
if(AVPLAYER_BUILD_SDL_BACKEND)
    add_subdirectory(src/output/sdl)
endif()
add_subdirectory(src/player)
add_subdirectory(src/ui)
if(AVPLAYER_BUILD_CONSOLE_APP)
    add_subdirectory(src/app/console)
endif()
if(AVPLAYER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# 汇总信息
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "avplayer ${PROJECT_VERSION}  (${CMAKE_BUILD_TYPE})")
message(STATUS "  SDK root ....... ${AVPLAYER_SDK_ROOT}")
message(STATUS "  FFmpeg ......... avcodec ${AVPLAYER_FFMPEG_AVCODEC_VERSION}")
message(STATUS "  SDL2 backend ... ${AVPLAYER_BUILD_SDL_BACKEND}")
message(STATUS "  console app .... ${AVPLAYER_BUILD_CONSOLE_APP}")
message(STATUS "  tests .......... ${AVPLAYER_BUILD_TESTS}")
message(STATUS "")
