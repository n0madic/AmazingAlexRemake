if(WIN32 AND AA_WINDOWS_ICON)
    if(NOT EXISTS "${AA_WINDOWS_ICON}")
        message(FATAL_ERROR "AA_WINDOWS_ICON does not exist: ${AA_WINDOWS_ICON}")
    endif()
    enable_language(RC)
    string(REPLACE "\\" "/" AA_WINDOWS_ICON_RC "${AA_WINDOWS_ICON}")
    configure_file("${CMAKE_SOURCE_DIR}/platform/windows/AppIcon.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc" @ONLY)
endif()

if(ANDROID)
    # A NativeActivity app is a shared library: raylib's android_main (native_app_glue) calls our main().
    # `-u ANativeActivity_onCreate` keeps the glue's entry point from the static raylib archive (raylib's own
    # CMake puts it into CMAKE_SHARED_LINKER_FLAGS in its directory scope only); the 16 KiB page size is
    # what Android 15+ devices expect of a native library.
    add_library(amazing_alex SHARED main.cpp)
    target_link_options(amazing_alex PRIVATE -u ANativeActivity_onCreate -Wl,-z,max-page-size=16384)
else()
    add_executable(amazing_alex main.cpp)
endif()
if(WIN32 AND AA_WINDOWS_ICON)
    target_sources(amazing_alex PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.rc")
endif()
target_link_libraries(amazing_alex PRIVATE aa_platform)

if(EMSCRIPTEN)
    # The imported game tree remains the player's own input. Emscripten preloads it read-only at /assets;
    # web_pre.js mounts /persistent as IDBFS before main so SaveStore keeps its regular file contract.
    target_link_options(amazing_alex PRIVATE
        "--preload-file" "${AA_WEB_ASSETS}@/assets"
        "--pre-js" "${CMAKE_SOURCE_DIR}/platform/web/web_pre.js"
        "--post-js" "${CMAKE_SOURCE_DIR}/platform/web/web_post.js"
        "-lidbfs.js"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sNO_EXIT_RUNTIME=1"
        "-sFORCE_FILESYSTEM=1")
    # manifest.json stands in for the whole preloaded tree: import_assets.py writes it last with the sha1
    # of every file, so a re-import (another --profile, an edited level) relinks amazing_alex.data.
    set_property(TARGET amazing_alex APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_SOURCE_DIR}/platform/web/web_pre.js" "${CMAKE_SOURCE_DIR}/platform/web/web_post.js"
        "${AA_WEB_ASSETS}/manifest.json")
    # No --shell-file: emcc emits only the JS glue, wasm binary and preloaded-asset blob (no HTML at
    # all, so none of Emscripten's own default page chrome exists to strip). platform/web/index.html is our own
    # static page — just the canvas and a script tag — copied next to them so it becomes the site's root.
    set_target_properties(amazing_alex PROPERTIES OUTPUT_NAME "amazing_alex" SUFFIX ".js")
    add_custom_command(TARGET amazing_alex POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/platform/web/index.html"
            "$<TARGET_FILE_DIR:amazing_alex>/index.html"
        COMMENT "Copying platform/web/index.html next to amazing_alex.js/.wasm/.data")
    set_property(TARGET amazing_alex APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/platform/web/index.html")
    if(AA_WEB_FAVICON)
        # import_assets.py normalises the original launcher artwork to a canonical PNG.
        add_custom_command(TARGET amazing_alex POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${AA_WEB_FAVICON}"
                "$<TARGET_FILE_DIR:amazing_alex>/favicon.png"
            COMMENT "Copying the original launcher artwork as the web favicon")
        set_property(TARGET amazing_alex APPEND PROPERTY LINK_DEPENDS "${AA_WEB_FAVICON}")
    endif()
endif()

if(APPLE AND NOT ANDROID)
    # The macOS .app (tools/build_macos_app.sh): the same main.cpp as a bundle, built on request only — the
    # bare amazing_alex stays the binary the scripts and the tests run. The script lays the imported tree
    # into Contents/Resources/assets (platform_desktop.cpp finds it there) and the icon next to it.
    add_executable(amazing_alex_bundle MACOSX_BUNDLE EXCLUDE_FROM_ALL main.cpp)
    target_link_libraries(amazing_alex_bundle PRIVATE aa_platform)
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(AA_MACOS_MINIMUM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(AA_MACOS_MINIMUM_VERSION "11.0")
    endif()
    set_target_properties(amazing_alex_bundle PROPERTIES
        OUTPUT_NAME "Amazing Alex"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Amazing Alex"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.amazingalex.remake"
        MACOSX_BUNDLE_ICON_FILE "AppIcon"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()
