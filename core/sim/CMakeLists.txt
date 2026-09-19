# aa_sim — the deterministic, headless game core (docs/10-architecture.md §3, §5).
# Depends on Box2D (and, through it, aa_libm) only: no raylib, no I/O, no platform libm.
add_library(aa_sim STATIC
    src/level.cpp
    src/screen_layout.cpp
    src/templates.cpp
    src/world_state.cpp
    src/physics_world.cpp
    src/scene_recorder.cpp
    src/physics_object.cpp
    src/session.cpp
    src/simulation.cpp
    src/contact_listener.cpp
    src/goals.cpp
    src/attachments.cpp
    src/animations.cpp
    src/toolbox.cpp
    src/interaction.cpp
    src/touch_handler.cpp
    src/visual_state.cpp
    src/sound_renderer.cpp
    src/tutorial.cpp
    src/items/world_bound.cpp
    src/items/shelf.cpp
    src/items/balls.cpp
    src/items/boxes.cpp
    src/items/book.cpp
    src/items/containers.cpp
    src/items/hook.cpp
    src/items/goal_star.cpp
    src/items/billboard.cpp
    src/items/misc.cpp
    src/items/pipes.cpp
    src/items/balloon.cpp
    src/items/piggy_bank.cpp
    src/items/dart.cpp
    src/items/scissors.cpp
    src/items/seesaw.cpp
    src/items/trapdoor.cpp
    src/items/rc_controller.cpp
    src/items/boxing_glove.cpp
    src/items/skateboard.cpp
    src/items/rc_truck.cpp
    src/items/spring.cpp
    src/items/slingshot.cpp
    src/items/zip_line.cpp
    src/items/rope.cpp
    src/items/doll.cpp
    src/items/selection_area.cpp
    src/items/forces.cpp
    src/items/sounds.cpp)
target_include_directories(aa_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aa_sim PUBLIC Box2D aa_libm aa_fp_strict)
target_compile_features(aa_sim PUBLIC cxx_std_17)
if(NOT MSVC)
    # Every float→double promotion in the core must be deliberate (the original's own double paths are
    # written as explicit casts), so an accidental one is a build error.
    target_compile_options(aa_sim PRIVATE -Wall -Wextra -Wdouble-promotion -Werror=double-promotion)
endif()
