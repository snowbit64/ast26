cmake_minimum_required(VERSION 3.20)

project(ast26
    VERSION 1.0.0
    DESCRIPTION "Texture converter for Farming Simulator 2026 AST (GS2D v8)"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(AST26_BUILD_CLI  "Build the ast26 CLI executable"  ON)
option(AST26_BUILD_GIMP "Build the GIMP 2.x file plug-in" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /utf-8 /EHsc)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function)
endif()

# ------------------------------------------------------------------
# Third-party: miniz (zlib-compatible) static
# ------------------------------------------------------------------
add_library(ast26_miniz STATIC third_party/miniz/miniz.c)
target_include_directories(ast26_miniz PUBLIC third_party/miniz)
set_target_properties(ast26_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------
# Third-party: bcdec (BC1..BC7 + BC6H block decoders)
# ------------------------------------------------------------------
add_library(ast26_bcdec STATIC third_party/bcdec/bcdec_impl.c)
target_include_directories(ast26_bcdec PUBLIC third_party/bcdec)
set_target_properties(ast26_bcdec PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------
# Third-party: astcenc embedded
# ------------------------------------------------------------------
file(GLOB AST26_ASTCENC_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/astcenc/astcenc_*.cpp")
add_library(ast26_astcenc STATIC ${AST26_ASTCENC_SRC})
target_include_directories(ast26_astcenc PUBLIC third_party/astcenc)
set_target_properties(ast26_astcenc PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64)")
    if(MSVC)
        target_compile_options(ast26_astcenc PRIVATE /arch:AVX2)
    else()
        target_compile_options(ast26_astcenc PRIVATE -mavx2 -mf16c -mpopcnt)
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64|ARM64)")
    if(NOT MSVC)
        target_compile_options(ast26_astcenc PRIVATE -march=armv8-a+crc)
    endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
    target_compile_definitions(ast26_astcenc PRIVATE
        ASTCENC_SSE=20 ASTCENC_AVX=0 ASTCENC_POPCNT=0 ASTCENC_F16C=0 ASTCENC_NEON=0)
endif()

# ------------------------------------------------------------------
# Core static library
# ------------------------------------------------------------------
add_library(ast26_core STATIC
    src/container.cpp
    src/codec.cpp
    src/astc_bridge.cpp
    src/image_io.cpp
    src/dds_io.cpp
    src/color_format.cpp
    src/mipgen.cpp
    src/flip.cpp
    src/api.cpp)
target_include_directories(ast26_core
    PUBLIC  include
    PRIVATE src third_party/stb)
target_link_libraries(ast26_core
    PUBLIC  ast26_astcenc
    PRIVATE ast26_miniz ast26_bcdec)

# ------------------------------------------------------------------
# CLI executable — static linkage
# ------------------------------------------------------------------
if(AST26_BUILD_CLI)
    add_executable(ast26 cli/main.cpp)
    target_link_libraries(ast26 PRIVATE ast26_core)
    set_target_properties(ast26 PROPERTIES OUTPUT_NAME ast26)

    if(MSVC)
        set_property(TARGET ast26 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET ast26_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET ast26_astcenc PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET ast26_miniz PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set_property(TARGET ast26_bcdec PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    elseif(MINGW OR (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND WIN32))
        target_link_options(ast26 PRIVATE
            -static -static-libgcc -static-libstdc++)
    elseif(ANDROID)
        target_link_options(ast26 PRIVATE -static-libstdc++)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(ast26 PRIVATE -static-libgcc -static-libstdc++)
    endif()

    install(TARGETS ast26 RUNTIME DESTINATION bin)
endif()

# ------------------------------------------------------------------
# GIMP 2.x file plug-in (optional, requires libgimp2.0-dev)
# ------------------------------------------------------------------
if(AST26_BUILD_GIMP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIMP REQUIRED gimpui-2.0)

    add_executable(file-ast26 gimp-plugin/file-ast26.cpp)
    target_include_directories(file-ast26 PRIVATE ${GIMP_INCLUDE_DIRS})
    target_link_libraries(file-ast26 PRIVATE ast26_core ${GIMP_LIBRARIES})
    target_link_directories(file-ast26 PRIVATE ${GIMP_LIBRARY_DIRS})
    target_compile_options(file-ast26 PRIVATE ${GIMP_CFLAGS_OTHER})
    set_target_properties(file-ast26 PROPERTIES OUTPUT_NAME file-ast26)

    # Install into the GIMP user plug-ins directory
    execute_process(
        COMMAND gimptool-2.0 --gimpplugindir
        OUTPUT_VARIABLE GIMP_PLUGIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT GIMP_PLUGIN_DIR)
        set(GIMP_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/lib/gimp/2.0")
    endif()
    install(TARGETS file-ast26
            RUNTIME DESTINATION "${GIMP_PLUGIN_DIR}/plug-ins")
endif()
