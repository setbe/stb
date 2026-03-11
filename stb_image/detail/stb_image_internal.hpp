#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jpeg_png_legacy_backend.hpp"
#include "png_codec.hpp"
#include "bmp_codec.hpp"
#include "gif_codec.hpp"
#include "psd_codec.hpp"
#include "pic_codec.hpp"
#include "pnm_codec.hpp"
#include "hdr_codec.hpp"
#include "tga_codec.hpp"

namespace stbi { namespace detail {

struct InternalImageBackend {
private:
    enum class FormatTag : uint8_t {
        Unknown,
        Png,
        Bmp,
        Gif,
        Psd,
        Pic,
        Jpeg,
        Pnm,
        Hdr,
        Tga
    };

    static inline const char*& LastErrorRef() noexcept {
        static const char* s = "";
        return s;
    }

    static inline void SetError(const char* s) noexcept {
        LastErrorRef() = (s && s[0]) ? s : "";
    }

    static inline void SetErrorOr(const char* s, const char* fallback) noexcept {
        if (s && s[0]) SetError(s);
        else SetError(fallback);
    }

    static inline FormatTag Detect(const uint8_t* bytes, int byte_count) noexcept {
        if (!bytes || byte_count <= 0) return FormatTag::Unknown;

#ifndef STBI_NO_PNG
        if (PngLegacyBackend::IsPng(bytes, byte_count)) return FormatTag::Png;
#endif
#ifndef STBI_NO_JPEG
        if (JpegLegacyBackend::IsJpeg(bytes, byte_count)) return FormatTag::Jpeg;
#endif
#ifndef STBI_NO_BMP
        if (BmpCodec::IsBmp(bytes, byte_count)) return FormatTag::Bmp;
#endif
#ifndef STBI_NO_GIF
        if (GifCodec::IsGif(bytes, byte_count)) return FormatTag::Gif;
#endif
#ifndef STBI_NO_PSD
        if (PsdCodec::IsPsd(bytes, byte_count)) return FormatTag::Psd;
#endif
#ifndef STBI_NO_PIC
        if (PicCodec::IsPic(bytes, byte_count)) return FormatTag::Pic;
#endif
#ifndef STBI_NO_PNM
        if (PnmCodec::IsPnm(bytes, byte_count)) return FormatTag::Pnm;
#endif
#ifndef STBI_NO_HDR
        if (HdrCodec::IsHdr(bytes, byte_count)) return FormatTag::Hdr;
#endif
#ifndef STBI_NO_TGA
        if (TgaCodec::IsTga(bytes, byte_count)) return FormatTag::Tga;
#endif

        return FormatTag::Unknown;
    }

    static inline void WriteInfo(int* x, int* y, int* comp, int w, int h, int c) noexcept {
        if (x) *x = w;
        if (y) *y = h;
        if (comp) *comp = c;
    }

    static inline bool MulSize(size_t a, size_t b, size_t& out) noexcept {
        if (a == 0 || b == 0) {
            out = 0;
            return true;
        }
        if (a > ((size_t)-1) / b) return false;
        out = a * b;
        return true;
    }

    static inline bool PixelCount(int x, int y, int comp, size_t& out) noexcept {
        if (x <= 0 || y <= 0 || comp <= 0) return false;
        size_t t = 0;
        if (!MulSize((size_t)x, (size_t)y, t)) return false;
        return MulSize(t, (size_t)comp, out);
    }

    static inline void* ConvertU8ToU16Owned(void* src_u8, int x, int y, int out_comp) noexcept {
        if (!src_u8) return nullptr;

        size_t count = 0;
        if (!PixelCount(x, y, out_comp, count)) {
            free(src_u8);
            SetError("image size overflow");
            return nullptr;
        }

        uint16_t* out = (uint16_t*)malloc(count * sizeof(uint16_t));
        if (!out) {
            free(src_u8);
            SetError("outofmem");
            return nullptr;
        }

        const uint8_t* src = (const uint8_t*)src_u8;
        for (size_t i = 0; i < count; ++i) {
            out[i] = (uint16_t)((uint16_t(src[i]) << 8) | uint16_t(src[i]));
        }

        free(src_u8);
        return out;
    }

