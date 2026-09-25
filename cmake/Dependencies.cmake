# ---------------------------------------------------------------------------
# 第三方依赖：只在这里出现"具体 SDK 路径"，
# 其余层一律通过 imported target 使用（FFmpeg::avcodec / SDL2::SDL2）。
# 好处：换 SDK 位置 / 换平台只改这一个文件。
# ---------------------------------------------------------------------------

set(AVPLAYER_FFMPEG_INCLUDE_DIR "${AVPLAYER_SDK_ROOT}/include")
set(AVPLAYER_FFMPEG_LIB_DIR     "${AVPLAYER_SDK_ROOT}/lib")

if(NOT EXISTS "${AVPLAYER_FFMPEG_INCLUDE_DIR}/libavcodec/avcodec.h")
    message(FATAL_ERROR
        "找不到 FFmpeg 头文件：${AVPLAYER_FFMPEG_INCLUDE_DIR}/libavcodec/avcodec.h\n"
        "请用 -DAVPLAYER_SDK_ROOT=<path> 指向正确的 SDK。")
endif()

# 记录 FFmpeg 版本（写进构建信息，便于排查"用错 SDK"）
file(STRINGS "${AVPLAYER_FFMPEG_INCLUDE_DIR}/libavcodec/version_major.h"
     _avcodec_major_line REGEX "#define LIBAVCODEC_VERSION_MAJOR")
string(REGEX MATCH "[0-9]+" AVPLAYER_FFMPEG_AVCODEC_MAJOR "${_avcodec_major_line}")
set(AVPLAYER_FFMPEG_AVCODEC_VERSION "${AVPLAYER_FFMPEG_AVCODEC_MAJOR}.x")

# 有 .def/.lib 用 .lib（MSVC），否则退回 .dll.a（MinGW）
function(avplayer_find_ffmpeg_lib name out_var)
    foreach(suffix ".lib" ".dll.a" ".a")
        set(candidate "${AVPLAYER_FFMPEG_LIB_DIR}/${name}${suffix}")
        if(EXISTS "${candidate}")
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "FFmpeg 库缺失：${AVPLAYER_FFMPEG_LIB_DIR}/${name}(.lib|.dll.a)")
endfunction()

function(avplayer_add_ffmpeg_target target fflib)
    if(NOT TARGET FFmpeg::${target})
        avplayer_find_ffmpeg_lib("${fflib}" _loc)
        add_library(FFmpeg::${target} UNKNOWN IMPORTED GLOBAL)
        set_target_properties(FFmpeg::${target} PROPERTIES
            IMPORTED_LOCATION "${_loc}"
            INTERFACE_INCLUDE_DIRECTORIES "${AVPLAYER_FFMPEG_INCLUDE_DIR}"
            # 第三方头文件当"系统头"处理：不参与本项目 /W4 的警告统计
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${AVPLAYER_FFMPEG_INCLUDE_DIR}")
    endif()
endfunction()

avplayer_add_ffmpeg_target(avutil      avutil)
avplayer_add_ffmpeg_target(avcodec     avcodec)
avplayer_add_ffmpeg_target(avformat    avformat)
avplayer_add_ffmpeg_target(swresample  swresample)
avplayer_add_ffmpeg_target(swscale     swscale)

# SDL2（仅给 output/sdl 后端用）
if(AVPLAYER_BUILD_SDL_BACKEND)
    set(SDL2_INCLUDE_DIR "${AVPLAYER_SDK_ROOT}/include/SDL2")
    foreach(suffix ".lib" ".dll.a" ".a")
        if(EXISTS "${AVPLAYER_SDK_ROOT}/lib/x64/SDL2${suffix}")
            set(SDL2_LIBRARY "${AVPLAYER_SDK_ROOT}/lib/x64/SDL2${suffix}")
        endif()
    endforeach()
    if(NOT EXISTS "${SDL2_INCLUDE_DIR}/SDL.h" OR NOT DEFINED SDL2_LIBRARY)
        message(FATAL_ERROR "找不到 SDL2（${SDL2_INCLUDE_DIR} / ${AVPLAYER_SDK_ROOT}/lib/x64/SDL2.lib）")
    endif()
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 UNKNOWN IMPORTED GLOBAL)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION "${SDL2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}")
    endif()
endif()