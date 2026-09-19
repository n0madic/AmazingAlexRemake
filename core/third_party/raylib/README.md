# raylib 6.0 (vendored, pinned release)

Source: `raylib-6.0.tar.gz` from `https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz`
(release 2026-04-23, sha256 `2b3ee1e2120c7a0796b33062c7e9a694dd8a8caa56a96319ac8c8ecf54a90d0b`), licence zlib (`LICENSE`).

Kept: `src/` (including `src/external/` — GLFW, RGFW, miniaudio, dr_*, stb_*, glad, …), `CMakeLists.txt`,
`CMakeOptions.txt`, `cmake/`, `raylib.pc.in`, `LICENSE`. Dropped: `examples/`, `projects/`, `tools/`, `logo/`, the
docs/metadata files (README, CHANGELOG, HISTORY, FAQ, …, `build.zig*`) and `src/external/glfw/{docs,examples,tests}`.

Built by the top-level `CMakeLists.txt` with `BUILD_EXAMPLES=OFF`, `PLATFORM=Desktop`. The bundled GLFW declares
`cmake_minimum_required(VERSION 3.4...3.28)`, which CMake ≥ 4.0 refuses, so the top level sets
`CMAKE_POLICY_VERSION_MINIMUM 3.5` around the `add_subdirectory`. No source modifications.

raylib is the platform layer only (window, textures, audio, input — `docs/10-architecture.md` §2, §6); nothing in
the deterministic core (`core/sim`) may include it.
