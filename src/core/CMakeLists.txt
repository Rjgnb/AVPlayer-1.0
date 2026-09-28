# avcore：跨层数据契约 + 基础原语（VS 项目名也是 avcore）
# 规则：不依赖任何第三方（只能用到 C++ 标准库）—— 这条规则让核心可以被单测与复用。
add_library(avcore STATIC
    src/Status.cpp
    src/Types.cpp
    src/InputEvent.cpp
    src/Log.cpp
    src/MediaClock.cpp
    src/StringFormat.cpp)

add_library(av::core ALIAS avcore)

avplayer_set_includes(avcore)
avplayer_set_warnings(avcore)

target_include_directories(avcore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
    target_compile_options(avcore PRIVATE /MP)   # 多进程编译
endif()

set_target_properties(avcore PROPERTIES FOLDER "avplayer/core")