    static inline void* ConvertU8ToF32Owned(void* src_u8, int x, int y, int out_comp) noexcept {
        if (!src_u8) return nullptr;

        size_t count = 0;
        if (!PixelCount(x, y, out_comp, count)) {
            free(src_u8);
            SetError("image size overflow");
            return nullptr;
        }

        float* out = (float*)malloc(count * sizeof(float));
        if (!out) {
            free(src_u8);
            SetError("outofmem");
            return nullptr;
        }

        const uint8_t* src = (const uint8_t*)src_u8;
        const float inv255 = 1.0f / 255.0f;
        for (size_t i = 0; i < count; ++i) out[i] = (float)src[i] * inv255;

        free(src_u8);
        return out;
    }

    static inline void* ConvertU16ToF32Owned(void* src_u16, int x, int y, int out_comp) noexcept {
        if (!src_u16) return nullptr;

        size_t count = 0;
        if (!PixelCount(x, y, out_comp, count)) {
            free(src_u16);
            SetError("image size overflow");
            return nullptr;
        }

        float* out = (float*)malloc(count * sizeof(float));
        if (!out) {
            free(src_u16);
            SetError("outofmem");
            return nullptr;
        }

        const uint16_t* src = (const uint16_t*)src_u16;
        const float inv65535 = 1.0f / 65535.0f;
        for (size_t i = 0; i < count; ++i) out[i] = (float)src[i] * inv65535;

        free(src_u16);
        return out;
    }

    static inline uint16_t ComputeLuma16(uint16_t r, uint16_t g, uint16_t b) noexcept {
        return (uint16_t)(((uint32_t)r * 77u + (uint32_t)g * 150u + (uint32_t)b * 29u) >> 8);
    }

    static inline void* ConvertU16Channels(const uint16_t* src, int x, int y,
                                           int src_comp, int req_comp) noexcept {
        if (!src || src_comp < 1 || src_comp > 4) return nullptr;

        if (req_comp == 0 || req_comp == src_comp) {
            size_t count = 0;
            if (!PixelCount(x, y, src_comp, count)) {
                SetError("image size overflow");
                return nullptr;
            }
            uint16_t* out = (uint16_t*)malloc(count * sizeof(uint16_t));
            if (!out) {
                SetError("outofmem");
                return nullptr;
            }
            memcpy(out, src, count * sizeof(uint16_t));
            return out;
        }

        if (req_comp < 1 || req_comp > 4) {
            SetError("bad req_comp");
            return nullptr;
        }

        size_t src_count = 0;
        size_t out_count = 0;
        if (!PixelCount(x, y, src_comp, src_count) || !PixelCount(x, y, req_comp, out_count)) {
            SetError("image size overflow");
            return nullptr;
        }

        uint16_t* out = (uint16_t*)malloc(out_count * sizeof(uint16_t));
        if (!out) {
            SetError("outofmem");
            return nullptr;
        }

        const size_t px_count = (size_t)x * (size_t)y;
        for (size_t i = 0; i < px_count; ++i) {
            const uint16_t* s = src + i * (size_t)src_comp;
            uint16_t* d = out + i * (size_t)req_comp;

            const uint16_t r = src_comp >= 3 ? s[0] : s[0];
            const uint16_t g = src_comp >= 3 ? s[1] : s[0];
            const uint16_t b = src_comp >= 3 ? s[2] : s[0];
            const uint16_t a = src_comp == 2 ? s[1] : (src_comp == 4 ? s[3] : 0xffffu);

            if (req_comp == 1) {
                d[0] = ComputeLuma16(r, g, b);
            } else if (req_comp == 2) {
                d[0] = ComputeLuma16(r, g, b);
                d[1] = a;
            } else if (req_comp == 3) {
                d[0] = r; d[1] = g; d[2] = b;
            } else {
                d[0] = r; d[1] = g; d[2] = b; d[3] = a;
            }
        }

        (void)src_count;
        return out;
    }

