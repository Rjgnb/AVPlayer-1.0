# av_tests：不依赖任何测试框架（自带 60 行的 TestHarness）
add_executable(av_tests
    TestHarness.cpp
    test_core_queue.cpp
    test_core_clock.cpp
    test_output_null.cpp
    test_media.cpp
    test_ui_overlay.cpp
    test_ui_font.cpp
    test_player_e2e.cpp
    test_player_input.cpp)

avplayer_set_warnings(av_tests)

# AVPLAYER_TEST_MEDIA_DIR 指到仓库根（样例视频所在位置），端到端测试用它。
# 找不到文件时测试会自动跳过并打印提示。
set(AVPLAYER_TEST_MEDIA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "目录（含样例视频）")

target_compile_definitions(av_tests PRIVATE
    AVPLAYER_TEST_MEDIA_DIR="${AVPLAYER_TEST_MEDIA_DIR}")

# av::media 只在测试里用到（验证解封装/解码/pits），生产代码里只有 avplayer 能碰它
target_link_libraries(av_tests PRIVATE av::player av::ui av::output av::core av::media)

if(AVPLAYER_BUILD_SDL_BACKEND)
    include(CTest)
endif()

add_test(NAME av_tests COMMAND av_tests)
set_target_properties(av_tests PROPERTIES FOLDER "avplayer/tests")
