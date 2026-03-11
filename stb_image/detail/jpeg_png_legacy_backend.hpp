#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// We intentionally disable SIMD in this legacy backend path to keep
// freestanding builds deterministic and avoid platform-specific intrinsics.
#ifndef STBI_NO_SIMD
#define STBI_NO_SIMD
#endif

namespace stbi { namespace detail { namespace core {

typedef unsigned char uc;
typedef unsigned short us;
typedef uint16_t uint16;
typedef int16_t int16;
typedef uint32_t uint32;
typedef int32_t int32;

struct context {
    uint32 x{};
    uint32 y{};
    int n{};
    int out_n{};

    uc* buffer{};
    uc* buffer_end{};
    uc* buffer_original{};
    uc* buffer_original_end{};
};

struct result_info {
    int bits_per_channel{};
    int num_channels{};
    int channel_order{};
};

enum {
    STBI_ORDER_RGB,
    STBI_ORDER_BGR
};

enum {
    STBI__SCAN_load = 0,
    STBI__SCAN_type,
    STBI__SCAN_header
};

inline const char*& failure_reason_ref() noexcept {
    static const char* g = "";
    return g;
}

inline const char* failure_reason() noexcept {
    return failure_reason_ref();
}

inline void set_failure_reason(const char* s) noexcept {
    failure_reason_ref() = s ? s : "";
}

inline int err(const char* primary, const char* secondary) noexcept {
    set_failure_reason((primary && primary[0]) ? primary : secondary);
    return 0;
}

inline unsigned char* errpuc(const char* primary, const char* secondary) noexcept {
    err(primary, secondary);
    return NULL;
}

inline float* errpf(const char* primary, const char* secondary) noexcept {
    err(primary, secondary);
    return NULL;
}

inline void* realloc_sized(void* p, size_t old_size, size_t new_size) noexcept {
    (void)old_size;
    return realloc(p, new_size);
}

inline int at_eof(context* s) noexcept {
    return s->buffer >= s->buffer_end;
}

inline void start_mem(context* s, const uc* buffer, int len) noexcept {
    s->buffer = (uc*)buffer;
    s->buffer_end = (uc*)buffer + (len > 0 ? len : 0);
    s->buffer_original = s->buffer;
    s->buffer_original_end = s->buffer_end;
}

inline void rewind(context* s) noexcept {
    s->buffer = s->buffer_original;
    s->buffer_end = s->buffer_original_end;
}

inline int get8(context* s) noexcept {
    if (s->buffer < s->buffer_end) return *s->buffer++;
    return 0;
}

inline int getn(context* s, uc* dst, int n) noexcept {
    if (n <= 0) return 1;
    if ((size_t)(s->buffer_end - s->buffer) < (size_t)n) {
        if (s->buffer < s->buffer_end && dst) {
            const size_t avail = (size_t)(s->buffer_end - s->buffer);
            memcpy(dst, s->buffer, avail);
        }
        s->buffer = s->buffer_end;
        return 0;
    }
    if (dst) memcpy(dst, s->buffer, (size_t)n);
    s->buffer += n;
    return 1;
}

inline void skip(context* s, int n) noexcept {
    if (n <= 0) return;
    if ((size_t)(s->buffer_end - s->buffer) < (size_t)n) {
        s->buffer = s->buffer_end;
        return;
    }
    s->buffer += n;
}

inline uint16 get16be(context* s) noexcept {
    int z = get8(s);
    return (uint16)((z << 8) + get8(s));
}

inline uint32 get32be(context* s) noexcept {
    uint32 z = (uint32)get16be(s);
    return (z << 16) + get16be(s);
}

inline uint16 get16le(context* s) noexcept {
    int z = get8(s);
    return (uint16)(z + (get8(s) << 8));
}

inline uint32 get32le(context* s) noexcept {
    uint32 z = get16le(s);
    return z + ((uint32)get16le(s) << 16);
}

inline int addsizes_valid(int a, int b) noexcept {
    if (b < 0) return 0;
    return a <= INT_MAX - b;
}

inline int mul2sizes_valid(int a, int b) noexcept {
    if (a < 0 || b < 0) return 0;
    if (b == 0) return 1;
    return a <= INT_MAX / b;
}

inline int mad2sizes_valid(int a, int b, int add) noexcept {
    return mul2sizes_valid(a, b) && addsizes_valid(a * b, add);
}

inline int mad3sizes_valid(int a, int b, int c, int add) noexcept {
    return mul2sizes_valid(a, b) && mul2sizes_valid(a * b, c) && addsizes_valid(a * b * c, add);
}

inline int mad4sizes_valid(int a, int b, int c, int d, int add) noexcept {
    return mul2sizes_valid(a, b) && mul2sizes_valid(a * b, c) && mul2sizes_valid(a * b * c, d) &&
           addsizes_valid(a * b * c * d, add);
}

inline void* malloc_mad2(int a, int b, int add) noexcept {
    if (!mad2sizes_valid(a, b, add)) return NULL;
    return malloc((size_t)(a * b + add));
}

inline void* malloc_mad3(int a, int b, int c, int add) noexcept {
    if (!mad3sizes_valid(a, b, c, add)) return NULL;
    return malloc((size_t)(a * b * c + add));
}

inline void* malloc_mad4(int a, int b, int c, int d, int add) noexcept {
    if (!mad4sizes_valid(a, b, c, d, add)) return NULL;
    return malloc((size_t)(a * b * c * d + add));
}

inline int addints_valid(int a, int b) noexcept {
    if ((a >= 0) != (b >= 0)) return 1;
    if (a < 0 && b < 0) return a >= INT_MIN - b;
    return a <= INT_MAX - b;
}

inline int mul2shorts_valid(int a, int b) noexcept {
    if (b == 0 || b == -1) return 1;
    if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX / b;
    if (b < 0) return a <= SHRT_MIN / b;
    return a >= SHRT_MIN / b;
}

inline uc compute_y(int r, int g, int b) noexcept {
    return (uc)(((r * 77) + (g * 150) + (b * 29)) >> 8);
}

inline uint16 compute_y_16(int r, int g, int b) noexcept {
    return (uint16)(((r * 77) + (g * 150) + (b * 29)) >> 8);
}

inline unsigned char* convert_format(unsigned char* data, int img_n, int req_comp,
                                     unsigned int x, unsigned int y) noexcept {
    int i, j;
    unsigned char* good;

    if (req_comp == img_n) return data;
    if (req_comp < 1 || req_comp > 4 || img_n < 1 || img_n > 4) {
        free(data);
        return (unsigned char*)((size_t)(err("unsupported", "Unsupported format conversion") ? NULL : NULL));
    }

    good = (unsigned char*)malloc_mad3(req_comp, (int)x, (int)y, 0);
    if (!good) {
        free(data);
        return (unsigned char*)((size_t)(err("outofmem", "Out of memory") ? NULL : NULL));
    }

    for (j = 0; j < (int)y; ++j) {
        unsigned char* src = data + (size_t)j * x * (size_t)img_n;
        unsigned char* dest = good + (size_t)j * x * (size_t)req_comp;

#define STBIX__COMBO(a, b) ((a) * 8 + (b))
#define STBIX__CASE(a, b) case STBIX__COMBO(a, b): for (i = (int)x - 1; i >= 0; --i, src += (a), dest += (b))
        switch (STBIX__COMBO(img_n, req_comp)) {
            STBIX__CASE(1, 2) { dest[0] = src[0]; dest[1] = 255; } break;
            STBIX__CASE(1, 3) { dest[0] = dest[1] = dest[2] = src[0]; } break;
            STBIX__CASE(1, 4) { dest[0] = dest[1] = dest[2] = src[0]; dest[3] = 255; } break;
            STBIX__CASE(2, 1) { dest[0] = src[0]; } break;
            STBIX__CASE(2, 3) { dest[0] = dest[1] = dest[2] = src[0]; } break;
            STBIX__CASE(2, 4) { dest[0] = dest[1] = dest[2] = src[0]; dest[3] = src[1]; } break;
            STBIX__CASE(3, 4) { dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2]; dest[3] = 255; } break;
            STBIX__CASE(3, 1) { dest[0] = compute_y(src[0], src[1], src[2]); } break;
            STBIX__CASE(3, 2) { dest[0] = compute_y(src[0], src[1], src[2]); dest[1] = 255; } break;
            STBIX__CASE(4, 1) { dest[0] = compute_y(src[0], src[1], src[2]); } break;
            STBIX__CASE(4, 2) { dest[0] = compute_y(src[0], src[1], src[2]); dest[1] = src[3]; } break;
            STBIX__CASE(4, 3) { dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2]; } break;
            default: free(data); free(good); return (unsigned char*)((size_t)(err("unsupported", "Unsupported format conversion") ? NULL : NULL));
        }
#undef STBIX__CASE
#undef STBIX__COMBO
    }

