cmake_minimum_required(VERSION 3.20)
project(WinTetris LANGUAGES CXX)

add_executable(tetris WIN32
    audio.cpp
    audio.h
    game.cpp
    game.h
    main.cpp
)

target_compile_features(tetris PRIVATE cxx_std_17)
target_compile_definitions(tetris PRIVATE UNICODE _UNICODE)
target_link_libraries(tetris PRIVATE user32 gdi32 msimg32 shell32 ole32 dwmapi winmm)

if (MSVC)
    target_compile_options(tetris PRIVATE /W4)
endif()
