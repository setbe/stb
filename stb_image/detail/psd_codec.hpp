#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct PsdCodec {
    struct Header {
        int channel_count{};
        int width{};
        int height{};
        int bit_depth{};
        int color_mode{};
        int compression{};
        size_t image_data_offset{};
    };

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

    static inline uint16_t ReadU16Be(const uint8_t* p) noexcept {
        return (uint16_t)((uint16_t(p[0]) << 8) | uint16_t(p[1]));
    }

    static inline uint32_t ReadU32Be(const uint8_t* p) noexcept {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    static inline bool IsPsd(const uint8_t* b, int n) noexcept {
        return b && n >= 4 && b[0] == '8' && b[1] == 'B' && b[2] == 'P' && b[3] == 'S';
    }

    static inline bool SkipChecked(size_t& at, size_t n, size_t len) noexcept {
        if (at + n > len) return false;
        at += n;
        return true;
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count, Header& out) noexcept {
        SetError(nullptr);
        if (!IsPsd(bytes, byte_count)) return false;
        if (byte_count < 26) {
            SetError("truncated PSD header");
            return false;
        }

        const size_t len = (size_t)byte_count;
        size_t at = 0;

        if (!SkipChecked(at, 4, len)) return false; // signature
        if (ReadU16Be(bytes + at) != 1) {
            SetError("unsupported PSD version");
            return false;
        }
        at += 2;
        if (!SkipChecked(at, 6, len)) {
            SetError("truncated PSD header");
            return false;
        }

        const int channel_count = (int)ReadU16Be(bytes + at); at += 2;
        const int height = (int)ReadU32Be(bytes + at); at += 4;
        const int width = (int)ReadU32Be(bytes + at); at += 4;
        const int bit_depth = (int)ReadU16Be(bytes + at); at += 2;
        const int color_mode = (int)ReadU16Be(bytes + at); at += 2;

        if (channel_count < 0 || channel_count > 16) {
            SetError("unsupported PSD channel count");
            return false;
        }
        if (width <= 0 || height <= 0) {
            SetError("bad PSD dimensions");
            return false;
        }
        if (bit_depth != 8 && bit_depth != 16) {
            SetError("unsupported PSD bit depth");
            return false;
        }
        if (color_mode != 3) {
            SetError("unsupported PSD color mode");
            return false;
        }

        if (at + 4 > len) {
            SetError("truncated PSD mode data");
            return false;
        }
        const uint32_t mode_len = ReadU32Be(bytes + at); at += 4;
        if (!SkipChecked(at, (size_t)mode_len, len)) {
            SetError("truncated PSD mode data");
            return false;
        }

        if (at + 4 > len) {
            SetError("truncated PSD resources");
            return false;
        }
        const uint32_t resources_len = ReadU32Be(bytes + at); at += 4;
        if (!SkipChecked(at, (size_t)resources_len, len)) {
            SetError("truncated PSD resources");
            return false;
        }

        if (at + 4 > len) {
            SetError("truncated PSD reserved data");
            return false;
        }
        const uint32_t reserved_len = ReadU32Be(bytes + at); at += 4;
        if (!SkipChecked(at, (size_t)reserved_len, len)) {
            SetError("truncated PSD reserved data");
            return false;
        }

        if (at + 2 > len) {
            SetError("truncated PSD compression");
            return false;
        }
        const int compression = (int)ReadU16Be(bytes + at); at += 2;
        if (compression < 0 || compression > 1) {
            SetError("unsupported PSD compression");
            return false;
        }

        Header h{};
        h.channel_count = channel_count;
        h.width = width;
        h.height = height;
        h.bit_depth = bit_depth;
        h.color_mode = color_mode;
        h.compression = compression;
        h.image_data_offset = at;
        out = h;
        return true;
    }

    static inline bool DecodeRleChannel(const uint8_t* bytes, size_t len, size_t& at,
                                        uint8_t* dst, int pixel_count) noexcept {
        int count = 0;
        while (count < pixel_count) {
            if (at >= len) {
                SetError("truncated PSD RLE stream");
                return false;
            }
            int nleft = pixel_count - count;
            int code = (int)bytes[at++];

            if (code == 128) {
                continue;
            } else if (code < 128) {
                int run = code + 1;
                if (run > nleft || at + (size_t)run > len) {
                    SetError("corrupt PSD RLE literal");
                    return false;
                }
                count += run;
                while (run--) {
                    *dst = bytes[at++];
                    dst += 4;
                }
            } else {
                int run = 257 - code;
                if (run > nleft || at >= len) {
                    SetError("corrupt PSD RLE repeat");
                    return false;
                }
                const uint8_t v = bytes[at++];
                count += run;
                while (run--) {
                    *dst = v;
                    dst += 4;
                }
            }
        }
        return true;
    }

    static inline void RemoveWhiteMatte8(uint8_t* rgba, size_t pixel_count) noexcept {
        for (size_t i = 0; i < pixel_count; ++i) {
            uint8_t* p = rgba + i * 4u;
            if (p[3] != 0 && p[3] != 255) {
                const float a = p[3] / 255.0f;
                const float ra = 1.0f / a;
                const float inv_a = 255.0f * (1.0f - ra);
                p[0] = (uint8_t)(p[0] * ra + inv_a);
                p[1] = (uint8_t)(p[1] * ra + inv_a);
                p[2] = (uint8_t)(p[2] * ra + inv_a);
            }
        }
    }

    static inline bool ReadRawChannel8(const uint8_t* bytes, size_t len, size_t& at,
                                       uint8_t* dst, int pixel_count, int bit_depth) noexcept {
        if (bit_depth == 16) {
            const size_t need = (size_t)pixel_count * 2u;
            if (at + need > len) {
                SetError("truncated PSD 16-bit channel");
                return false;
            }
            for (int i = 0; i < pixel_count; ++i) {
                const uint16_t v = ReadU16Be(bytes + at);
                at += 2;
                *dst = (uint8_t)(v >> 8);
                dst += 4;
            }
            return true;
        }

        const size_t need = (size_t)pixel_count;
        if (at + need > len) {
            SetError("truncated PSD 8-bit channel");
            return false;
        }
        for (int i = 0; i < pixel_count; ++i) {
            *dst = bytes[at++];
            dst += 4;
        }
        return true;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        Header h{};
        if (!ParseHeader(bytes, byte_count, h)) return nullptr;

        const size_t pixel_count = (size_t)h.width * (size_t)h.height;
        const size_t out_bytes = pixel_count * 4u;
        uint8_t* rgba = (uint8_t*)malloc(out_bytes ? out_bytes : 1u);
        if (!rgba) {
            SetError("out of memory");
            return nullptr;
        }

        size_t at = h.image_data_offset;
        const size_t len = (size_t)byte_count;

        if (h.compression == 1) {
            const size_t row_table_bytes = (size_t)h.height * (size_t)h.channel_count * 2u;
            if (at + row_table_bytes > len) {
                free(rgba);
                SetError("truncated PSD RLE row table");
                return nullptr;
            }
            at += row_table_bytes;
        }

        for (int channel = 0; channel < 4; ++channel) {
            uint8_t* dst = rgba + channel;
            if (channel >= h.channel_count) {
                const uint8_t v = (channel == 3) ? 255 : 0;
                for (size_t i = 0; i < pixel_count; ++i) {
                    *dst = v;
                    dst += 4;
                }
                continue;
            }

            bool ok = false;
            if (h.compression == 1) {
                ok = DecodeRleChannel(bytes, len, at, dst, (int)pixel_count);
            } else {
                ok = ReadRawChannel8(bytes, len, at, dst, (int)pixel_count, h.bit_depth);
            }
            if (!ok) {
                free(rgba);
                return nullptr;
            }
        }

        if (h.channel_count >= 4) {
            RemoveWhiteMatte8(rgba, pixel_count);
        }

        void* out = PngCodec::ConvertU8(rgba, h.width, h.height, 4, req_comp);
        free(rgba);
        if (!out) {
            SetError("PSD channel conversion failed");
            return nullptr;
        }

        if (x) *x = h.width;
        if (y) *y = h.height;
        if (comp) *comp = 4;
        return out;
    }

    static inline bool Is16Bit(const uint8_t* bytes, int byte_count) noexcept {
        Header h{};
        if (!ParseHeader(bytes, byte_count, h)) return false;
        return h.bit_depth == 16;
    }
};

} // namespace detail
} // namespace stbi
