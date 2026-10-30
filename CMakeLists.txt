cmake_minimum_required(VERSION 3.20)
project(Silk LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Silk — a modern immediate-mode UI framework for Win32 + DirectX 11.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)   # let option() honour the cache values below
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT WIN32)
    message(FATAL_ERROR "Silk targets Win32 + DirectX 11 only.")
endif()

# --- FreeType (vendored, minimal: no zlib/png/bzip2/harfbuzz/brotli) -------
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON CACHE BOOL "" FORCE)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE) # FreeType uses an old cmake_minimum_required
add_subdirectory(third_party/freetype EXCLUDE_FROM_ALL)
if(MSVC)
    # Third-party code: silence its warnings so our build output stays clean.
    target_compile_options(freetype PRIVATE /W0)
endif()

# --- Silk library ----------------------------------------------------------
file(GLOB_RECURSE SILK_SOURCES CONFIGURE_DEPENDS src/silk/*.cpp src/silk/*.h)
add_library(silk STATIC ${SILK_SOURCES})
target_include_directories(silk PUBLIC src)
target_include_directories(silk SYSTEM PRIVATE third_party/stb)
target_link_libraries(silk PUBLIC freetype d3d11 dxgi d3dcompiler windowscodecs imm32)
target_compile_definitions(silk PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(MSVC)
    target_compile_options(silk PUBLIC /utf-8 /permissive- /Zc:__cplusplus)
    target_compile_options(silk PRIVATE /W4 /WX /external:W0 /external:anglebrackets)
endif()

# --- Demo application ------------------------------------------------------
add_executable(silk_demo WIN32 demo/main.cpp demo/demo_app.cpp demo/demo_app.h)
target_link_libraries(silk_demo PRIVATE silk dcomp dwmapi)
if(MSVC)
    target_compile_options(silk_demo PRIVATE /W4 /WX)
endif()

# Copy runtime assets (icon font) next to the executable.
add_custom_command(TARGET silk_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:silk_demo>/assets)
