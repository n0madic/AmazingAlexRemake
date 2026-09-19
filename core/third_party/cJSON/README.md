# cJSON 1.7.18 (vendored, pinned release)

Source: `v1.7.18.tar.gz` from `https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz`
(sha256 `3aa806844a03442c00769b83e99970be70fbef03735ff898f4811dd03b9f5ee5`), licence MIT (`LICENSE`).

Kept: `cJSON.c`, `cJSON.h` only (no `cJSON_Utils`, tests, fuzzing or build files); built as the tiny static
target `cJSON` by `CMakeLists.txt` in this directory. No source modifications.

Numbers: cJSON parses every JSON number with `strtod` into `valuedouble`; `core/data` casts it to `float`, which is
the original game's own decimal → double → float path (`docs/02-level-format.md` §6).
