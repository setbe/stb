#pragma once

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "detail/backend.hpp"

namespace stbi {

enum class Format : uint8_t {
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

enum class SampleType : uint8_t {
    U8,
    U16,
    F32
};

struct DecodeOptions {
    uint8_t desired_channels{};
    SampleType sample_type{ SampleType::U8 };
    bool flip_vertically{};
};

struct ImagePlan {
    Format format{ Format::Unknown };
    SampleType sample_type{ SampleType::U8 };
    bool flip_vertically{};
    uint32_t width{};
    uint32_t height{};
    uint8_t channels_in_file{};
    uint8_t output_channels{};
    uint8_t source_bits_per_channel{};
    size_t pixel_bytes{};
    size_t scratch_bytes{};
};

struct BatchPlanSummary {
    uint32_t image_count{};
    uint32_t max_width{};
    uint32_t max_height{};
    uint8_t max_channels_in_file{};
    uint8_t max_output_channels{};
    uint8_t max_source_bits_per_channel{};
    size_t max_pixel_bytes{};
    size_t max_scratch_bytes{};
    size_t max_total_bytes{};
    size_t sum_pixel_bytes{};
    size_t sum_total_bytes{};
};

struct BatchPlanner {
    BatchPlanSummary summary{};

    inline void Reset() noexcept { summary = BatchPlanSummary{}; }

    inline bool Add(const ImagePlan& plan) noexcept {
        if (plan.format == Format::Unknown) return false;

        if (plan.width > summary.max_width) summary.max_width = plan.width;
        if (plan.height > summary.max_height) summary.max_height = plan.height;
        if (plan.channels_in_file > summary.max_channels_in_file) summary.max_channels_in_file = plan.channels_in_file;
        if (plan.output_channels > summary.max_output_channels) summary.max_output_channels = plan.output_channels;
        if (plan.source_bits_per_channel > summary.max_source_bits_per_channel) {
            summary.max_source_bits_per_channel = plan.source_bits_per_channel;
        }
        if (plan.pixel_bytes > summary.max_pixel_bytes) summary.max_pixel_bytes = plan.pixel_bytes;
        if (plan.scratch_bytes > summary.max_scratch_bytes) summary.max_scratch_bytes = plan.scratch_bytes;

        const size_t total = plan.pixel_bytes + plan.scratch_bytes;
        if (total > summary.max_total_bytes) summary.max_total_bytes = total;

        if (summary.sum_pixel_bytes > (size_t)-1 - plan.pixel_bytes) return false;
        if (summary.sum_total_bytes > (size_t)-1 - total) return false;
        if (summary.image_count == 0xffffffffu) return false;

        summary.sum_pixel_bytes += plan.pixel_bytes;
        summary.sum_total_bytes += total;
        ++summary.image_count;
        return true;
    }

