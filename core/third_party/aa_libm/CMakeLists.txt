# aa_libm: Bionic/FreeBSD-msun transcendentals, vendored so every platform computes the same bits
# (README.md). Linked into Box2D (B2_DOUBLE_LIBM) and, as a shared library, into the Unicorn harness.
cmake_minimum_required(VERSION 3.16)
project(aa_libm C)

file(GLOB AA_LIBM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

# Strict IEEE double arithmetic: no fused multiply-add, no fast-math, SSE2 (not x87) on 32-bit x86.
if(MSVC)
    set(AA_LIBM_FLAGS /fp:precise)
else()
    set(AA_LIBM_FLAGS -ffp-contract=off -fno-fast-math)
    # Emscripten's compatibility processor string is x86, but requesting SSE there raises the
    # browser baseline to WASM SIMD. Its scalar IEEE path is the portable equivalent.
    if(NOT EMSCRIPTEN AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        list(APPEND AA_LIBM_FLAGS -msse2 -mfpmath=sse)
    endif()
endif()

add_library(aa_libm STATIC ${AA_LIBM_SOURCES})
target_include_directories(aa_libm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(aa_libm PRIVATE ${AA_LIBM_FLAGS})

# Same sources as a shared library for tools/uc_harness.py (ctypes); not part of the default build.
add_library(aa_libm_shared SHARED EXCLUDE_FROM_ALL ${AA_LIBM_SOURCES})
set_target_properties(aa_libm_shared PROPERTIES OUTPUT_NAME aa_libm)
target_include_directories(aa_libm_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(aa_libm_shared PRIVATE ${AA_LIBM_FLAGS})
# The exact IEEE operations (sqrt, floor, ...) stay on the platform libm (README.md); glibc keeps it in a
# separate library that a C link does not pull in by itself.
if(UNIX AND NOT APPLE)
    target_link_libraries(aa_libm PUBLIC m)
    target_link_libraries(aa_libm_shared PUBLIC m)
endif()

# Known-answer self-test: run it on every new platform/compiler before trusting bit identity.
add_executable(aa_libm_selftest EXCLUDE_FROM_ALL selftest.c)
target_link_libraries(aa_libm_selftest PRIVATE aa_libm)
target_compile_options(aa_libm_selftest PRIVATE ${AA_LIBM_FLAGS})
