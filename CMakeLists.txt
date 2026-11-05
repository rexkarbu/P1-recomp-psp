cmake_minimum_required(VERSION 3.16)
project(persona_pc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-O2 -foptimize-sibling-calls -fomit-frame-pointer -w)
add_compile_definitions(PSPRECOMP_REAL_THREADS=1)

file(GLOB RECOMP_SRCS CONFIGURE_DEPENDS "recomp_src/*.c")

add_executable(persona_pc
    src/main.c
    src/hle_custom.c
    ${RECOMP_SRCS}
)

target_include_directories(persona_pc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/psprecomp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/recomp_src
)

target_link_libraries(persona_pc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/psprecomp/build/libpsprecomp.a
    gdi32
    opengl32
    xinput
    winmm
)

add_executable(persona_launcher WIN32
    src/launcher/main.c
    src/launcher/iso_extractor.c
)

target_include_directories(persona_launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/launcher
)

target_link_libraries(persona_launcher PRIVATE
    gdi32
    comdlg32
    shell32
    user32
    advapi32
)
