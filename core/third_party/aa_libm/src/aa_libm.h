/* Deterministic double-precision libm for the Amazing Alex remake: FreeBSD msun as shipped in Android's
 * Bionic (2.3–4.2), i.e. the code the original game called on the device. Exact IEEE operations
 * (sqrt, floor, ceil, fmod, frexp, ldexp, modf, fabs) stay on the platform libm. See ../README.md. */
#ifndef AA_LIBM_H
#define AA_LIBM_H

#ifdef __cplusplus
extern "C" {
#endif

double aa_sin(double x);
double aa_cos(double x);
double aa_tan(double x);
double aa_asin(double x);
double aa_acos(double x);
double aa_atan(double x);
double aa_atan2(double y, double x);
double aa_exp(double x);
double aa_expm1(double x);
double aa_log(double x);
double aa_log10(double x);
double aa_pow(double x, double y);
double aa_sinh(double x);
double aa_cosh(double x);
double aa_tanh(double x);

#ifdef __cplusplus
}
#endif

#endif /* AA_LIBM_H */
