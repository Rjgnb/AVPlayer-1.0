# avoutput：输出后端的**接口层** + headless(null) 后端 + 注册表
# 规则：不依赖 FFmpeg、不依赖 SDL —— 只依赖 core。
# 具体后端（sdl/qt/alsa...）在各自的子目录里，作为独立 target。
add_library(avoutput STATIC
    src/BackendRegistry.cpp
    src/NullBackend.cpp)

add_library(av::output ALIAS avoutput)

avplayer_set_includes(avoutput)
avplayer_set_warnings(avoutput)

target_link_libraries(avoutput PUBLIC av::core)

if(MSVC)
    target_compile_options(avoutput PRIVATE /MP)
endif()

set_target_properties(avoutput PROPERTIES FOLDER "avplayer/output")