#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct PngCodec {
    static inline uint32_t ReadU32Be(const uint8_t* p) noexcept {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    static inline bool IsPng(const uint8_t* b, int n) noexcept {
        return b && n >= 8 &&
            b[0] == 137 && b[1] == 80 && b[2] == 78 && b[3] == 71 &&
            b[4] == 13 && b[5] == 10 && b[6] == 26 && b[7] == 10;
    }

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

    static inline int ChannelsFromColorType(uint8_t ct) noexcept {
        if (ct == 0) return 1;
        if (ct == 2) return 3;
        if (ct == 4) return 2;
        if (ct == 6) return 4;
        return 0;
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count,
                                   int& x, int& y, int& comp, int& bit_depth,
                                   uint8_t& color_type, uint8_t& interlace) noexcept {
        SetError(nullptr);
        if (!IsPng(bytes, byte_count)) return false;

        size_t at = 8;
        bool saw_ihdr = false;
        while (at + 12 <= (size_t)byte_count) {
            const uint32_t len = ReadU32Be(bytes + at);
            const uint32_t type = ReadU32Be(bytes + at + 4);
            at += 8;
            if (at + (size_t)len + 4 > (size_t)byte_count) {
                SetError("bad PNG chunk bounds");
                return false;
            }

            if (type == 0x49484452u) { // IHDR
                if (len != 13) {
                    SetError("bad PNG IHDR");
                    return false;
                }
                x = (int)ReadU32Be(bytes + at + 0);
                y = (int)ReadU32Be(bytes + at + 4);
                bit_depth = (int)bytes[at + 8];
                color_type = bytes[at + 9];
                const uint8_t comp_method = bytes[at + 10];
                const uint8_t filter_method = bytes[at + 11];
                interlace = bytes[at + 12];
                if (x <= 0 || y <= 0 || comp_method != 0 || filter_method != 0 || interlace > 1) {
                    SetError("unsupported PNG header");
                    return false;
                }
                comp = ChannelsFromColorType(color_type);
                if (comp == 0) {
                    SetError("unsupported PNG color type");
                    return false;
                }
                saw_ihdr = true;
                return true;
            }

            at += (size_t)len + 4; // data + crc
        }
        if (!saw_ihdr) SetError("missing PNG IHDR");
        return false;
    }

    static inline uint8_t Paeth(uint8_t a, uint8_t b, uint8_t c) noexcept {
        const int p = int(a) + int(b) - int(c);
        const int pa = p > int(a) ? p - int(a) : int(a) - p;
        const int pb = p > int(b) ? p - int(b) : int(b) - p;
        const int pc = p > int(c) ? p - int(c) : int(c) - p;
        if (pa <= pb && pa <= pc) return a;
        if (pb <= pc) return b;
        return c;
    }

    static inline void* ConvertU8(const uint8_t* src, int x, int y, int src_comp, int req_comp) noexcept {
        if (req_comp == 0 || req_comp == src_comp) {
            const size_t bytes = (size_t)x * (size_t)y * (size_t)src_comp;
            uint8_t* out = (uint8_t*)malloc(bytes);
            if (!out) return nullptr;
            memcpy(out, src, bytes);
            return out;
        }
        if (req_comp < 1 || req_comp > 4 || src_comp < 1 || src_comp > 4) return nullptr;

        const size_t out_bytes = (size_t)x * (size_t)y * (size_t)req_comp;
        uint8_t* out = (uint8_t*)malloc(out_bytes);
        if (!out) return nullptr;

        const size_t px_count = (size_t)x * (size_t)y;
        for (size_t i = 0; i < px_count; ++i) {
            const uint8_t* s = src + i * (size_t)src_comp;
            uint8_t* d = out + i * (size_t)req_comp;
            const uint8_t r = src_comp >= 3 ? s[0] : s[0];
            const uint8_t g = src_comp >= 3 ? s[1] : s[0];
            const uint8_t b = src_comp >= 3 ? s[2] : s[0];
            const uint8_t a = src_comp == 2 ? s[1] : (src_comp == 4 ? s[3] : 255);

            if (req_comp == 1) {
                d[0] = (uint8_t)(((uint32_t)r * 77u + (uint32_t)g * 150u + (uint32_t)b * 29u) >> 8);
            } else if (req_comp == 2) {
                d[0] = (uint8_t)(((uint32_t)r * 77u + (uint32_t)g * 150u + (uint32_t)b * 29u) >> 8);
                d[1] = a;
            } else if (req_comp == 3) {
                d[0] = r; d[1] = g; d[2] = b;
            } else {
                d[0] = r; d[1] = g; d[2] = b; d[3] = a;
            }
        }
        return out;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        SetError(nullptr);
        int w = 0, h = 0, in_comp = 0, bit_depth = 0;
        uint8_t color_type = 0, interlace = 0;
        if (!ParseHeader(bytes, byte_count, w, h, in_comp, bit_depth, color_type, interlace)) return nullptr;

        if (bit_depth != 8) {
            SetError("PNG clean decoder supports only 8-bit");
            return nullptr;
        }
        if (interlace != 0) {
            SetError("PNG clean decoder supports no interlace");
            return nullptr;
        }

        // Gather IDAT
        uint8_t* idat = nullptr;
        size_t idat_size = 0;
        size_t at = 8;
        while (at + 12 <= (size_t)byte_count) {
            const uint32_t len = ReadU32Be(bytes + at);
            const uint32_t type = ReadU32Be(bytes + at + 4);
            at += 8;
            if (at + (size_t)len + 4 > (size_t)byte_count) {
                free(idat);
                SetError("bad PNG chunk bounds");
                return nullptr;
            }

            if (type == 0x49444154u) { // IDAT
                const size_t new_size = idat_size + (size_t)len;
                uint8_t* next = (uint8_t*)realloc(idat, new_size);
                if (!next) {
                    free(idat);
                    SetError("out of memory");
                    return nullptr;
                }
                idat = next;
                memcpy(idat + idat_size, bytes + at, (size_t)len);
                idat_size = new_size;
            } else if (type == 0x49454E44u) { // IEND
                at += (size_t)len + 4;
                break;
            }

            at += (size_t)len + 4; // data + crc
        }

        if (!idat || idat_size == 0) {
            free(idat);
            SetError("missing PNG IDAT");
            return nullptr;
        }

        const size_t stride = (size_t)w * (size_t)in_comp;
        const size_t raw_guess = (stride + 1u) * (size_t)h;
        int raw_len_i = 0;
        char* raw = stbi::detail::core::zlib_decode_malloc_guesssize_headerflag(
            (const char*)idat, (int)idat_size, (int)raw_guess, &raw_len_i, 1);
        free(idat);
        if (!raw) {
            SetError("zlib decode failed");
            return nullptr;
        }

        const size_t raw_len = (size_t)raw_len_i;
        if (raw_len < raw_guess) {
            free(raw);
            SetError("truncated PNG scanlines");
            return nullptr;
        }

        uint8_t* unpack = (uint8_t*)malloc((size_t)w * (size_t)h * (size_t)in_comp);
        if (!unpack) {
            free(raw);
            SetError("out of memory");
            return nullptr;
        }

        const uint8_t* src = (const uint8_t*)raw;
        for (int row = 0; row < h; ++row) {
            const uint8_t filter = *src++;
            uint8_t* cur = unpack + (size_t)row * stride;
            const uint8_t* prev = row > 0 ? (unpack + (size_t)(row - 1) * stride) : nullptr;

            if (filter == 0) { // none
                memcpy(cur, src, stride);
            } else if (filter == 1) { // sub
                for (size_t i = 0; i < stride; ++i) {
                    const uint8_t left = i >= (size_t)in_comp ? cur[i - (size_t)in_comp] : 0;
                    cur[i] = (uint8_t)(src[i] + left);
                }
            } else if (filter == 2) { // up
                for (size_t i = 0; i < stride; ++i) {
                    const uint8_t up = prev ? prev[i] : 0;
                    cur[i] = (uint8_t)(src[i] + up);
                }
            } else if (filter == 3) { // avg
                for (size_t i = 0; i < stride; ++i) {
                    const uint8_t left = i >= (size_t)in_comp ? cur[i - (size_t)in_comp] : 0;
                    const uint8_t up = prev ? prev[i] : 0;
                    cur[i] = (uint8_t)(src[i] + ((uint32_t(left) + uint32_t(up)) >> 1));
                }
            } else if (filter == 4) { // paeth
                for (size_t i = 0; i < stride; ++i) {
                    const uint8_t a = i >= (size_t)in_comp ? cur[i - (size_t)in_comp] : 0;
                    const uint8_t b = prev ? prev[i] : 0;
                    const uint8_t c = (prev && i >= (size_t)in_comp) ? prev[i - (size_t)in_comp] : 0;
                    cur[i] = (uint8_t)(src[i] + Paeth(a, b, c));
                }
            } else {
                free(unpack);
                free(raw);
                SetError("unsupported PNG filter");
                return nullptr;
            }
            src += stride;
        }
        free(raw);

        void* out = ConvertU8(unpack, w, h, in_comp, req_comp);
        free(unpack);
        if (!out) {
            SetError("PNG channel conversion failed");
            return nullptr;
        }

        if (x) *x = w;
        if (y) *y = h;
        if (comp) *comp = in_comp;
        return out;
    }
};

} // namespace detail
} // namespace stbi