    free(data);
    return good;
}

inline uint16* convert_format16(uint16* data, int img_n, int req_comp,
                                unsigned int x, unsigned int y) noexcept {
    int i, j;
    uint16* good;

    if (req_comp == img_n) return data;
    if (req_comp < 1 || req_comp > 4 || img_n < 1 || img_n > 4) {
        free(data);
        return (uint16*)((size_t)(err("unsupported", "Unsupported format conversion") ? NULL : NULL));
    }

    good = (uint16*)malloc((size_t)req_comp * x * y * 2u);
    if (!good) {
        free(data);
        return (uint16*)((size_t)(err("outofmem", "Out of memory") ? NULL : NULL));
    }

    for (j = 0; j < (int)y; ++j) {
        uint16* src = data + (size_t)j * x * (size_t)img_n;
        uint16* dest = good + (size_t)j * x * (size_t)req_comp;

#define STBIX__COMBO(a, b) ((a) * 8 + (b))
#define STBIX__CASE(a, b) case STBIX__COMBO(a, b): for (i = (int)x - 1; i >= 0; --i, src += (a), dest += (b))
        switch (STBIX__COMBO(img_n, req_comp)) {
            STBIX__CASE(1, 2) { dest[0] = src[0]; dest[1] = 0xffff; } break;
            STBIX__CASE(1, 3) { dest[0] = dest[1] = dest[2] = src[0]; } break;
            STBIX__CASE(1, 4) { dest[0] = dest[1] = dest[2] = src[0]; dest[3] = 0xffff; } break;
            STBIX__CASE(2, 1) { dest[0] = src[0]; } break;
            STBIX__CASE(2, 3) { dest[0] = dest[1] = dest[2] = src[0]; } break;
            STBIX__CASE(2, 4) { dest[0] = dest[1] = dest[2] = src[0]; dest[3] = src[1]; } break;
            STBIX__CASE(3, 4) { dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2]; dest[3] = 0xffff; } break;
            STBIX__CASE(3, 1) { dest[0] = compute_y_16(src[0], src[1], src[2]); } break;
            STBIX__CASE(3, 2) { dest[0] = compute_y_16(src[0], src[1], src[2]); dest[1] = 0xffff; } break;
            STBIX__CASE(4, 1) { dest[0] = compute_y_16(src[0], src[1], src[2]); } break;
            STBIX__CASE(4, 2) { dest[0] = compute_y_16(src[0], src[1], src[2]); dest[1] = src[3]; } break;
            STBIX__CASE(4, 3) { dest[0] = src[0]; dest[1] = src[1]; dest[2] = src[2]; } break;
            default: free(data); free(good); return (uint16*)((size_t)(err("unsupported", "Unsupported format conversion") ? NULL : NULL));
        }
#undef STBIX__CASE
#undef STBIX__COMBO
    }

