# cJSON 1.7.18 (vendored). See README.md in this directory.
add_library(cJSON STATIC cJSON.c)
target_include_directories(cJSON PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
