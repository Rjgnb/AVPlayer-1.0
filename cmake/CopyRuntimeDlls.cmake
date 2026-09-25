# 把运行期需要的 DLL 拷到可执行文件旁边（Windows 专属，其它平台 no-op）
function(avplayer_copy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    set(_dlls)
    foreach(_name SDL2 avcodec avformat avutil swresample swscale)
        file(GLOB _found "${AVPLAYER_SDK_ROOT}/bin/${_name}*.dll")
        list(APPEND _dlls ${_found})
    endforeach()
    if(_dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dlls} "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copy runtime DLLs -> $<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()