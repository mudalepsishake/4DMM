cmake_minimum_required(VERSION 3.28)
cmake_policy(SET CMP0141 NEW)
project(3DMMEx LANGUAGES CXX C VERSION 0.6.0)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)

if(MSVC)
  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")

  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # Compiling with ClangCL

    # FUTURE: Fix dangling-else warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /clang:-Wno-dangling-else")

    # Disable SafeSEH: required to link BRender
    # TODO: fix FindBRender to set this correctly when compiling with ClangCL
    set(CMAKE_EXE_LINKER_FLAGS "/SAFESEH:NO")
  else()
    # Generate debug information for Edit and Continue / Hot Reload
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug>,EditAndContinue,ProgramDatabase>")

    # Remove unused functions from release binary
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:REF")
  endif()
endif()

include(CMakeDependentOption)
include(FetchContent)
include(TargetChompSources)

# Options
option(BUILD_PACKAGES "Build packages for 3DMM" ON)

cmake_dependent_option(3DMM_PACKAGE_WIX "Generate an MSI via WiX" ON "BUILD_PACKAGES AND DEFINED ENV{WIX}" OFF)
cmake_dependent_option(3DMM_PACKAGE_ZIP "Generate a portable ZIP" ON "BUILD_PACKAGES" OFF)

set(3DMM_PRECOMPILED_CHUNKY_PATH "" CACHE PATH "Use pre-compiled chunky files")
if (3DMM_PRECOMPILED_CHUNKY_PATH)
    if (IS_DIRECTORY "${3DMM_PRECOMPILED_CHUNKY_PATH}")
        message("Using precompiled chunky files: ${3DMM_PRECOMPILED_CHUNKY_PATH}")
    else()
        message(FATAL_ERROR "Precompiled chunky path not found: ${3DMM_PRECOMPILED_CHUNKY_PATH}")
    endif()
endif()

