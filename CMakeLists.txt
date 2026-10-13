cmake_minimum_required(VERSION 3.24)
project(r1q2_standalone LANGUAGES C)
option(R1Q2_CORE_RENDERER "Build the optional OpenGL core renderer" ON)
option(R1Q2_CHAT_TRANSLATOR "Build the online chat translator" ON)
option(R1Q2_HTTP_DOWNLOADS "Build HTTP downloads with static curl and Windows Schannel" ON)
option(R1Q2_AVCODEC "Enable local media using an explicit static MSVC FFmpeg prefix" OFF)
set(R1Q2_FFMPEG_ROOT "" CACHE PATH "Native x86 /MT static FFmpeg installation prefix")
option(R1Q2_OPENAL "Build the optional static OpenAL sound backend" OFF)
set(R1Q2_OPENAL_ROOT "" CACHE PATH "Native x86 /MT static OpenAL installation prefix")

# This target supplements the original Unix and Visual C++ DLL builds.
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The standalone client requires MSVC targeting Win32 (x86). Use the original build recipes for other targets.")
endif()
enable_language(RC)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

# Check explicitly supplied codecs before any dependency download/configuration.
if(R1Q2_AVCODEC)
    if(NOT R1Q2_FFMPEG_ROOT)
        message(FATAL_ERROR "R1Q2_AVCODEC requires an explicit R1Q2_FFMPEG_ROOT prefix.")
    endif()
    string(TOUPPER "${CMAKE_BUILD_TYPE};${CMAKE_CONFIGURATION_TYPES}" ffmpeg_configurations)
    if("DEBUG" IN_LIST ffmpeg_configurations)
        message(FATAL_ERROR "R1Q2_AVCODEC requires /MT; Debug uses /MTd. Select only non-Debug configurations (for example -DCMAKE_CONFIGURATION_TYPES=Release).")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_static_ffmpeg.py"
            --prefix "${R1Q2_FFMPEG_ROOT}" --compiler "${CMAKE_C_COMPILER}"
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-check"
        RESULT_VARIABLE ffmpeg_result OUTPUT_VARIABLE ffmpeg_paths ERROR_VARIABLE ffmpeg_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT ffmpeg_result EQUAL 0)
        message(FATAL_ERROR "${ffmpeg_error}")
    endif()
    string(REPLACE "\r" "" ffmpeg_paths "${ffmpeg_paths}")
    string(REPLACE "\n" ";" ffmpeg_libraries "${ffmpeg_paths}")
    list(POP_FRONT ffmpeg_libraries ffmpeg_include)
endif()
if(R1Q2_OPENAL)
    if(NOT R1Q2_OPENAL_ROOT)
        message(FATAL_ERROR "R1Q2_OPENAL requires an explicit R1Q2_OPENAL_ROOT prefix.")
    endif()
    string(TOUPPER "${CMAKE_BUILD_TYPE};${CMAKE_CONFIGURATION_TYPES}" openal_configurations)
    if("DEBUG" IN_LIST openal_configurations)
        message(FATAL_ERROR "R1Q2_OPENAL requires /MT; select only non-Debug configurations (for example -DCMAKE_CONFIGURATION_TYPES=Release).")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_static_openal.py"
            --prefix "${R1Q2_OPENAL_ROOT}" --compiler "${CMAKE_C_COMPILER}"
            --output-dir "${CMAKE_CURRENT_BINARY_DIR}/openal-check"
        RESULT_VARIABLE openal_result OUTPUT_VARIABLE openal_paths ERROR_VARIABLE openal_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT openal_result EQUAL 0)
        message(FATAL_ERROR "${openal_error}")
    endif()
    string(REPLACE "\r" "" openal_paths "${openal_paths}")
    string(REPLACE "\n" ";" openal_libraries "${openal_paths}")
    list(POP_FRONT openal_libraries openal_include)
endif()
include(FetchContent)

# Source archives are pinned and verified; no Git checkout is needed.
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    URL https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.2
    URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(zlib)

# libpng's FindZLIB must use this build's static target and generated headers.
add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR}" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY zlibstatic CACHE STRING "" FORCE)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(png
    URL https://codeload.github.com/pnggroup/libpng/tar.gz/refs/tags/v1.6.55
    URL_HASH SHA256=71a2c5b1218f60c4c6d2f1954c7eb20132156cae90bdb90b566c24db002782a6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(png)

