# aa_platform — raylib layer (docs/10-architecture.md §6): atlases, sprite quads, the world renderer, the
# audio system, the UI renderer, the game application and the level viewer. The screen maths (ScreenLayout) lives in aa_sim since M3: the core needs the letterbox
# numbers to map native pointer pixels to world metres.
add_library(aa_platform STATIC
    src/atlas.cpp
    src/audio.cpp
    src/sprite_batch.cpp
    src/world_renderer.cpp
    src/toolbox_renderer.cpp
    src/render/items.cpp
    src/ui_renderer.cpp
    src/app.cpp
    src/viewer.cpp)
# The per-OS answers (platform.h): the NativeActivity on Android, the environment elsewhere.
if(EMSCRIPTEN)
    target_sources(aa_platform PRIVATE src/platform_web.cpp)
elseif(ANDROID)
    target_sources(aa_platform PRIVATE src/platform_android.cpp)
    target_include_directories(aa_platform PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(aa_platform PUBLIC log android)
else()
    target_sources(aa_platform PRIVATE src/platform_desktop.cpp)
    if(APPLE)
        target_link_libraries(aa_platform PUBLIC "-framework CoreFoundation")   # the bundle's language list
    endif()
endif()
target_include_directories(aa_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(aa_platform PUBLIC raylib aa_data aa_game aa_sim aa_ui)
target_compile_definitions(aa_platform PRIVATE AA_DEFAULT_NOINTRO=$<BOOL:${AA_NOINTRO}>)
if(NOT MSVC)
    target_compile_options(aa_platform PRIVATE -Wall -Wextra)
endif()
