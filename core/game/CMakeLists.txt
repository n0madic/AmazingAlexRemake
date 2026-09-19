# aa_game — progression, settings, save files and localisation (docs/05 §4/§8, docs/10 §7): plain value
# types over aa_data's loaders; no raylib.
add_library(aa_game STATIC
    src/progress.cpp
    src/save_store.cpp
    src/localization.cpp)
target_include_directories(aa_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aa_game PUBLIC aa_data cJSON)
if(NOT MSVC)
    target_compile_options(aa_game PRIVATE -Wall -Wextra)
endif()
