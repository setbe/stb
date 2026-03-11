#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct TgaCodec {
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

    static inline bool IsTga(const uint8_t* b, int n) noexcept {
        if (!b || n < 18) return false;
        const uint8_t color_map_type = b[1];
        const uint8_t image_type = b[2];
        const uint8_t bpp = b[16];
        const bool type_ok = image_type == 2 || image_type == 3 || image_type == 10 || image_type == 11;
        const bool cmap_ok = color_map_type == 0;
        const bool bpp_ok = bpp == 8 || bpp == 24 || bpp == 32;
        const uint16_t w = (uint16_t)(uint16_t(b[12]) | (uint16_t(b[13]) << 8));
        const uint16_t h = (uint16_t)(uint16_t(b[14]) | (uint16_t(b[15]) << 8));
        return type_ok && cmap_ok && bpp_ok && w > 0 && h > 0;
    }

    static inline bool ParseHeader(const uint8_t* b, int n,
                                   int& x, int& y, int& comp,
                                   uint8_t& image_type, uint8_t& bpp,
                                   bool& top_origin, size_t& data_offset) noexcept {
        SetError(nullptr);
        if (!IsTga(b, n)) return false;

        const uint8_t id_len = b[0];
        image_type = b[2];
        x = (int)(uint16_t(b[12]) | (uint16_t(b[13]) << 8));
        y = (int)(uint16_t(b[14]) | (uint16_t(b[15]) << 8));
        bpp = b[16];
        top_origin = (b[17] & 0x20) != 0;

        if (image_type == 3 || image_type == 11) comp = 1;
        else if (bpp == 24) comp = 3;
        else if (bpp == 32) comp = 4;
        else {
            SetError("unsupported TGA bpp");
            return false;
        }

        data_offset = 18u + (size_t)id_len;
        if (data_offset > (size_t)n) {
            SetError("truncated TGA");
            return false;
        }
        return true;
    }

    static inline bool ReadPixel(const uint8_t* src, int src_comp, uint8_t* dst) noexcept {
        if (src_comp == 1) {
            dst[0] = src[0];
            return true;
        }
        if (src_comp == 3) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            return true;
        }
        if (src_comp == 4) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            return true;
        }
        return false;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        int w = 0, h = 0, src_comp = 0;
        uint8_t image_type = 0, bpp = 0;
        bool top_origin = false;
        size_t at = 0;
        if (!ParseHeader(bytes, byte_count, w, h, src_comp, image_type, bpp, top_origin, at)) return nullptr;

        const size_t src_px_size = (size_t)(bpp / 8u);
        const size_t px_count = (size_t)w * (size_t)h;
        uint8_t* unpack = (uint8_t*)malloc(px_count * (size_t)src_comp);
        if (!unpack) {
            SetError("out of memory");
            return nullptr;
        }

        size_t out_i = 0;
        if (image_type == 2 || image_type == 3) {
            // uncompressed
            const size_t need = px_count * src_px_size;
            if (at + need > (size_t)byte_count) {
                free(unpack);
                SetError("truncated TGA data");
                return nullptr;
            }
            for (size_t i = 0; i < px_count; ++i) {
                uint8_t p[4] = {0, 0, 0, 255};
                if (!ReadPixel(bytes + at, src_comp, p)) {
                    free(unpack);
                    SetError("bad TGA pixel");
                    return nullptr;
                }
                memcpy(unpack + out_i, p, (size_t)src_comp);
                out_i += (size_t)src_comp;
                at += src_px_size;
            }
        } else {
            // RLE (10 / 11)
            while (out_i < px_count * (size_t)src_comp) {
                if (at >= (size_t)byte_count) {
                    free(unpack);
                    SetError("truncated TGA RLE");
                    return nullptr;
                }
                const uint8_t packet = bytes[at++];
                const size_t count = (size_t)(packet & 0x7f) + 1u;
                if (packet & 0x80) {
                    if (at + src_px_size > (size_t)byte_count) {
                        free(unpack);
                        SetError("truncated TGA RLE run");
                        return nullptr;
                    }
                    uint8_t p[4] = {0, 0, 0, 255};
                    if (!ReadPixel(bytes + at, src_comp, p)) {
                        free(unpack);
                        SetError("bad TGA pixel");
                        return nullptr;
                    }
                    at += src_px_size;
                    for (size_t k = 0; k < count; ++k) {
                        if (out_i + (size_t)src_comp > px_count * (size_t)src_comp) break;
                        memcpy(unpack + out_i, p, (size_t)src_comp);
                        out_i += (size_t)src_comp;
                    }
                } else {
                    const size_t need = count * src_px_size;
                    if (at + need > (size_t)byte_count) {
                        free(unpack);
                        SetError("truncated TGA RLE raw");
                        return nullptr;
                    }
                    for (size_t k = 0; k < count; ++k) {
                        uint8_t p[4] = {0, 0, 0, 255};
                        if (!ReadPixel(bytes + at, src_comp, p)) {
                            free(unpack);
                            SetError("bad TGA pixel");
                            return nullptr;
                        }
                        at += src_px_size;
                        if (out_i + (size_t)src_comp > px_count * (size_t)src_comp) break;
                        memcpy(unpack + out_i, p, (size_t)src_comp);
                        out_i += (size_t)src_comp;
                    }
                }
            }
        }

        if (!top_origin && h > 1) {
            const size_t row = (size_t)w * (size_t)src_comp;
            uint8_t* tmp = (uint8_t*)malloc(row);
            if (!tmp) {
                free(unpack);
                SetError("out of memory");
                return nullptr;
            }
            for (int j = 0; j < (h >> 1); ++j) {
                uint8_t* a = unpack + (size_t)j * row;
                uint8_t* b = unpack + (size_t)(h - 1 - j) * row;
                memcpy(tmp, a, row);
                memcpy(a, b, row);
                memcpy(b, tmp, row);
            }
            free(tmp);
        }

        void* out = PngCodec::ConvertU8(unpack, w, h, src_comp, req_comp);
        free(unpack);
        if (!out) {
            SetError("TGA channel conversion failed");
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
