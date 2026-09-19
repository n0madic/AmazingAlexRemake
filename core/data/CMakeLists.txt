# aa_data — loaders for the imported asset tree (docs/10-architecture.md §4): cJSON + aa_sim types.
add_library(aa_data STATIC
    src/json.cpp
    src/asset_root.cpp
    src/level_loader.cpp
    src/level_writer.cpp
    src/frame_table_loader.cpp
    src/ui_loaders.cpp)
target_include_directories(aa_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aa_data PUBLIC aa_sim cJSON aa_fp_strict)
if(NOT MSVC)
    target_compile_options(aa_data PRIVATE -Wall -Wextra)
endif()