    free(data);
    return good;
}

// Forward declarations consumed by wrappers at the bottom of png.hpp/jpeg.hpp
struct PngFormatModule {
    static int Test(context* s) noexcept;
    static void* Load(context* s, int* x, int* y, int* comp, int req_comp, result_info* ri) noexcept;
    static int Info(context* s, int* x, int* y, int* comp) noexcept;
    static int Is16(context* s) noexcept;
};

struct JpegFormatModule {
    static int Test(context* s) noexcept;
    static void* Load(context* s, int* x, int* y, int* comp, int req_comp, result_info* ri) noexcept;
    static int Info(context* s, int* x, int* y, int* comp) noexcept;
};

} } } // namespace stbi::detail::core

#ifndef STBIDEF
#define STBIDEF static inline
#endif
#ifndef STBI_ASSERT
#define STBI_ASSERT(x) ((void)0)
#endif
#ifndef STBI_NOTUSED
#define STBI_NOTUSED(v) (void)(v)
#endif
#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif
#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)
#endif
#ifndef STBI__BYTECAST
#define STBI__BYTECAST(x) ((stbi::detail::core::uc)((x) & 255))
#endif
#ifndef lrot
#define lrot(x, y) (((x) << (y)) | ((x) >> (-(y) & 31)))
#endif

