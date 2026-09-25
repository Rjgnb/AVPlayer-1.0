# avui：覆盖层（进度条/时间/倍速/点击暂停）
# 只依赖 core 与 output 的**接口**，不认识 FFmpeg，也不认识 SDL。
add_library(avui STATIC
    src/BitmapFont.cpp
    src/OverlayController.cpp
    src/OverlayRenderer.cpp)

add_library(av::ui ALIAS avui)

avplayer_set_includes(avui)
avplayer_set_warnings(avui)

target_link_libraries(avui PUBLIC av::core av::output)

if(MSVC)
    target_compile_options(avui PRIVATE /MP)
endif()

set_target_properties(avui PROPERTIES FOLDER "avplayer/ui")