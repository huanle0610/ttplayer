cmake_minimum_required(VERSION 3.20)

project(TTPlayerClassicCpp LANGUAGES CXX RC)

add_library(ttplayer_classic_core
    src/desktop_lyric_model.cpp
    src/equalizer_model.cpp
    src/lyric_mode.cpp
    src/playlist_model.cpp
    src/skin_definition.cpp
    src/skin_package.cpp
    src/skin_selection.cpp
    src/window_layout.cpp
)

target_compile_features(ttplayer_classic_core PUBLIC cxx_std_20)

target_include_directories(ttplayer_classic_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ttplayer_classic_core PUBLIC ntdll)

target_compile_definitions(ttplayer_classic_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

add_executable(ttplayer_classic WIN32
    src/main.cpp
    resources/app.rc
)

target_include_directories(ttplayer_classic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)
target_link_libraries(ttplayer_classic PRIVATE
    ttplayer_classic_core
    user32
    gdi32
    comctl32
    shell32
    msimg32
    winmm
)

add_custom_command(TARGET ttplayer_classic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/skins
        $<TARGET_FILE_DIR:ttplayer_classic>/skins
)

add_executable(ttplayer_classic_tests
    tests/test_skin_package.cpp
)

target_link_libraries(ttplayer_classic_tests PRIVATE ttplayer_classic_core)

enable_testing()
add_test(NAME ttplayer_classic_tests COMMAND ttplayer_classic_tests)

if(MSVC)
    target_compile_options(ttplayer_classic_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(ttplayer_classic PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_options(ttplayer_classic_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(ttplayer_classic_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ttplayer_classic PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ttplayer_classic_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()