    static inline void* LoadPnmU16FromMemory(const uint8_t* bytes, int byte_count,
                                             int* x, int* y, int* comp, int req_comp) noexcept {
#ifdef STBI_NO_PNM
        (void)bytes; (void)byte_count; (void)x; (void)y; (void)comp; (void)req_comp;
        SetError("PNM decoder disabled");
        return nullptr;
#else
        int w = 0, h = 0, c = 0, maxv = 0;
        size_t data_at = 0;
        if (!PnmCodec::ParseHeader(bytes, byte_count, w, h, c, maxv, data_at)) {
            SetErrorOr(PnmCodec::FailureReason(), "bad PNM header");
            return nullptr;
        }

        if (maxv <= 255) {
            void* u8 = PnmCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
            if (!u8) {
                SetErrorOr(PnmCodec::FailureReason(), "PNM decode failed");
                return nullptr;
            }
            const int out_comp = req_comp ? req_comp : c;
            return ConvertU8ToU16Owned(u8, w, h, out_comp);
        }

        size_t src_count = 0;
        if (!PixelCount(w, h, c, src_count)) {
            SetError("image size overflow");
            return nullptr;
        }
        size_t src_bytes = 0;
        if (!MulSize(src_count, 2u, src_bytes)) {
            SetError("image size overflow");
            return nullptr;
        }

        if (data_at + src_bytes > (size_t)byte_count) {
            SetError("truncated PNM data");
            return nullptr;
        }

        uint16_t* src = (uint16_t*)malloc(src_count * sizeof(uint16_t));
        if (!src) {
            SetError("outofmem");
            return nullptr;
        }

        const uint8_t* p = bytes + data_at;
        for (size_t i = 0; i < src_count; ++i) {
            src[i] = (uint16_t)((uint16_t(p[0]) << 8) | uint16_t(p[1]));
            p += 2;
        }

        if (maxv != 65535) {
            for (size_t i = 0; i < src_count; ++i) {
                src[i] = (uint16_t)(((uint32_t)src[i] * 65535u + (uint32_t)(maxv / 2)) / (uint32_t)maxv);
            }
        }

        void* out = ConvertU16Channels(src, w, h, c, req_comp);
        free(src);
        if (!out) {
            if (!LastErrorRef()[0]) SetError("PNM channel conversion failed");
            return nullptr;
        }

        WriteInfo(x, y, comp, w, h, c);
        return out;
#endif
    }

public:
    static inline bool InfoFromMemory(const uint8_t* bytes, int byte_count,
                                      int* x, int* y, int* comp) noexcept {
        SetError("");
        const FormatTag fmt = Detect(bytes, byte_count);
        switch (fmt) {
#ifndef STBI_NO_PNG
            case FormatTag::Png: {
                if (PngLegacyBackend::Info(bytes, byte_count, x, y, comp)) return true;
                SetErrorOr(PngLegacyBackend::FailureReason(), "PNG info failed");
                return false;
            }
#endif
#ifndef STBI_NO_BMP
            case FormatTag::Bmp: {
                int w = 0, h = 0, c = 0, bpp = 0;
                uint32_t pixel_offset = 0;
                bool flip_y = false;
                if (!BmpCodec::ParseHeader(bytes, byte_count, w, h, c, bpp, pixel_offset, flip_y)) {
                    SetErrorOr(BmpCodec::FailureReason(), "BMP info failed");
                    return false;
                }
                WriteInfo(x, y, comp, w, h, c);
                return true;
            }
#endif
#ifndef STBI_NO_GIF
            case FormatTag::Gif: {
                GifCodec::Header h{};
                if (!GifCodec::ParseHeader(bytes, byte_count, h)) {
                    SetErrorOr(GifCodec::FailureReason(), "GIF info failed");
                    return false;
                }
                WriteInfo(x, y, comp, h.width, h.height, 4);
                return true;
            }
#endif
#ifndef STBI_NO_PSD
            case FormatTag::Psd: {
                PsdCodec::Header h{};
                if (!PsdCodec::ParseHeader(bytes, byte_count, h)) {
                    SetErrorOr(PsdCodec::FailureReason(), "PSD info failed");
                    return false;
                }
                WriteInfo(x, y, comp, h.width, h.height, 4);
                return true;
            }
#endif
#ifndef STBI_NO_PIC
            case FormatTag::Pic: {
                PicCodec::Header h{};
                if (!PicCodec::ParseHeader(bytes, byte_count, h)) {
                    SetErrorOr(PicCodec::FailureReason(), "PIC info failed");
                    return false;
                }
                WriteInfo(x, y, comp, h.width, h.height, h.comp);
                return true;
            }
#endif
#ifndef STBI_NO_JPEG
            case FormatTag::Jpeg: {
                if (JpegLegacyBackend::Info(bytes, byte_count, x, y, comp)) return true;
                SetErrorOr(JpegLegacyBackend::FailureReason(), "JPEG info failed");
                return false;
            }
#endif
#ifndef STBI_NO_PNM
            case FormatTag::Pnm: {
                int w = 0, h = 0, c = 0, maxv = 0;
                size_t data_at = 0;
                if (!PnmCodec::ParseHeader(bytes, byte_count, w, h, c, maxv, data_at)) {
                    SetErrorOr(PnmCodec::FailureReason(), "PNM info failed");
                    return false;
                }
                WriteInfo(x, y, comp, w, h, c);
                return true;
            }
#endif
#ifndef STBI_NO_HDR
            case FormatTag::Hdr: {
                int w = 0, h = 0;
                size_t data_at = 0;
                if (!HdrCodec::ParseHeader(bytes, byte_count, w, h, data_at)) {
                    SetErrorOr(HdrCodec::FailureReason(), "HDR info failed");
                    return false;
                }
                WriteInfo(x, y, comp, w, h, 3);
                return true;
            }
#endif
#ifndef STBI_NO_TGA
            case FormatTag::Tga: {
                int w = 0, h = 0, c = 0;
                uint8_t image_type = 0;
                uint8_t bpp = 0;
                bool top_origin = false;
                size_t data_offset = 0;
                if (!TgaCodec::ParseHeader(bytes, byte_count, w, h, c, image_type, bpp, top_origin, data_offset)) {
                    SetErrorOr(TgaCodec::FailureReason(), "TGA info failed");
                    return false;
                }
                WriteInfo(x, y, comp, w, h, c);
                return true;
            }
#endif
            default:
                SetError("unknown image type");
                return false;
        }
    }