#include "zlib.hpp"
#include "png.hpp"
#ifdef STBI__IDCT_1D
#undef STBI__IDCT_1D
#endif
#include "jpeg.hpp"

namespace stbi { namespace detail {

struct PngLegacyBackend {
#ifdef STBI_NO_PNG
    static inline bool IsPng(const uint8_t* b, int n) noexcept {
        (void)b;
        (void)n;
        return false;
    }

    static inline bool Info(const uint8_t* bytes, int byte_count, int* x, int* y, int* comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        return false;
    }

    static inline bool Is16Bit(const uint8_t* bytes, int byte_count) noexcept {
        (void)bytes;
        (void)byte_count;
        return false;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        (void)req_comp;
        return nullptr;
    }

    static inline void* LoadU16(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        (void)req_comp;
        return nullptr;
    }

    static inline void* LoadF32(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        (void)req_comp;
        return nullptr;
    }

    static inline const char* FailureReason() noexcept {
        return "";
    }
#else
    static inline bool IsPng(const uint8_t* b, int n) noexcept {
        return b && n >= 8 &&
               b[0] == 137 && b[1] == 80 && b[2] == 78 && b[3] == 71 &&
               b[4] == 13 && b[5] == 10 && b[6] == 26 && b[7] == 10;
    }

    static inline bool Info(const uint8_t* bytes, int byte_count, int* x, int* y, int* comp) noexcept {
        core::context s{};
        core::start_mem(&s, (const core::uc*)bytes, byte_count);
        return core::png_info(&s, x, y, comp) != 0;
    }

    static inline bool Is16Bit(const uint8_t* bytes, int byte_count) noexcept {
        core::context s{};
        core::start_mem(&s, (const core::uc*)bytes, byte_count);
        return core::png_is16(&s) != 0;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        core::context s{};
        core::result_info ri{};
        core::start_mem(&s, (const core::uc*)bytes, byte_count);
        return core::png_load(&s, x, y, comp, req_comp, &ri);
    }

    static inline void* LoadU16(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        core::context s{};
        core::result_info ri{};
        core::start_mem(&s, (const core::uc*)bytes, byte_count);
        void* p = core::png_load(&s, x, y, comp, req_comp, &ri);
        if (!p) return nullptr;

        const int out_comp = req_comp ? req_comp : (comp ? *comp : 0);
        if (out_comp <= 0) {
            free(p);
            core::set_failure_reason("bad req_comp");
            return nullptr;
        }

        if (ri.bits_per_channel == 16) return p;

        const size_t count = (size_t)(*x) * (size_t)(*y) * (size_t)out_comp;
        uint16_t* out = (uint16_t*)malloc(count * sizeof(uint16_t));
        if (!out) {
            free(p);
            core::set_failure_reason("outofmem");
            return nullptr;
        }

        const uint8_t* src = (const uint8_t*)p;
        for (size_t i = 0; i < count; ++i) {
            out[i] = (uint16_t)((uint16_t(src[i]) << 8) | uint16_t(src[i]));
        }
        free(p);
        return out;
    }

