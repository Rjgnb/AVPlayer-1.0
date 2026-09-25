# 统一的编译警告设置；AVPLAYER_WARNINGS_AS_ERRORS=ON 时可当门禁用
function(avplayer_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        target_compile_options(${target} PRIVATE /wd4100)   # 未使用形参（接口实现中很常见）
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(AVPLAYER_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# 让静态库的 public include 目录写起来短一点
function(avplayer_set_includes target)
    target_include_directories(${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
endfunction()