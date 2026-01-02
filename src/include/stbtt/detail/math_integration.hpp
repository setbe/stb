#pragma once
// ----------------------------------------------
// Fallbacks for completely math.h-free environments
// (only compiled if STBTT_FREESTANDING is defined
//  and user hasn't provided replacements)
// ----------------------------------------------
#ifdef STBTT_FREESTANDING

#ifndef STBTT_ifloor
static inline int STBTT_ifloor(float x) noexcept {
    return (int)(x >= 0 ? (int)x : (int)x - (x != (int)x));
}
#   define STBTT_ifloor(x) STBTT_ifloor(x)
#endif

#ifndef STBTT_iceil
static inline int STBTT_iceil(float x) noexcept {
    int i = (int)x; return (x > i) ? i + 1 : i;
}
#   define STBTT_iceil(x) STBTT_iceil(x)
#endif

#ifndef STBTT_fabs
static inline float STBTT_fabs(float x) noexcept { return x < 0 ? -x : x; }
#define STBTT_fabs(x) STBTT_fabs(x)
#endif

#ifndef STBTT_sqrt
// Basic Newton-Raphson sqrt approximation
static inline float STBTT_sqrt(float x) noexcept {
    if (x <= 0) return 0;
    float r = x;
    for (int i = 0; i < 5; ++i)
        r = 0.5f * (r + x / r);
    return r;
}
#define STBTT_sqrt(x) STBTT_sqrt(x)
#endif

#ifndef STBTT_pow
static inline float STBTT_pow(float base, float exp) noexcept {
    // crude exp/log approximation (only for small exp)
    float result = 1.0f;
    int e = (int)exp;
    for (int i = 0; i < e; ++i)
        result *= base;
    return result;
}
#define STBTT_pow(x,y) STBTT_pow(x,y)
#endif

#ifndef STBTT_fmod
static inline float STBTT_fmod(float x, float y) noexcept {
    return x - (int)(x / y) * y;
}
#    define STBTT_fmod(x,y) STBTT_fmod(x,y)
#endif

#ifndef STBTT_cos
// Taylor approximation of cos(x) for small angles
static inline float STBTT_cos(float x) noexcept {
    const float PI = 3.14159265358979323846f;
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    float x2 = x * x;
    return 1.0f - x2 / 2 + x2 * x2 / 24 - x2 * x2 * x2 / 720;
}
#    define STBTT_cos(x) STBTT_cos(x)
#endif

#ifndef STBTT_acos
// Rough acos approximation
static inline float STBTT_acos(float x) noexcept {
    // Clamp
    if (x < -1) x = -1;
    if (x > 1) x = 1;
    // Polynomial approximation
    float negate = x < 0;
    x = STBTT_fabs(x);
    float ret = -0.0187293f;
    ret = ret * x + 0.0742610f;
    ret = ret * x - 0.2121144f;
    ret = ret * x + 1.5707288f;
    ret = ret * STBTT_sqrt(1.0f - x);
    return negate ? 3.14159265f - ret : ret;
}
#    define STBTT_acos(x) STBTT_acos(x)
#endif

#endif // STBTT_FREESTANDING