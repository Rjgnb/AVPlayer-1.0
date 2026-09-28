# avmedia：FFmpeg 适配层（VS 项目名也是 avmedia）
# 规则：可以用 FFmpeg，不可以用 SDL（更不许出现在 output/ui 里）。
add_library(avmedia STATIC
    src/FFmpegUtil.cpp
    src/Frame.cpp
    src/Demuxer.cpp
    src/Decoder.cpp
    src/AudioResampler.cpp
    src/VideoConverter.cpp)

add_library(av::media ALIAS avmedia)

avplayer_set_includes(avmedia)
avplayer_set_warnings(avmedia)

# FFmpeg 的类型会出现在本层的公开头文件里，所以头文件目录必须 PUBLIC；
# 但"只允许 media 调用 FFmpeg 函数"这条约定由代码评审与依赖方向保证。
target_link_libraries(avmedia
    PUBLIC  av::core
    PUBLIC  FFmpeg::avutil FFmpeg::avcodec FFmpeg::avformat FFmpeg::swresample FFmpeg::swscale)

if(MSVC)
    target_compile_options(avmedia PRIVATE /MP)
endif()

set_target_properties(avmedia PROPERTIES FOLDER "avplayer/media")