    inline const BatchPlanSummary& Get() const noexcept { return summary; }
    inline size_t ReusableScratchBytes() const noexcept { return summary.max_scratch_bytes; }
    inline size_t TotalPixelBytes() const noexcept { return summary.sum_pixel_bytes; }
    inline size_t MaxImageBytes() const noexcept { return summary.max_total_bytes; }
};

namespace detail {

static inline bool add_size(size_t a, size_t b, size_t& out) noexcept {
    if (a > (size_t)-1 - b) return false;
    out = a + b;
    return true;
}

static inline bool mul_size(size_t a, size_t b, size_t& out) noexcept {
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > (size_t)-1 / b) return false;
    out = a * b;
    return true;
}

static inline bool pixel_bytes(uint32_t width, uint32_t height, uint8_t channels, SampleType type, size_t& out) noexcept {
    size_t t = 0;
    if (!mul_size((size_t)width, (size_t)height, t)) return false;
    if (!mul_size(t, (size_t)channels, t)) return false;
    const size_t bytes_per_channel = type == SampleType::U8 ? 1u : (type == SampleType::U16 ? 2u : 4u);
    return mul_size(t, bytes_per_channel, out);
}

static inline bool row_bytes(const ImagePlan& plan, size_t& out) noexcept {
    size_t t = 0;
    if (!mul_size((size_t)plan.width, (size_t)plan.output_channels, t)) return false;
    const size_t bytes_per_channel = plan.sample_type == SampleType::U8 ? 1u : (plan.sample_type == SampleType::U16 ? 2u : 4u);
    return mul_size(t, bytes_per_channel, out);
}

static inline void flip_rows(void* pixels, size_t stride_bytes, uint32_t height) noexcept {
    uint8_t tmp[4096];
    uint8_t* p = (uint8_t*)pixels;
    for (uint32_t y = 0; y < (height >> 1); ++y) {
        uint8_t* top = p + (size_t)y * stride_bytes;
        uint8_t* bot = p + (size_t)(height - 1u - y) * stride_bytes;
        size_t left = stride_bytes;
        while (left) {
            const size_t chunk = left < sizeof(tmp) ? left : sizeof(tmp);
            memcpy(tmp, top, chunk);
            memcpy(top, bot, chunk);
            memcpy(bot, tmp, chunk);
            top += chunk;
            bot += chunk;
            left -= chunk;
        }
    }
}

static inline bool to_int_len(size_t byte_count, int& out_len) noexcept {
    if (byte_count > (size_t)INT_MAX) return false;
    out_len = (int)byte_count;
    return true;
}

static inline bool has_prefix(const uint8_t* b, size_t n, const char* s, size_t m) noexcept {
    if (!b || n < m) return false;
    for (size_t i = 0; i < m; ++i) {
        if (b[i] != (uint8_t)s[i]) return false;
    }
    return true;
}

static inline Format detect_format(const uint8_t* b, size_t n) noexcept {
    if (!b || n < 2) return Format::Unknown;

    if (n >= 8 &&
        b[0] == 137 && b[1] == 80 && b[2] == 78 && b[3] == 71 &&
        b[4] == 13 && b[5] == 10 && b[6] == 26 && b[7] == 10) {
        return Format::Png;
    }

    if (n >= 3 && b[0] == 0xff && b[1] == 0xd8 && b[2] == 0xff) return Format::Jpeg;
    if (n >= 2 && b[0] == 'B' && b[1] == 'M') return Format::Bmp;
    if (has_prefix(b, n, "GIF87a", 6) || has_prefix(b, n, "GIF89a", 6)) return Format::Gif;
    if (has_prefix(b, n, "8BPS", 4)) return Format::Psd;
    if (has_prefix(b, n, "#?RADIANCE", 10) || has_prefix(b, n, "#?RGBE", 6)) return Format::Hdr;
    if (n >= 8 && b[0] == 0x53 && b[1] == 0x80 && b[2] == 0xF6 && b[3] == 0x34 &&
        b[4] == 'P' && b[5] == 'I' && b[6] == 'C' && b[7] == 'T') {
        return Format::Pic;
    }

    if (n >= 2 && b[0] == 'P') {
        const uint8_t c = b[1];
        if (c == '5' || c == '6' || c == 'f' || c == 'F') return Format::Pnm;
    }

    if (n >= 18) {
        const uint8_t color_map_type = b[1];
        const uint8_t image_type = b[2];
        const bool type_ok = image_type == 1 || image_type == 2 || image_type == 3 ||
                             image_type == 9 || image_type == 10 || image_type == 11;
        if ((color_map_type == 0 || color_map_type == 1) && type_ok) return Format::Tga;
    }

    return Format::Unknown;
}

static inline bool plan_impl(Format required,
                             const uint8_t* bytes,
                             size_t byte_count,
                             const DecodeOptions& options,
                             ImagePlan& out_plan) noexcept {
    if (!bytes || byte_count == 0) return false;
    if (options.desired_channels > 4) return false;

    int len = 0;
    if (!to_int_len(byte_count, len)) return false;

    int x = 0, y = 0, comp = 0;
    if (!core::ImageBackend::InfoFromMemory(bytes, len, &x, &y, &comp)) return false;
    if (x <= 0 || y <= 0 || comp <= 0 || comp > 4) return false;

    const Format fmt = detect_format(bytes, byte_count);
    if (required != Format::Unknown && fmt != required) return false;

    const uint8_t out_comp = options.desired_channels ? options.desired_channels : (uint8_t)comp;
    if (out_comp == 0 || out_comp > 4) return false;

    size_t pix_bytes = 0;
    if (!pixel_bytes((uint32_t)x, (uint32_t)y, out_comp, options.sample_type, pix_bytes)) return false;

    uint8_t src_bits = 8;
    if (core::ImageBackend::IsHdrFromMemory(bytes, len)) {
        src_bits = 32;
    } else if (core::ImageBackend::Is16BitFromMemory(bytes, len)) {
        src_bits = 16;
    }

    out_plan = ImagePlan{};
    out_plan.format = fmt;
    out_plan.sample_type = options.sample_type;
    out_plan.flip_vertically = options.flip_vertically;
    out_plan.width = (uint32_t)x;
    out_plan.height = (uint32_t)y;
    out_plan.channels_in_file = (uint8_t)comp;
    out_plan.output_channels = out_comp;
    out_plan.source_bits_per_channel = src_bits;
    out_plan.pixel_bytes = pix_bytes;
    out_plan.scratch_bytes = 0;
    return true;
}

static inline bool decode_impl(Format required,
                               const uint8_t* bytes,
                               size_t byte_count,
                               const ImagePlan& plan,
                               void* scratch_mem,
                               size_t scratch_bytes,
                               void* out_pixels,
                               size_t out_bytes) noexcept {
    (void)scratch_mem;
    (void)scratch_bytes;

    if (!bytes || byte_count == 0) return false;
    if (!out_pixels || out_bytes < plan.pixel_bytes) return false;
    if (plan.format == Format::Unknown) return false;
    if (required != Format::Unknown && plan.format != required) return false;
    if (plan.output_channels == 0 || plan.output_channels > 4) return false;

    int len = 0;
    if (!to_int_len(byte_count, len)) return false;

    int x = 0, y = 0, comp = 0;
    void* decoded = nullptr;
    if (plan.sample_type == SampleType::U8) {
        decoded = core::ImageBackend::LoadU8FromMemory(bytes, len, &x, &y, &comp, (int)plan.output_channels);
    } else if (plan.sample_type == SampleType::U16) {
        decoded = core::ImageBackend::LoadU16FromMemory(bytes, len, &x, &y, &comp, (int)plan.output_channels);
    } else {
        decoded = core::ImageBackend::LoadF32FromMemory(bytes, len, &x, &y, &comp, (int)plan.output_channels);
    }

    if (!decoded) return false;

    const bool ok_meta = (x > 0 && y > 0 &&
                          (uint32_t)x == plan.width &&
                          (uint32_t)y == plan.height &&
                          (uint8_t)comp == plan.channels_in_file);
    if (!ok_meta) {
        core::ImageBackend::ImageFree(decoded);
        return false;
    }

    memcpy(out_pixels, decoded, plan.pixel_bytes);
    core::ImageBackend::ImageFree(decoded);

    if (plan.flip_vertically && plan.height > 1u) {
        size_t stride = 0;
        if (!row_bytes(plan, stride)) return false;
        flip_rows(out_pixels, stride, plan.height);
    }
    return true;
}

} // namespace detail

static inline size_t sample_bytes(SampleType type) noexcept {
    return type == SampleType::U8 ? 1u : (type == SampleType::U16 ? 2u : 4u);
}

static inline size_t total_bytes(const ImagePlan& plan) noexcept {
    return plan.pixel_bytes + plan.scratch_bytes;
}

inline const char* failure_reason() noexcept {
    return detail::core::ImageBackend::FailureReason();
}

inline bool Plan(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Unknown, bytes, byte_count, options, out_plan);
}
inline bool PlanPng(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Png, bytes, byte_count, options, out_plan);
}
inline bool PlanBmp(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Bmp, bytes, byte_count, options, out_plan);
}
inline bool PlanGif(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Gif, bytes, byte_count, options, out_plan);
}
inline bool PlanPsd(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Psd, bytes, byte_count, options, out_plan);
}
inline bool PlanPic(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Pic, bytes, byte_count, options, out_plan);
}
inline bool PlanJpeg(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Jpeg, bytes, byte_count, options, out_plan);
}
inline bool PlanPnm(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Pnm, bytes, byte_count, options, out_plan);
}
inline bool PlanHdr(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Hdr, bytes, byte_count, options, out_plan);
}
inline bool PlanTga(const uint8_t* bytes, size_t byte_count, const DecodeOptions& options, ImagePlan& out_plan) noexcept {
    return detail::plan_impl(Format::Tga, bytes, byte_count, options, out_plan);
}

