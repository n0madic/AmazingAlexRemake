# Box2D 2.2.1 (vendored, frozen). See README.md in this directory.
cmake_minimum_required(VERSION 3.16)
project(Box2D CXX)

file(GLOB_RECURSE BOX2D_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Collision/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Dynamics/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Rope/*.cpp)

add_library(Box2D STATIC ${BOX2D_SOURCES})
target_include_directories(Box2D PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(Box2D PUBLIC cxx_std_11)
# No fused multiply-add and no fast-math: the original ran on ARMv7 VFPv3 (unfused vmla) and the
# remake must stay bit-comparable across desktop and mobile builds.
target_compile_options(Box2D PUBLIC -ffp-contract=off -fno-fast-math)
option(BOX2D_DOUBLE_LIBM "Evaluate sqrt/atan2/sin/cos in double like the original Android build" ON)
option(BOX2D_PLATFORM_LIBM "Diagnostic: use the host's double libm instead of the vendored aa_libm" OFF)
if(BOX2D_DOUBLE_LIBM)
    target_compile_definitions(Box2D PUBLIC B2_DOUBLE_LIBM)
    if(BOX2D_PLATFORM_LIBM)
        target_compile_definitions(Box2D PUBLIC B2_PLATFORM_LIBM)
    else()
        if(NOT TARGET aa_libm)
            add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../aa_libm ${CMAKE_BINARY_DIR}/aa_libm)
        endif()
        target_link_libraries(Box2D PUBLIC aa_libm)
    endif()
endif()
option(BOX2D_RELEASE_EARLY_OUT "Use the 2.2.1 release early-out factor (3.0) instead of the trunk's 1.5" OFF)
if(BOX2D_RELEASE_EARLY_OUT)
    target_compile_definitions(Box2D PUBLIC b2_positionEarlyOutFactor=3.0f)
endif()
option(BOX2D_RELEASE_SLEEP "Use the 2.2.1 release sleep rule (requires converged positions)" OFF)
if(BOX2D_RELEASE_SLEEP)
    target_compile_definitions(Box2D PUBLIC b2_sleepRequiresPositionSolved=1)
endif()
