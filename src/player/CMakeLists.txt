# avplayer：编排层（VS: avplayer；会话 + 三条后台线程 + 门面）
#
# 依赖是分级的：
#   PUBLIC  av::core / av::output  -> 因为 Player.h 里出现了它们的类型（接口的一部分）
#   PRIVATE av::media              -> FFmpeg 适配层只在 .cpp 里用
# 于是 app 只包含 Player.h 是**编译不过** media 的：想直接调 FFmpeg 就得显式加依赖，
# 摩擦即约束（这是"依赖方向被构建系统守住"的关键一招）。
add_library(avplayer STATIC
    src/SharedState.cpp
    src/PacketPump.cpp
    src/AudioPipeline.cpp
    src/VideoPipeline.cpp
    src/Player.cpp)

add_library(av::player ALIAS avplayer)

avplayer_set_includes(avplayer)
target_include_directories(avplayer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
avplayer_set_warnings(avplayer)

target_link_libraries(avplayer
    PUBLIC  av::core av::output
    PRIVATE av::media)

if(MSVC)
    target_compile_options(avplayer PRIVATE /MP)
endif()

set_target_properties(avplayer PROPERTIES FOLDER "avplayer/player")