    static inline void* LoadF32(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        const int out_comp = req_comp ? req_comp : 0;
        int tx = 0, ty = 0, tc = 0;
        uint8_t* u8 = (uint8_t*)LoadU8(bytes, byte_count, &tx, &ty, &tc, out_comp);
        if (!u8) return nullptr;

        const int oc = out_comp ? out_comp : tc;
        const size_t count = (size_t)tx * (size_t)ty * (size_t)oc;
        float* f = (float*)malloc(count * sizeof(float));
        if (!f) {
            free(u8);
            core::set_failure_reason("outofmem");
            return nullptr;
        }
        for (size_t i = 0; i < count; ++i) {
            f[i] = (float)u8[i] / 255.0f;
        }
        free(u8);

        if (x) *x = tx;
        if (y) *y = ty;
        if (comp) *comp = tc;
        return f;
    }

    static inline const char* FailureReason() noexcept {
        return core::failure_reason();
    }
#endif
};

struct JpegLegacyBackend {
#ifdef STBI_NO_JPEG
    static inline bool IsJpeg(const uint8_t* b, int n) noexcept {
        (void)b;
        (void)n;
        return false;
    }

    static inline bool Info(const uint8_t* bytes, int byte_count, int* x, int* y, int* comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        return false;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        (void)req_comp;
        return nullptr;
    }

    static inline void* LoadU16(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        (void)req_comp;
        return nullptr;
    }

    static inline void* LoadF32(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        (void)bytes;
        (void)byte_count;
        (void)x;
        (void)y;
        (void)comp;
        (void)req_comp;
        return nullptr;
    }

    static inline const char* FailureReason() noexcept {
        return "";
    }
#else
    static inline bool IsJpeg(const uint8_t* b, int n) noexcept {
        return b && n >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff;
    }

    static inline bool Info(const uint8_t* bytes, int byte_count, int* x, int* y, int* comp) noexcept {
        core::context s{};
        core::start_mem(&s, (const core::uc*)bytes, byte_count);
        return core::jpeg_info(&s, x, y, comp) != 0;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        core::context s{};
        core::result_info ri{};
        core::start_mem(&s, (const core::uc*)bytes, byte_count);
        return core::jpeg_load(&s, x, y, comp, req_comp, &ri);
    }

    static inline void* LoadU16(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        const int out_comp = req_comp ? req_comp : 0;
        int tx = 0, ty = 0, tc = 0;
        uint8_t* u8 = (uint8_t*)LoadU8(bytes, byte_count, &tx, &ty, &tc, out_comp);
        if (!u8) return nullptr;

        const int oc = out_comp ? out_comp : tc;
        const size_t count = (size_t)tx * (size_t)ty * (size_t)oc;
        uint16_t* out = (uint16_t*)malloc(count * sizeof(uint16_t));
        if (!out) {
            free(u8);
            core::set_failure_reason("outofmem");
            return nullptr;
        }
        for (size_t i = 0; i < count; ++i) {
            out[i] = (uint16_t)((uint16_t(u8[i]) << 8) | uint16_t(u8[i]));
        }
        free(u8);

        if (x) *x = tx;
        if (y) *y = ty;
        if (comp) *comp = tc;
        return out;
    }

    static inline void* LoadF32(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        const int out_comp = req_comp ? req_comp : 0;
        int tx = 0, ty = 0, tc = 0;
        uint8_t* u8 = (uint8_t*)LoadU8(bytes, byte_count, &tx, &ty, &tc, out_comp);
        if (!u8) return nullptr;

        const int oc = out_comp ? out_comp : tc;
        const size_t count = (size_t)tx * (size_t)ty * (size_t)oc;
        float* f = (float*)malloc(count * sizeof(float));
        if (!f) {
            free(u8);
            core::set_failure_reason("outofmem");
            return nullptr;
        }
        for (size_t i = 0; i < count; ++i) {
            f[i] = (float)u8[i] / 255.0f;
        }
        free(u8);

        if (x) *x = tx;
        if (y) *y = ty;
        if (comp) *comp = tc;
        return f;
    }

    static inline const char* FailureReason() noexcept {
        return core::failure_reason();
    }
#endif
};

} } // namespace stbi::detail

#undef STBIDEF
#undef STBI_ASSERT
#undef STBI_NOTUSED
#undef STBI_SIMD_ALIGN
#undef STBI_MAX_DIMENSIONS
#undef STBI__BYTECAST
#undef lrot
