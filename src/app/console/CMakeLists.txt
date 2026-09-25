# av_console：控制台宿主
# 它是**唯一**决定"用哪个后端"的地方 —— 依赖方向在这里收口。
add_executable(av_console main.cpp)

avplayer_set_warnings(av_console)
target_include_directories(av_console PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(av_console PRIVATE av::player av::ui avoutput)

if(AVPLAYER_BUILD_SDL_BACKEND)
    target_link_libraries(av_console PRIVATE av::output::sdl)
    target_compile_definitions(av_console PRIVATE AVPLAYER_HAS_SDL=1)
endif()

avplayer_copy_runtime_dlls(av_console)
set_target_properties(av_console PROPERTIES FOLDER "avplayer/app")