# Amazing Alex remake — top-level build (docs/10-architecture.md §3).
# Layers: core/third_party (vendored, pinned) ← core/sim (deterministic core) ← core/data (loaders) ←
# core/platform (raylib) ← app.  tools/trace_native keeps its own standalone CMake file (physics gate, docs/09).
cmake_minimum_required(VERSION 3.25)
# The remake's own version (the macOS bundle's CFBundleShortVersionString); the original it is based on is 1.0.5.
project(AmazingAlex VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AA_BUILD_APP "Build the raylib application and the platform layer" ON)
option(AA_BUILD_TESTS "Build the doctest suites and the conformance tools" ON)
option(AA_BUILD_WEB "Build the optional Emscripten/WebAssembly application" OFF)
option(AA_NOINTRO "Start the application at the main menu instead of the Rovio/loading splash" OFF)
set(AA_ASSETS "" CACHE PATH "Imported asset tree (tools/import_assets.py output) used by the tests")
set(AA_WEB_ASSETS "" CACHE PATH "Imported asset tree bundled into the optional web application")
set(AA_WEB_FAVICON "" CACHE FILEPATH "Original launcher artwork copied as the web favicon")
set(AA_WINDOWS_ICON "" CACHE FILEPATH "Windows ICO embedded into amazing_alex.exe")

if(AA_BUILD_WEB)
    if(NOT EMSCRIPTEN)
        message(FATAL_ERROR "AA_BUILD_WEB requires the Emscripten toolchain; configure with emcmake cmake")
    endif()
    if(NOT AA_BUILD_APP)
        message(FATAL_ERROR "AA_BUILD_WEB requires AA_BUILD_APP=ON")
    endif()
    if(NOT AA_WEB_ASSETS)
        message(FATAL_ERROR "AA_BUILD_WEB requires AA_WEB_ASSETS (the output of tools/import_assets.py)")
    endif()
    if(NOT EXISTS "${AA_WEB_ASSETS}/manifest.json")
        message(FATAL_ERROR "AA_WEB_ASSETS must contain manifest.json: ${AA_WEB_ASSETS}")
    endif()
    if(AA_WEB_FAVICON AND NOT EXISTS "${AA_WEB_FAVICON}")
        message(FATAL_ERROR "AA_WEB_FAVICON does not exist: ${AA_WEB_FAVICON}")
    endif()
    # Data loaders use C++ exceptions for malformed/missing resources; Emscripten disables catches by
    # default, turning an ordinary load diagnostic into an opaque abort in the browser.
    add_compile_options(-fexceptions)
    add_link_options("-sDISABLE_EXCEPTION_CATCHING=0")
endif()

include(CTest)

# Strict float semantics for every numeric target of the remake (docs/10 §5.2, docs/09 §2): no fused
# multiply-add, no fast-math, SSE2 (not x87) on 32-bit x86. PUBLIC so that consumers compiling inline
# core code get the same flags; Box2D and aa_libm carry their own copies.
add_library(aa_fp_strict INTERFACE)
if(MSVC)
    target_compile_options(aa_fp_strict INTERFACE /fp:precise)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_compile_options(aa_fp_strict INTERFACE /arch:SSE2)
    endif()
else()
    target_compile_options(aa_fp_strict INTERFACE -ffp-contract=off -fno-fast-math)
    # Emscripten reports an x86-like processor for CMake compatibility, but SSE would require the
    # optional WASM SIMD extension. The scalar strict-float path is the portable web baseline.
    if(NOT EMSCRIPTEN AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        target_compile_options(aa_fp_strict INTERFACE -msse2 -mfpmath=sse)
    endif()
endif()

# --- vendored third-party ---------------------------------------------------------------------------
add_subdirectory(core/third_party/Box2D)          # also adds aa_libm (B2_DOUBLE_LIBM)
add_subdirectory(core/third_party/cJSON)
add_library(doctest INTERFACE)
target_include_directories(doctest INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/third_party/doctest)

if(AA_BUILD_APP)
    # raylib 6.0: the bundled GLFW declares cmake_minimum_required(3.4...3.28), which CMake >= 4 refuses.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # raylib's platform: Web is an opt-in Emscripten build; the NDK selects GLES 2 / EGL / native_app_glue
    # (tools/build_android.sh), everything else remains the existing GLFW desktop build.
    if(AA_BUILD_WEB)
        set(PLATFORM "Web" CACHE STRING "" FORCE)
    elseif(ANDROID)
        set(PLATFORM "Android" CACHE STRING "" FORCE)
    else()
        set(PLATFORM "Desktop" CACHE STRING "" FORCE)
    endif()
    add_subdirectory(core/third_party/raylib EXCLUDE_FROM_ALL)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    # The level thumbnails are JPEGs (docs/12 §2); raylib's config.h leaves the decoder off by default.
    target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG)
endif()

# --- the remake -------------------------------------------------------------------------------------
add_subdirectory(core/sim)
add_subdirectory(core/data)
add_subdirectory(core/game)
add_subdirectory(core/ui)
if(AA_BUILD_APP)
    add_subdirectory(core/platform)
    add_subdirectory(app)
endif()

# --- gates ------------------------------------------------------------------------------------------
# G1: the vendored libm reproduces the reference bits on this platform/compiler (aa_libm/README.md).
add_test(NAME aa_libm_selftest COMMAND aa_libm_selftest)
set_tests_properties(aa_libm_selftest PROPERTIES LABELS "gate")
add_custom_target(aa_gate_binaries ALL DEPENDS aa_libm_selftest)

if(AA_BUILD_TESTS)
    add_subdirectory(tests)
endif()
