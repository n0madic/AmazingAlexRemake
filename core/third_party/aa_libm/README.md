# aa_libm — the device libm, vendored

The shipped Android build of Amazing Alex (`libamazingalex.so`) imports the **double** libm entry points
`sin cos tan asin acos atan atan2 exp log log10 pow sinh cosh tanh` (+ the exact `sqrt floor ceil fmod frexp
ldexp modf`). On the device those came from Bionic, whose `libm/src` is FreeBSD msun (fdlibm-derived) compiled
as plain C on ARM — and that code is byte-identical at the `android-2.3.7_r1`, `android-4.0.4_r2.1` and
`android-4.2.2_r1` tags (checked), i.e. across the Android versions the 2012 game ran on. Platform libms differ from it in the last bit on a few percent of inputs (measured
against Apple's on macOS: sin/cos ≈ 4 %, atan2 ≈ 18 %, tan ≈ 41 %, pow/exp ≈ 10 %), so the remake carries the
msun code and calls it wherever the original called libm. Box2D uses `aa_sin/aa_cos/aa_atan2` (`B2_DOUBLE_LIBM`,
`../Box2D/Common/b2Math.h`); the Unicorn harness (`tools/uc_harness.py`) routes the emulated binary's libm
imports to the same code, so the physics regression (docs/09) compares like with like.

## Contents

* `src/*.c` — 20 files from `platform_bionic` tag `android-4.0.4_r2.1`, `libm/src/` (`s_sin.c s_cos.c s_tan.c
  k_sin.c k_cos.c k_tan.c e_rem_pio2.c k_rem_pio2.c e_atan2.c s_atan.c e_asin.c e_acos.c e_exp.c e_log.c
  e_log10.c e_pow.c e_sinh.c e_cosh.c s_tanh.c s_expm1.c`). Produced by `tools/vendor_aa_libm.py`: verbatim
  except that every exported / `__ieee754_*` / `__kernel_*` symbol is renamed with an `aa_` prefix (no clash with
  the platform libm, no builtin folding by the compiler) and the FreeBSD `__weak_reference` lines are dropped.
  Re-run the script to re-vendor; do not edit the sources by hand.
* `src/math_private.h` — hand-written replacement for msun's header: the double word-access macros, little-endian
  only, `aa_` kernel prototypes. `src/aa_libm.h` — public prototypes (C linkage).
* `NOTICE` — Bionic's libm notice (Sun/SunPro and BSD licences; the SunPro headers in every source must stay).
* `selftest.c` + `kat.inc` — known-answer test, 3405 (input bits → output bits) cases recorded on the reference
  platform (macOS arm64, Apple clang 21) with `tools/gen_aa_libm_kat.py`. **Run it on every new platform or
  compiler** (`cmake --build … --target aa_libm_selftest && ./aa_libm_selftest`): it catches fused multiply-adds
  (a `-ffp-contract=fast` build fails 9 cases on arm64), x87 excess precision and fast-math.

## Build rules

Strict IEEE double: `-ffp-contract=off -fno-fast-math` (Clang/GCC), `-msse2 -mfpmath=sse` on 32-bit x86,
`/fp:precise` on MSVC (untested — the self-test, not the flag, decides). Residual assumption: the reference
bits (arm64 Clang, strict IEEE double) equal what the device's GCC/VFPv3 build produced, because VFPv3 `vmla` is
an unfused multiply-add and msun uses no excess precision; inferred from the ISA, not measured on hardware.
Targets: `aa_libm` (static, linked into Box2D), `aa_libm_shared`
(ctypes library for the harness — `uc_harness.load_aa_libm` compiles the same sources with `cc` on demand into
`build/aa_libm`), `aa_libm_selftest`.

Exact IEEE operations stay on the platform libm: `sqrt` (Box2D `b2Sqrt`), `floor`, `ceil`, `fmod`, `frexp`,
`ldexp`, `modf`, `fabs`, `scalbn`, `copysign`.