if(R1Q2_HTTP_DOWNLOADS)
    # Retain HTTP/HTTPS, compression and proxy support without application DLLs.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
    set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)
    set(SHARE_LIB_OBJECT OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_TARGET_WINDOWS_VERSION "0x0601" CACHE STRING "" FORCE)
    set(CURL_ZLIB ON CACHE STRING "" FORCE)
    set(CURL_BROTLI OFF CACHE STRING "" FORCE)
    set(CURL_ZSTD OFF CACHE STRING "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_CMAKECONFIG OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.22.0.tar.xz
        URL_HASH SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(curl)
endif()

include(ExternalProject)
set(jpeg_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/jpeg-source")
set(jpeg_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/jpeg-build")
if(CMAKE_CONFIGURATION_TYPES)
    set(jpeg_LIBRARY "${jpeg_BINARY_DIR}/$<CONFIG>/jpeg-static.lib")
else()
    set(jpeg_LIBRARY "${jpeg_BINARY_DIR}/jpeg-static.lib")
endif()
# libjpeg-turbo explicitly requires a separate CMake project.
ExternalProject_Add(r1q2_jpeg
    URL https://codeload.github.com/libjpeg-turbo/libjpeg-turbo/tar.gz/refs/tags/3.2.0
    URL_HASH SHA256=980dd81f425082aa6d7c9e47fef27554ce7a9ffc8e2f6e863b97d263c5c50858
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_DIR "${jpeg_SOURCE_DIR}"
    BINARY_DIR "${jpeg_BINARY_DIR}"
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON
        -DWITH_CRT_DLL=OFF -DWITH_TURBOJPEG=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
        --config $<CONFIG> --target jpeg-static
    BUILD_BYPRODUCTS "${jpeg_LIBRARY}"
    INSTALL_COMMAND "")

add_executable(r1q2 WIN32
    client/cl_cin.c
    client/cl_http.c
    client/cl_media.c
    client/cl_movie.c
    client/cl_dll.c
    client/cl_ents.c
    client/cl_fx.c
    client/cl_input.c
    client/cl_inv.c
    client/cl_main.c
    client/cl_timing.c
    client/cl_translate.c
    client/jump/jump_commands.c
    client/jump/jump_config.c
    client/jump/sh_nerdstats.c
    client/jump/sh_netmeter.c
    client/jump/jump_network.c
    client/jump/jump_widgets.c
	client/jump/jump_layout.c
	client/jump/jump_layout_native.c
	client/jump/jump_editor.c
    client/jump/jump_ui_compat.c
    client/jump/jump_menu.c
    client/jump/jump_script.c
    client/jump/jump_ui.c
    client/jump/jump_builtin_menu.c
    client/jump/jump_menu_services.c
    client/jump/jump_sh_commands.c
    client/jump/jump_playerconfig.c
    client/jump/jump_playermodels.c
    client/jump/jump_demos.c
    client/jump/jump_display.c
    client/jump/jump_video_ui.c
    client/jump/jump_browser_ui.c
    client/jump/jump_servers.c
    client/jump/jump_servers_http.c
    client/jump/jump_field.c
    client/jump/jump_reminders.c
    client/jump/jump_beams.c
    client/jump/jump_draw.c
    client/jump/strafe_helper_draw.c
    client/jump/strafe_efficiency.c
    client/jump/strafe_helper_customization.c
    client/jump/sh_efficiency_draw.c
    client/jump/jump_input.c
    client/jump/jump_prediction.c
    client/cl_newfx.c
    client/cl_parse.c
    client/cl_pred.c
    client/cl_scrn.c
    client/cl_tent.c
    client/cl_view.c
    client/con_prompt.c
    client/console.c
    client/keys.c
    client/le_physics.c
    client/le_util.c
    client/menu.c
    client/qmenu.c
    client/snd_dma.c
    client/snd_mem.c
    client/snd_ogg.c
    client/snd_stream.c
    client/snd_music.c
    client/snd_music_al.c
    client/snd_movie_al.c
    client/snd_mix.c
    game/m_flash.c
    game/q_shared.c
    qcommon/cmd.c
    qcommon/cmodel.c
    qcommon/common.c
    qcommon/crc.c
    qcommon/cvar.c
    qcommon/files.c
    qcommon/md4.c
    qcommon/mersennetwister.c
    qcommon/net_chan.c
    qcommon/pmove.c
    qcommon/redblack.c
    qcommon/unzip.c
    qcommon/ioapi.c
    server/sv_ccmds.c
    server/sv_ents.c
    server/sv_game.c
    server/sv_init.c
    server/sv_main.c
    server/sv_send.c
    server/sv_user.c
    server/sv_world.c
    win32/alw_win.c
    win32/cd_win.c
    win32/conproc.c
    win32/in_win.c
    win32/net_wins.c
    win32/q_shwin.c
    win32/qal_win.c
    win32/snd_win.c
    win32/sys_win.c
    win32/vid_dll.c
    win32/vid_menu.c
    win32/glw_imp.c
    win32/glw_display.c
    win32/qgl_win.c
    win32/q2.rc
    ref_gl/gl_jump.c
    ref_gl/gl_jump_lava.c
    ref_gl/gl_jump_beams.c
    ref_gl/gl_core.c
    ref_gl/gl_core_state.c
    ref_gl/gl_core_shader.c
    ref_gl/gl_shader.c
    ref_gl/gl_postfx.c
    ref_gl/gl_draw.c
    ref_gl/gl_image.c
    ref_gl/gl_hqx.c
    ref_gl/gl_light.c
    ref_gl/gl_mesh.c
    ref_gl/gl_model.c
    ref_gl/gl_rmain.c
    ref_gl/gl_rmisc.c
    ref_gl/gl_rsurf.c
    ref_gl/gl_warp.c)
set_target_properties(r1q2 PROPERTIES OUTPUT_NAME r1q2_race)
add_custom_target(race_version
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_version.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${CMAKE_CURRENT_BINARY_DIR}/race_version_generated.h"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/race_version_generated.h"
    VERBATIM)
add_dependencies(r1q2 race_version)
target_include_directories(r1q2 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(r1q2 PRIVATE R1Q2_GENERATED_VERSION)

target_compile_definitions(r1q2 PRIVATE
    WIN32 _WINDOWS REF_HARD_LINKED CINEMATICS _CRT_SECURE_NO_WARNINGS)
target_compile_options(r1q2 PRIVATE $<$<COMPILE_LANGUAGE:C>:/W3;/fp:precise>)
target_include_directories(r1q2 PRIVATE
    win32 "${jpeg_SOURCE_DIR}/src" "${jpeg_BINARY_DIR}")
add_dependencies(r1q2 r1q2_jpeg)
target_link_libraries(r1q2 PRIVATE
    zlibstatic png_static "${jpeg_LIBRARY}"
    winmm wsock32 kernel32 user32 gdi32 advapi32 dxguid dinput8 dsound)

if(R1Q2_HTTP_DOWNLOADS)
    target_compile_definitions(r1q2 PRIVATE USE_CURL)
    target_link_libraries(r1q2 PRIVATE CURL::libcurl_static)
endif()

if(NOT R1Q2_CHAT_TRANSLATOR)
    target_compile_definitions(r1q2 PRIVATE NO_CHAT_TRANSLATOR)
endif()

if(R1Q2_AVCODEC)
    target_compile_definitions(r1q2 PRIVATE USE_AVCODEC)
    target_include_directories(r1q2 PRIVATE "${ffmpeg_include}")
    target_link_libraries(r1q2 PRIVATE ${ffmpeg_libraries})
endif()

if(R1Q2_OPENAL)
    target_compile_definitions(r1q2 PRIVATE USE_OPENAL R1Q2_STATIC_OPENAL AL_LIBTYPE_STATIC)
    target_include_directories(r1q2 PRIVATE "${openal_include}")
    target_link_libraries(r1q2 PRIVATE ${openal_libraries})
endif()

# Establish per-monitor awareness before native windows are created.
target_link_options(r1q2 PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/win32/r1q2.manifest")

# Assets and mod/game DLLs are intentionally supplied by the game installation.
install(TARGETS r1q2 RUNTIME DESTINATION .)

if(R1Q2_CORE_RENDERER)
    target_compile_definitions(r1q2 PRIVATE R1Q2_GL_CORE)
endif()
