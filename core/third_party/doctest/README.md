# doctest 2.4.11 (vendored, pinned release)

Source: `v2.4.11.tar.gz` from `https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz`
(sha256 `632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186`), licence MIT (`LICENSE.txt`).

Kept: the single header `doctest/doctest.h` (copied here as `doctest.h`); dropped everything else (examples,
docs, scripts, CMake packaging). Exposed as the INTERFACE target `doctest` by the top-level `CMakeLists.txt`;
`tests/test_main.cpp` defines `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`. No source modifications.
