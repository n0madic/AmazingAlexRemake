/* Known-answer test for aa_libm: every (input bits → output bits) pair in kat.inc was recorded on the
 * reference platform (macOS arm64, Apple clang, the platform the Unicorn harness ran on). Run this on
 * every new platform/compiler before trusting bit identity with the original game; a mismatch means
 * the build fuses multiply-adds, uses x87 excess precision or otherwise deviates.
 * Regenerate kat.inc with tools/gen_aa_libm_kat.py. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aa_libm.h"

typedef struct { const char* name; double (*f1)(double); double (*f2)(double, double); } Func;

static const Func funcs[] = {
    {"sin", aa_sin, 0}, {"cos", aa_cos, 0}, {"tan", aa_tan, 0}, {"asin", aa_asin, 0}, {"acos", aa_acos, 0},
    {"atan", aa_atan, 0}, {"atan2", 0, aa_atan2}, {"exp", aa_exp, 0}, {"expm1", aa_expm1, 0}, {"log", aa_log, 0},
    {"log10", aa_log10, 0}, {"pow", 0, aa_pow}, {"sinh", aa_sinh, 0}, {"cosh", aa_cosh, 0}, {"tanh", aa_tanh, 0},
};

typedef struct { const char* name; uint64_t a, b, expected; } Case;
static const Case cases[] = {
#include "kat.inc"
};

static double bits2d(uint64_t u) { double d; memcpy(&d, &u, sizeof d); return d; }
static uint64_t d2bits(double d) { uint64_t u; memcpy(&u, &d, sizeof u); return u; }

int main(void) {
    int failed = 0;
    const size_t n = sizeof cases / sizeof cases[0];
    for (size_t i = 0; i < n; ++i) {
        const Case* c = &cases[i];
        const Func* f = 0;
        for (size_t k = 0; k < sizeof funcs / sizeof funcs[0]; ++k)
            if (strcmp(funcs[k].name, c->name) == 0) f = &funcs[k];
        if (!f) { printf("unknown function %s\n", c->name); return 2; }
        double r = f->f1 ? f->f1(bits2d(c->a)) : f->f2(bits2d(c->a), bits2d(c->b));
        uint64_t got = d2bits(r);
        int nan_ok = (got & 0x7fffffffffffffffull) > 0x7ff0000000000000ull &&
                     (c->expected & 0x7fffffffffffffffull) > 0x7ff0000000000000ull;
        if (got != c->expected && !nan_ok) {
            if (failed < 20)
                printf("FAIL %s(%016llx, %016llx): got %016llx expected %016llx\n", c->name,
                       (unsigned long long)c->a, (unsigned long long)c->b, (unsigned long long)got,
                       (unsigned long long)c->expected);
            ++failed;
        }
    }
    printf("aa_libm self-test: %zu cases, %d failed\n", n, failed);
    return failed ? 1 : 0;
}
