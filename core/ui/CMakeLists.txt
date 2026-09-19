# aa_ui — the retained view tree of the original's UI engine (docs/06 §1, docs/10 §6): View / ImageView /
# LabelView / Button / ScrollView / PageControl / Animator / Scene / SceneManager over aa_data's UI loaders
# and aa_game's localisation. No raylib: drawing goes through the Renderer interface, so the layout is
# testable without a window.
add_library(aa_ui STATIC
    src/resources.cpp
    src/view.cpp
    src/button.cpp
    src/scroll_view.cpp
    src/animator.cpp
    src/scene.cpp
    src/dialogs.cpp
    src/app_state.cpp
    src/menu_scenes.cpp
    src/game_scene.cpp
    src/extra_scenes.cpp
    src/sandbox_scene.cpp
    src/remake_views.cpp)
target_include_directories(aa_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aa_ui PUBLIC aa_data aa_game aa_sim)
if(NOT MSVC)
    target_compile_options(aa_ui PRIVATE -Wall -Wextra)
endif()
