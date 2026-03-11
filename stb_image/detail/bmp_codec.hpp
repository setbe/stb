#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct BmpCodec {
    static inline const char*& LastError() noexcept {
        static const char* e = nullptr;
        return e;
    }

    static inline void SetError(const char* s) noexcept {
        LastError() = s;
    }

    static inline const char* FailureReason() noexcept {
        return LastError();
    }

    static inline bool IsBmp(const uint8_t* b, int n) noexcept {
        return b && n >= 2 && b[0] == 'B' && b[1] == 'M';
    }

    static inline uint16_t ReadU16Le(const uint8_t* p) noexcept {
        return (uint16_t)(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
    }

    static inline uint32_t ReadU32Le(const uint8_t* p) noexcept {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    static inline int32_t ReadS32Le(const uint8_t* p) noexcept {
        return (int32_t)ReadU32Le(p);
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count,
                                   int& x, int& y, int& comp, int& bpp,
                                   uint32_t& pixel_offset, bool& flip_y) noexcept {
        SetError(nullptr);
        if (!IsBmp(bytes, byte_count) || byte_count < 54) return false;

        pixel_offset = ReadU32Le(bytes + 10);
        const uint32_t dib_size = ReadU32Le(bytes + 14);
        if (dib_size < 40 || (size_t)14 + (size_t)dib_size > (size_t)byte_count) {
            SetError("unsupported BMP DIB header");
            return false;
        }

        const int32_t w = ReadS32Le(bytes + 18);
        const int32_t h_raw = ReadS32Le(bytes + 22);
        const uint16_t planes = ReadU16Le(bytes + 26);
        const uint16_t bits = ReadU16Le(bytes + 28);
        const uint32_t compression = ReadU32Le(bytes + 30);

        if (w <= 0 || h_raw == 0 || planes != 1) {
            SetError("bad BMP header");
            return false;
        }
        if (compression != 0) {
            SetError("unsupported BMP compression");
            return false;
        }
        if (bits != 24 && bits != 32) {
            SetError("BMP clean decoder supports only 24/32-bit");
            return false;
        }

        x = (int)w;
        y = h_raw > 0 ? (int)h_raw : (int)(-h_raw);
        bpp = (int)bits;
        comp = bits == 32 ? 4 : 3;
        flip_y = h_raw > 0;
        return true;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        int w = 0, h = 0, src_comp = 0, bpp = 0;
        uint32_t pixel_offset = 0;
        bool flip_y = false;
        if (!ParseHeader(bytes, byte_count, w, h, src_comp, bpp, pixel_offset, flip_y)) return nullptr;

        const size_t src_row = bpp == 24
            ? (((size_t)w * 3u + 3u) & ~size_t(3))
            : (size_t)w * 4u;
        const size_t need = (size_t)h * src_row;
        if ((size_t)pixel_offset + need > (size_t)byte_count) {
            SetError("truncated BMP data");
            return nullptr;
        }

        const size_t unpack_bytes = (size_t)w * (size_t)h * (size_t)src_comp;
        uint8_t* unpack = (uint8_t*)malloc(unpack_bytes);
        if (!unpack) {
            SetError("out of memory");
            return nullptr;
        }

        for (int row = 0; row < h; ++row) {
            const int src_row_idx = flip_y ? (h - 1 - row) : row;
            const uint8_t* src = bytes + pixel_offset + (size_t)src_row_idx * src_row;
            uint8_t* dst = unpack + (size_t)row * (size_t)w * (size_t)src_comp;

            if (bpp == 24) {
                for (int i = 0; i < w; ++i) {
                    const uint8_t b = src[i * 3 + 0];
                    const uint8_t g = src[i * 3 + 1];
                    const uint8_t r = src[i * 3 + 2];
                    dst[i * 3 + 0] = r;
                    dst[i * 3 + 1] = g;
                    dst[i * 3 + 2] = b;
                }
            } else {
                for (int i = 0; i < w; ++i) {
                    const uint8_t b = src[i * 4 + 0];
                    const uint8_t g = src[i * 4 + 1];
                    const uint8_t r = src[i * 4 + 2];
                    const uint8_t a = src[i * 4 + 3];
                    dst[i * 4 + 0] = r;
                    dst[i * 4 + 1] = g;
                    dst[i * 4 + 2] = b;
                    dst[i * 4 + 3] = a;
                }
            }
        }

        void* out = PngCodec::ConvertU8(unpack, w, h, src_comp, req_comp);
        free(unpack);
        if (!out) {
            SetError("BMP channel conversion failed");
            return nullptr;
        }

        if (x) *x = w;
        if (y) *y = h;
        if (comp) *comp = src_comp;
        return out;
    }
};

} // namespace detail
} // namespace stbi