    static inline bool IsHdrFromMemory(const uint8_t* bytes, int byte_count) noexcept {
#ifdef STBI_NO_HDR
        (void)bytes;
        (void)byte_count;
        return false;
#else
        return HdrCodec::IsHdr(bytes, byte_count);
#endif
    }

    static inline bool Is16BitFromMemory(const uint8_t* bytes, int byte_count) noexcept {
        const FormatTag fmt = Detect(bytes, byte_count);
        switch (fmt) {
#ifndef STBI_NO_PNG
            case FormatTag::Png:
                return PngLegacyBackend::Is16Bit(bytes, byte_count);
#endif
#ifndef STBI_NO_PSD
            case FormatTag::Psd:
                return PsdCodec::Is16Bit(bytes, byte_count);
#endif
#ifndef STBI_NO_PNM
            case FormatTag::Pnm: {
                int w = 0, h = 0, c = 0, maxv = 0;
                size_t data_at = 0;
                if (!PnmCodec::ParseHeader(bytes, byte_count, w, h, c, maxv, data_at)) return false;
                return maxv > 255;
            }
#endif
            default:
                return false;
        }
    }

    static inline void* LoadU8FromMemory(const uint8_t* bytes, int byte_count,
                                         int* x, int* y, int* comp, int req_comp) noexcept {
        SetError("");
        const FormatTag fmt = Detect(bytes, byte_count);

        switch (fmt) {
#ifndef STBI_NO_PNG
            case FormatTag::Png: {
                void* p = PngLegacyBackend::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(PngLegacyBackend::FailureReason(), "PNG decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_BMP
            case FormatTag::Bmp: {
                void* p = BmpCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(BmpCodec::FailureReason(), "BMP decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_GIF
            case FormatTag::Gif: {
                void* p = GifCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(GifCodec::FailureReason(), "GIF decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_PSD
            case FormatTag::Psd: {
                void* p = PsdCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(PsdCodec::FailureReason(), "PSD decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_PIC
            case FormatTag::Pic: {
                void* p = PicCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(PicCodec::FailureReason(), "PIC decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_JPEG
            case FormatTag::Jpeg: {
                void* p = JpegLegacyBackend::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(JpegLegacyBackend::FailureReason(), "JPEG decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_PNM
            case FormatTag::Pnm: {
                void* p = PnmCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(PnmCodec::FailureReason(), "PNM decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_HDR
            case FormatTag::Hdr: {
                void* p = HdrCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(HdrCodec::FailureReason(), "HDR decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_TGA
            case FormatTag::Tga: {
                void* p = TgaCodec::LoadU8(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(TgaCodec::FailureReason(), "TGA decode failed");
                return p;
            }
#endif
            default:
                SetError("unknown image type");
                return nullptr;
        }
    }

    static inline void* LoadU16FromMemory(const uint8_t* bytes, int byte_count,
                                          int* x, int* y, int* comp, int req_comp) noexcept {
        SetError("");
        const FormatTag fmt = Detect(bytes, byte_count);

        switch (fmt) {
#ifndef STBI_NO_PNG
            case FormatTag::Png: {
                void* p = PngLegacyBackend::LoadU16(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(PngLegacyBackend::FailureReason(), "PNG decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_JPEG
            case FormatTag::Jpeg: {
                void* p = JpegLegacyBackend::LoadU16(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(JpegLegacyBackend::FailureReason(), "JPEG decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_PNM
            case FormatTag::Pnm: {
                void* p = LoadPnmU16FromMemory(bytes, byte_count, x, y, comp, req_comp);
                if (!p && !LastErrorRef()[0]) SetErrorOr(PnmCodec::FailureReason(), "PNM decode failed");
                return p;
            }
#endif
            case FormatTag::Unknown:
                SetError("unknown image type");
                return nullptr;
            default: {
                int tx = 0, ty = 0, tc = 0;
                void* u8 = LoadU8FromMemory(bytes, byte_count, &tx, &ty, &tc, req_comp);
                if (!u8) return nullptr;
                const int out_comp = req_comp ? req_comp : tc;
                void* out = ConvertU8ToU16Owned(u8, tx, ty, out_comp);
                if (!out) return nullptr;
                WriteInfo(x, y, comp, tx, ty, tc);
                return out;
            }
        }
    }

    static inline void* LoadF32FromMemory(const uint8_t* bytes, int byte_count,
                                          int* x, int* y, int* comp, int req_comp) noexcept {
        SetError("");
        const FormatTag fmt = Detect(bytes, byte_count);

        switch (fmt) {
#ifndef STBI_NO_PNG
            case FormatTag::Png: {
                void* p = PngLegacyBackend::LoadF32(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(PngLegacyBackend::FailureReason(), "PNG decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_JPEG
            case FormatTag::Jpeg: {
                void* p = JpegLegacyBackend::LoadF32(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(JpegLegacyBackend::FailureReason(), "JPEG decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_HDR
            case FormatTag::Hdr: {
                void* p = HdrCodec::LoadF32(bytes, byte_count, x, y, comp, req_comp);
                if (!p) SetErrorOr(HdrCodec::FailureReason(), "HDR decode failed");
                return p;
            }
#endif
#ifndef STBI_NO_PNM
            case FormatTag::Pnm: {
                int w = 0, h = 0, c = 0, maxv = 0;
                size_t data_at = 0;
                if (!PnmCodec::ParseHeader(bytes, byte_count, w, h, c, maxv, data_at)) {
                    SetErrorOr(PnmCodec::FailureReason(), "PNM decode failed");
                    return nullptr;
                }
                if (maxv > 255) {
                    int tx = 0, ty = 0, tc = 0;
                    void* u16 = LoadPnmU16FromMemory(bytes, byte_count, &tx, &ty, &tc, req_comp);
                    if (!u16) return nullptr;
                    const int out_comp = req_comp ? req_comp : tc;
                    void* f32 = ConvertU16ToF32Owned(u16, tx, ty, out_comp);
                    if (!f32) return nullptr;
                    WriteInfo(x, y, comp, tx, ty, tc);
                    return f32;
                }
                int tx = 0, ty = 0, tc = 0;
                void* u8 = PnmCodec::LoadU8(bytes, byte_count, &tx, &ty, &tc, req_comp);
                if (!u8) {
                    SetErrorOr(PnmCodec::FailureReason(), "PNM decode failed");
                    return nullptr;
                }
                const int out_comp = req_comp ? req_comp : tc;
                void* f32 = ConvertU8ToF32Owned(u8, tx, ty, out_comp);
                if (!f32) return nullptr;
                WriteInfo(x, y, comp, tx, ty, tc);
                return f32;
            }
#endif
            case FormatTag::Unknown:
                SetError("unknown image type");
                return nullptr;
            default: {
                int tx = 0, ty = 0, tc = 0;
                void* u8 = LoadU8FromMemory(bytes, byte_count, &tx, &ty, &tc, req_comp);
                if (!u8) return nullptr;
                const int out_comp = req_comp ? req_comp : tc;
                void* out = ConvertU8ToF32Owned(u8, tx, ty, out_comp);
                if (!out) return nullptr;
                WriteInfo(x, y, comp, tx, ty, tc);
                return out;
            }
        }
    }

    static inline void ImageFree(void* p) noexcept {
        free(p);
    }

    static inline const char* FailureReason() noexcept {
        return LastErrorRef();
    }
};

} // namespace detail
} // namespace stbi
