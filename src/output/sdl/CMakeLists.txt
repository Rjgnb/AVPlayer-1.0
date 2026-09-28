# avoutput_sdl：SDL2 具体后端（VS: avoutput_sdl；这里才允许包含 <SDL.h>）
add_library(avoutput_sdl STATIC
    src/SdlBackend.cpp
    src/SdlAudioSink.cpp
    src/SdlVideoSink.cpp
    src/SdlEventSource.cpp)

add_library(av::output::sdl ALIAS avoutput_sdl)

avplayer_set_includes(avoutput_sdl)
avplayer_set_warnings(avoutput_sdl)
target_include_directories(avoutput_sdl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(avoutput_sdl
    PUBLIC  av::output
    PRIVATE SDL2::SDL2)

if(MSVC)
    target_compile_options(avoutput_sdl PRIVATE /MP)
endif()

set_target_properties(avoutput_sdl PROPERTIES FOLDER "avplayer/output")
