# doctest suites, the G3 conformance tool and the gate registrations (docs/10-architecture.md §8).

# --- conformance tool: writes the core's world construction in the harness's .scene format --------
add_executable(sim_scene_dump sim_scene_dump.cpp)
target_link_libraries(sim_scene_dump PRIVATE aa_data)
# --- G5a tool: runs a set-up interaction script (tools/uc_setup_oracle.py format) on the core ----------
add_executable(sim_setup_dump sim_setup_dump.cpp)
target_link_libraries(sim_setup_dump PRIVATE aa_data)
# --- G4 / G6 tool: plays a level (tools/uc_sim_oracle.py script format) on the core -----------------------
add_executable(sim_run_dump sim_run_dump.cpp)
target_link_libraries(sim_run_dump PRIVATE aa_data)

# --- doctest suites --------------------------------------------------------------------------------
add_executable(aa_tests
    test_main.cpp
    test_level_loader.cpp
    test_level_writer.cpp
    test_sandbox_store.cpp
    test_sandbox_modes.cpp
    test_frame_table.cpp
    test_templates.cpp
    test_filters.cpp
    test_world_state.cpp
    test_physics_world.cpp
    test_render_state.cpp
    test_screen_layout.cpp
    test_setup_undo.cpp
    test_setup_pick_drag.cpp
    test_setup_snap_ghost.cpp
    test_setup_camera_limits.cpp
    test_simulation.cpp
    test_ui_loaders.cpp
    test_asset_root.cpp
    test_progression.cpp
    test_ui_layout.cpp
    test_tutorial.cpp
    test_touch_two_finger.cpp)
target_link_libraries(aa_tests PRIVATE aa_ui doctest)
target_compile_definitions(aa_tests PRIVATE
    AA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    AA_ASSETS_CONFIGURED="${AA_ASSETS}")
add_test(NAME aa_tests COMMAND aa_tests)

# --- gates that need the research harness (Python venv + the Android binary) -----------------------
set(AA_PY "${CMAKE_SOURCE_DIR}/.venv/bin/python")
if(DEFINED ENV{PY} AND NOT "$ENV{PY}" STREQUAL "")
    set(AA_PY "$ENV{PY}")
endif()
set(AA_SO "${CMAKE_SOURCE_DIR}/extracted/apk/lib/armeabi-v7a/libamazingalex.so")
# The Python gates default to <source>/build/assets; hand them the configured tree like aa_tests gets.
set(AA_GATE_ASSETS "")
if(AA_ASSETS)
    set(AA_GATE_ASSETS --assets "${AA_ASSETS}")
endif()
if(EXISTS "${AA_PY}" AND EXISTS "${AA_SO}")
    # G2 (docs/09): ~40 s; fails by exit code when fewer than 55 scenarios are bit-identical.
    add_test(NAME physics_regression
             COMMAND ${CMAKE_SOURCE_DIR}/tools/run_physics_regression.sh ${CMAKE_BINARY_DIR}/physics_regression
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(physics_regression PROPERTIES LABELS "gate" ENVIRONMENT "PY=${AA_PY}")
    # G3: the core's construction vs the emulated original, every implemented type × mode × flip.
    add_test(NAME sim_conformance
             COMMAND ${AA_PY} ${CMAKE_SOURCE_DIR}/tools/sim_conformance.py ${AA_GATE_ASSETS}
                     --dump $<TARGET_FILE:sim_scene_dump> --out ${CMAKE_BINARY_DIR}/sim_conformance
                     --md ${CMAKE_BINARY_DIR}/sim_conformance/report.md
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(sim_conformance PROPERTIES LABELS "gate")
    # G5a: the core's set-up manipulations vs the emulated original on the scripts in tests/setup_scripts.
    add_test(NAME setup_conformance
             COMMAND ${AA_PY} ${CMAKE_SOURCE_DIR}/tools/setup_conformance.py ${AA_GATE_ASSETS}
                     --dump $<TARGET_FILE:sim_setup_dump> --out ${CMAKE_BINARY_DIR}/setup_conformance
                     --md ${CMAKE_BINARY_DIR}/setup_conformance/report.md
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(setup_conformance PROPERTIES LABELS "gate")
    # G4: the core's simulation runs vs the emulated original on the scripts in tests/sim_scripts.
    add_test(NAME sim_run_conformance
             COMMAND ${AA_PY} ${CMAKE_SOURCE_DIR}/tools/sim_run_conformance.py ${AA_GATE_ASSETS}
                     --dump $<TARGET_FILE:sim_run_dump> --out ${CMAKE_BINARY_DIR}/sim_run_conformance
                     --md ${CMAKE_BINARY_DIR}/sim_run_conformance/report.md
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(sim_run_conformance PROPERTIES LABELS "gate")
    # G6: every level of the catalogue for 5 s (minutes of Unicorn; its own label).
    add_test(NAME sim_catalogue_conformance
             COMMAND ${AA_PY} ${CMAKE_SOURCE_DIR}/tools/sim_run_conformance.py ${AA_GATE_ASSETS}
                     --dump $<TARGET_FILE:sim_run_dump> --all-levels --seconds 5
                     --known ${CMAKE_SOURCE_DIR}/tests/sim_known_divergences.txt
                     --out ${CMAKE_BINARY_DIR}/sim_catalogue_conformance
                     --md ${CMAKE_BINARY_DIR}/sim_catalogue_conformance/report.md
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(sim_catalogue_conformance PROPERTIES LABELS "catalogue")
else()
    message(STATUS "physics_regression / sim_conformance / setup_conformance skipped: no harness (${AA_PY}, ${AA_SO})")
endif()