inline bool Decode(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                   void* scratch_mem, size_t scratch_bytes,
                   void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Unknown, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodePng(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Png, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodeBmp(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Bmp, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodeGif(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Gif, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodePsd(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Psd, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodePic(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Pic, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodeJpeg(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                       void* scratch_mem, size_t scratch_bytes,
                       void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Jpeg, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodePnm(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Pnm, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodeHdr(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Hdr, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}
inline bool DecodeTga(const uint8_t* bytes, size_t byte_count, const ImagePlan& plan,
                      void* scratch_mem, size_t scratch_bytes,
                      void* out_pixels, size_t out_bytes) noexcept {
    return detail::decode_impl(Format::Tga, bytes, byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
}

struct Decoder {
    explicit Decoder() noexcept = default;
    ~Decoder() noexcept = default;

    inline bool ReadBytes(const uint8_t* bytes, size_t byte_count) noexcept {
        _bytes = bytes;
        _byte_count = byte_count;
        return _bytes != nullptr && _byte_count != 0;
    }

    inline void Clear() noexcept {
        _bytes = nullptr;
        _byte_count = 0;
    }

    inline bool Plan(const DecodeOptions& options, ImagePlan& out_plan) const noexcept {
        return stbi::Plan(_bytes, _byte_count, options, out_plan);
    }
    inline bool Decode(const ImagePlan& plan,
                       void* scratch_mem, size_t scratch_bytes,
                       void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::Decode(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }

    inline bool PlanPng(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanPng(_bytes, _byte_count, options, out_plan); }
    inline bool PlanBmp(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanBmp(_bytes, _byte_count, options, out_plan); }
    inline bool PlanGif(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanGif(_bytes, _byte_count, options, out_plan); }
    inline bool PlanPsd(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanPsd(_bytes, _byte_count, options, out_plan); }
    inline bool PlanPic(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanPic(_bytes, _byte_count, options, out_plan); }
    inline bool PlanJpeg(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanJpeg(_bytes, _byte_count, options, out_plan); }
    inline bool PlanPnm(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanPnm(_bytes, _byte_count, options, out_plan); }
    inline bool PlanHdr(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanHdr(_bytes, _byte_count, options, out_plan); }
    inline bool PlanTga(const DecodeOptions& options, ImagePlan& out_plan) const noexcept { return stbi::PlanTga(_bytes, _byte_count, options, out_plan); }

    inline bool DecodePng(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodePng(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodeBmp(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodeBmp(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodeGif(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodeGif(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodePsd(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodePsd(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodePic(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodePic(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodeJpeg(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodeJpeg(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodePnm(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodePnm(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodeHdr(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodeHdr(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }
    inline bool DecodeTga(const ImagePlan& plan, void* scratch_mem, size_t scratch_bytes, void* out_pixels, size_t out_bytes) const noexcept {
        return stbi::DecodeTga(_bytes, _byte_count, plan, scratch_mem, scratch_bytes, out_pixels, out_bytes);
    }

    inline const char* FailureReason() const noexcept { return stbi::failure_reason(); }
    inline const uint8_t* Bytes() const noexcept { return _bytes; }
    inline size_t ByteCount() const noexcept { return _byte_count; }

private:
    const uint8_t* _bytes{};
    size_t _byte_count{};
};

} // namespace stbi
