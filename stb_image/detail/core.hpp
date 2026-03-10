#pragma once

namespace stbi { namespace detail {

// OOP glue layer for per-format plan/decode entry points.
// Raw decoder implementations live in sibling format headers and are wired
// via plan_impl/decode_impl (selected by Format enum).

#ifndef STBI_NO_PNG
struct PngCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Png, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Png, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_BMP
struct BmpCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Bmp, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Bmp, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_GIF
struct GifCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Gif, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Gif, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_PSD
struct PsdCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Psd, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Psd, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_PIC
struct PicCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Pic, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Pic, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_JPEG
struct JpegCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Jpeg, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Jpeg, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_PNM
struct PnmCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Pnm, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Pnm, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_HDR
struct HdrCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Hdr, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Hdr, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

#ifndef STBI_NO_TGA
struct TgaCodec {
    static inline bool Plan(const uint8_t* bytes, size_t byte_count,
                            const DecodeOptions& options, ImagePlan& out_plan) noexcept {
        return plan_impl(Format::Tga, bytes, byte_count, options, out_plan);
    }

    static inline bool Decode(const uint8_t* bytes, size_t byte_count,
                              const ImagePlan& plan,
                              void* scratch_mem, size_t scratch_bytes,
                              void* out_pixels, size_t out_bytes) noexcept {
        return decode_impl(Format::Tga, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
};
#endif

} // namespace detail
} // namespace stbi