if (MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(3DMM_BRENDER_LIBRARY_DEFAULT "Original")
else()
  set(3DMM_BRENDER_LIBRARY_DEFAULT "Source")
endif()

set(3DMM_BRENDER_LIBRARY "${3DMM_BRENDER_LIBRARY_DEFAULT}" CACHE STRING "Argonaut BRender library to use for rendering 3D images")
set_property(CACHE 3DMM_BRENDER_LIBRARY PROPERTY STRINGS "Original" "Source" "Modern")

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # AudioMan is only supported on Windows
    set(3DMM_AUDIOMAN_LIBRARY "Decompilation" CACHE STRING "AudioMan library to use for mixing wave sounds")
    set_property(CACHE 3DMM_AUDIOMAN_LIBRARY PROPERTY STRINGS "Original" "Decompilation" "None")
else()
    set(3DMM_AUDIOMAN_LIBRARY "None")
endif()

# Default to Win32 on Windows, and SDL on other platforms.
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(3DMM_GUI_DEFAULT "Win32")
else()
    set(3DMM_GUI_DEFAULT "SDL")
endif()

# Configure GUI library
set(3DMM_GUI "${3DMM_GUI_DEFAULT}" CACHE STRING "GUI library to use for graphics")
set_property(CACHE 3DMM_GUI PROPERTY STRINGS "Win32" "SDL")

if ("${3DMM_GUI}" STREQUAL "Win32")

    if (NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(FATAL_ERROR "Win32 GUI only supported when compiling for Windows")
    endif()

elseif("${3DMM_GUI}" STREQUAL "SDL")

    find_package(PkgConfig QUIET)

    # Download binaries for SDL2 and FluidSynth when building for Windows
    if (WIN32)
        include(FetchSDL2)
        include(FetchFluidSynth)
    endif()

    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)

    find_package(FluidSynth CONFIG)
    if (FluidSynth_FOUND)
        include(FetchSoundfont)
    endif()

    # Find GStreamer
    if (PkgConfig_FOUND)
        pkg_check_modules(GST gstreamer-1.0 gstreamer-app-1.0)
    endif()

endif()

# Optional Tools
find_package(ClangTidy)
find_package(CCache)

if (TARGET CCache::CCache AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  get_property(CMAKE_CXX_COMPILER_LAUNCHER TARGET CCache::CCache PROPERTY IMPORTED_LOCATION)
  get_property(CMAKE_C_COMPILER_LAUNCHER TARGET CCache::CCache PROPERTY IMPORTED_LOCATION)
endif()

# TODO: Enable this when the 
#if (TARGET Clang::Tidy AND NOT CMAKE_CXX_CLANG_TIDY)
#  get_property(CMAKE_CXX_CLANG_TIDY TARGET Clang::Tidy PROPERTY IMPORTED_LOCATION)
#endif()

add_compile_definitions(
  $<$<CONFIG:Debug>:DEBUG>
)
if (WIN32)
    add_compile_definitions(WIN)
endif()

option(ENABLE_ASM_X86 "Use original optimized assembly for Intel x86" OFF)
if (ENABLE_ASM_X86)
    add_compile_definitions(IN_80386)
endif()

if (CMAKE_C_BYTE_ORDER MATCHES LITTLE_ENDIAN)
    add_compile_definitions ("LITTLE_ENDIAN")
endif()

option(3DMM_UNICODE "Enable Unicode" OFF)
if (3DMM_UNICODE)
    add_compile_definitions(
        UNICODE
        _UNICODE
    )
endif()

# Configure version header. Exported 4DMM source trees are intentionally not
# Git working copies, so do not invoke Git at all unless .git is actually here.
if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND git describe --tags --always
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GIT_DESCRIBE_RESULT
    ERROR_QUIET
  )
  if(NOT GIT_DESCRIBE_RESULT EQUAL 0)
    set(GIT_VERSION "source-archive")
  endif()
else()
  set(GIT_VERSION "source-archive")
endif()
configure_file(
  "${PROJECT_SOURCE_DIR}/inc/version.h.in"
  "${PROJECT_BINARY_DIR}/inc/version.h"
)

# Select BRender library
if ("${3DMM_BRENDER_LIBRARY}" STREQUAL "Original")

    # Use original static libraries
    find_package(BRender REQUIRED)

elseif("${3DMM_BRENDER_LIBRARY}" STREQUAL "Source")

    message("Building BRender from source")

    FetchContent_Declare(
        3DMM-BRender
        GIT_REPOSITORY https://github.com/benstone/3DMM-BRender
        GIT_TAG blazin
    )

    set(BRENDER_ASAN OFF)
    FetchContent_MakeAvailable(3DMM-BRender)

    # actorlight29: Source BRender's original framework has fixed-size active
    # light tables sized by BR_MAX_LIGHTS (16).  Raise that compile-time table
    # alongside 4DMM's Light Lab record arrays so city-scale authoring is not
    # stopped by the 1995 ceiling.
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                -DBRENDER_SOURCE_DIR=${BRender_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/cmake/brender-patches/PatchLightLimit.cmake
        RESULT_VARIABLE ACTORLIGHT_LIGHT_LIMIT_PATCH_RESULT
    )
    if(NOT ACTORLIGHT_LIGHT_LIMIT_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "actorlight29: failed to raise Source BRender light limit")
    endif()

    # actorlight2: Ben Stone's source BRender RGB888 lit-texture entry
    # selects the right triangle family, but its shared scanline routine still
    # performs INDEX_8 shade-table lookup for bpp == 3. Patch the fetched source
    # in place before it is compiled so RGB888 textures are modulated directly.
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                -DBRENDER_SOURCE_DIR=${BRender_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/cmake/brender-patches/PatchActorLitRgb888.cmake
        RESULT_VARIABLE ACTORLIGHT_BRENDER_PATCH_RESULT
    )
    if(NOT ACTORLIGHT_BRENDER_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "actorlight2: failed to patch source BRender RGB888 textured lighting")
    endif()

    # logperf17: instrument the exact Source BRender lighting/model update
    # pipeline used by -a so performance.csv can identify pathological tween
    # frames without relying on verbose per-actor breadcrumbs.
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                -DBRENDER_SOURCE_DIR=${BRender_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/cmake/brender-patches/PatchLightingPerf.cmake
        RESULT_VARIABLE LIGHTING_PERF_BRENDER_PATCH_RESULT
    )
    if(NOT LIGHTING_PERF_BRENDER_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "logperf17: failed to instrument Source BRender lighting performance")
    endif()

    # actorlight34: keep full projected depth for RGB888, resolve only true
    # legacy-depth ties by draw order, and guard giant/off-screen raster spans.
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                -DBRENDER_SOURCE_DIR=${BRender_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/cmake/brender-patches/PatchDepthPrecision.cmake
        RESULT_VARIABLE ACTORLIGHT_DEPTH_PRECISION_PATCH_RESULT
    )
    if(NOT ACTORLIGHT_DEPTH_PRECISION_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "actorlight34: failed to patch Source BRender depth/raster safety")
    endif()

    # actorlight39: optional -3dfix projection/depth probe.  This patch is
    # runtime-gated; normal rendering remains the v36/v38 path.
    execute_process(
        COMMAND ${CMAKE_COMMAND}
                -DBRENDER_SOURCE_DIR=${BRender_SOURCE_DIR}
                -P ${PROJECT_SOURCE_DIR}/cmake/brender-patches/Patch3DFixProbe.cmake
        RESULT_VARIABLE ACTORLIGHT_3DFIX_PROBE_PATCH_RESULT
    )
    if(NOT ACTORLIGHT_3DFIX_PROBE_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "actorlight39: failed to patch Source BRender 3dfix probe")
    endif()

    add_library(brender INTERFACE)
    target_link_libraries(
        brender
        INTERFACE
        FW ZB FMT STD
    )
    target_include_directories(
        brender
        INTERFACE
        "${BRender_SOURCE_DIR}/INC"
    )
    add_library(BRender::Libraries ALIAS brender)

elseif("${3DMM_BRENDER_LIBRARY}" STREQUAL "Modern")

    message("Building vendored BRender 1.4 Modern/OpenGL compatibility source")

    # BRender 1.4 is part of the 4DMM source tree. Do not FetchContent it and
    # do not mutate it with configure-time patch files. All compatibility work
    # is ordinary source editing under source/brender14.
    set(BRENDER_MODERN_SOURCE_DIR "${PROJECT_SOURCE_DIR}/brender14")
    if(NOT EXISTS "${BRENDER_MODERN_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Vendored BRender 1.4 source is missing: ${BRENDER_MODERN_SOURCE_DIR}. "
            "4DMM Modern builds require source/brender14 to be present.")
    endif()

    # Set BRender options before add_subdirectory() so the vendored project is
    # configured with 3DMM's fixed-point ABI and legacy model compatibility.
    set(BRENDER_LEGACY_FIXED_ABI ON CACHE BOOL "" FORCE)
    set(BRENDER_LEGACY_3DMM_MODEL_ABI ON CACHE BOOL "" FORCE)
    set(BRENDER_BUILD_DRIVERS ON CACHE BOOL "" FORCE)
    # 4DMM supplies its own Win32/WGL window bridge for glrend; do not pull
    # BRender's optional SDL device/window backends into this experiment.
    set(BRENDER_DISABLE_FINDSDL ON CACHE BOOL "" FORCE)
    set(BRENDER_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BRENDER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BRENDER_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(BRENDER_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(BRENDER_BUILD_SOFT OFF CACHE BOOL "" FORCE)

    # Keep dependency build products out of the checked-in source directory.
    add_subdirectory(
        "${BRENDER_MODERN_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/brender14-build"
    )

    add_library(brender_3dmm_modern INTERFACE)
    target_link_libraries(brender_3dmm_modern INTERFACE BRender::Full)
    add_library(BRender::Libraries ALIAS brender_3dmm_modern)
    add_compile_definitions(BRENDER_MODERN_14=1)
else()
    message(FATAL_ERROR "Invalid BRender library selected: ${3DMM_BRENDER_LIBRARY}")
endif()

# Select AudioMan library
if ("${3DMM_AUDIOMAN_LIBRARY}" STREQUAL "Original")

    # Use original static library
    find_package(AudioMan REQUIRED)

elseif("${3DMM_AUDIOMAN_LIBRARY}" STREQUAL "Decompilation")

    # Use decompiled AudioMan
    FetchContent_Declare(
        AudioManDecomp
        GIT_REPOSITORY https://github.com/benstone/audioman-decomp
        GIT_TAG main
    )

    set(AUDIOMAN_BUILD_DEMO_APPS OFF)
    set(AUDIOMAN_USE_DEBUG_ALLOCATOR OFF)
    FetchContent_MakeAvailable(AudioManDecomp)

    # AudioMan's decompiled converter skips its temporary source buffer when
    # source and destination byte counts are equal. Some valid 3DMM sounds
    # still need ACM conversion in that case, so patch the fetched filter at
    # the call site before AudioMan is compiled.
    FetchContent_GetProperties(AudioManDecomp SOURCE_DIR AUDIOMAN_DECOMP_SOURCE_DIR)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DAUDIOMAN_SOURCE_DIR=${AUDIOMAN_DECOMP_SOURCE_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchAudioManEqualSizeConversion.cmake"
        COMMAND_ERROR_IS_FATAL ANY
    )

    add_library(3DMMForever::AudioMan ALIAS AudioMan)

elseif("${3DMM_AUDIOMAN_LIBRARY}" STREQUAL "None")
    # No AudioMan
else()
    message(FATAL_ERROR "Invalid AudioMan library selected: ${3DMM_AUDIOMAN_LIBRARY}")
endif()

if (NOT "${3DMM_AUDIOMAN_LIBRARY}" STREQUAL "None")
    add_compile_definitions(HAS_AUDIOMAN)
endif()

# TODO: Find libiniparser if installed
include(FetchIniParser)

include(FetchMiniaudio)

option(BUILD_TESTS "Enable building tests" ON)
if (BUILD_TESTS)
    enable_testing()
    include(FetchGoogleTest)
    include(GoogleTest)
    add_custom_target(tests)
endif()

add_subdirectory(kauai)

add_subdirectory(src)

# Add documentation to install package
install(
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt
    DESTINATION .
)

# CPack must be included *last*, which is antithetical to how most CMake
# Modules work. We can set all variables for all packaging regardless of
# whether they're supported. This way we can just swap out what is added to
# `CPACK_GENERATOR`

# DO NOT CHANGE THIS. EVER. Generated manually via uuidgen. This needs to be
# the same so that user's can automatically upgrade via MSI
set(CPACK_WIX_UPGRADE_GUID 34ffdec3-60c4-4578-9cff-3e45cbc9ab0c)
# Change this as needed.
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT https://github.com/benstone/3DMMEx)

if (BUILD_PACKAGES)
  add_custom_target(dist DEPENDS $<TARGET_NAME_IF_EXISTS:package>)
  foreach (generator IN ITEMS ZIP WIX)
    if (3DMM_PACKAGE_${generator})
      list(APPEND CPACK_GENERATOR ${generator})
    endif()
  endforeach()
  include(CPack)
endif()
