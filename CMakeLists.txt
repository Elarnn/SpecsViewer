cmake_minimum_required(VERSION 3.16)
project(SpecsViewer VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_GENERATOR_PLATFORM x64)

if(NOT WIN32)
    message(FATAL_ERROR "This project can only be built on Windows.")
endif()

set(ICON_PATH "${CMAKE_SOURCE_DIR}/resources/icons/favicon.ico")
enable_language(RC)

configure_file(
    ${CMAKE_SOURCE_DIR}/resources/rc/app_icon.rc.in
    ${CMAKE_BINARY_DIR}/app_icon.rc
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/resources/rc/version_info.rc.in
    ${CMAKE_BINARY_DIR}/version_info.rc
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/src/app_version.h.in
    ${CMAKE_BINARY_DIR}/generated/app_version.h
    @ONLY
)

# Автосбор всех исходников в src (ui, core, pages, ...) + glad.c
file(GLOB_RECURSE SRC_APP CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/libs/GLAD/src/*.c"
)

add_executable(specsviewer
    ${SRC_APP}
    ${CMAKE_BINARY_DIR}/app_icon.rc
    ${CMAKE_BINARY_DIR}/version_info.rc
)
add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/resources/icons"
    "$<TARGET_FILE_DIR:specsviewer>/resources/icons"
)

add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/resources/fonts"
    "$<TARGET_FILE_DIR:specsviewer>/resources/fonts"
)

add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/resources/db"
    "$<TARGET_FILE_DIR:specsviewer>/resources/db"
)

add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/resources/jsons"
    "$<TARGET_FILE_DIR:specsviewer>/resources/jsons"
)

target_include_directories(specsviewer PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/glad/include
    ${CMAKE_SOURCE_DIR}/libs/GLFW/include
    ${CMAKE_SOURCE_DIR}/libs/Nuklear
    ${CMAKE_SOURCE_DIR}/libs/NVAPI
    ${CMAKE_SOURCE_DIR}/libs/WinRing0
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}/src/ui
)

target_link_directories(specsviewer PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/GLFW/lib-mingw-w64
)

target_link_libraries(specsviewer PRIVATE
    glfw3
    opengl32
    gdi32
    kernel32
)

set_target_properties(specsviewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "specsviewer"
)

# включаем фолбэк
target_compile_definitions(specsviewer PRIVATE CPUINFO_ENABLE_WMI)

# линки для сбора данных
target_link_libraries(specsviewer PRIVATE
    ole32
    oleaut32
    wbemuuid
    setupapi
    setupapi
    advapi32
    bcrypt
    version
    cfgmgr32
    wintrust
    crypt32
    powrprof
    winhttp
    windowscodecs
    comdlg32
)


if(MINGW)
    target_link_options(specsviewer PRIVATE -mwindows) # -mwindows # -mconsole

    target_link_options(specsviewer PRIVATE
        -static-libgcc
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
    )
endif()

add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/libs/GLFW/lib-mingw-w64/glfw3.dll"
    $<TARGET_FILE_DIR:specsviewer>
)

add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/libs/WinRing0/WinRing0x64.dll"
    $<TARGET_FILE_DIR:specsviewer>
)

add_custom_command(TARGET specsviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/libs/WinRing0/WinRing0x64.sys"
    $<TARGET_FILE_DIR:specsviewer>
)

# Для удобного дерева файлов в IDE
source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SRC_APP})
