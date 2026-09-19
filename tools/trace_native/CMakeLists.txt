# Native replayer for the physics regression harness (docs/09-physics-regression.md).
# Build:  cmake -S tools/trace_native -B build/trace_native -DCMAKE_BUILD_TYPE=Release [-DBOX2D_DOUBLE_LIBM=OFF: float libm; -DBOX2D_PLATFORM_LIBM=ON: host double libm instead of aa_libm]
#         cmake --build build/trace_native
cmake_minimum_required(VERSION 3.16)
project(trace_native CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../core/third_party/Box2D ${CMAKE_BINARY_DIR}/Box2D)

add_executable(trace_native main.cpp)
target_link_libraries(trace_native PRIVATE Box2D)
target_compile_options(trace_native PRIVATE -ffp-contract=off -fno-fast-math -Wall)
