/*
MIT License
Copyright (c) 2017 Sean Barrett
Copyright (c) 2025 setbe

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


ABOUT:

   This and other header files in `detail` directory are a library
   for writing images to a callback.

   The PNG output is not optimal; it is 20-50% larger than the file
   written by a decent optimizing implementation; though providing a custom
   zlib compress function (see STBIW_zlib_compress) can mitigate that.
   This library is designed for source code compactness and simplicity,
   not optimal image file size or run-time performance.

BUILDING:

   You can #define STBIW_assert(x) before the #include to avoid using assert.h.
   You can #define STBIW_malloc(), STBIW_realloc(), and STBIW_free() to replace
   malloc,realloc,free.
   You can #define STBIW_memmove() to replace memmove()
   You can #define STBIW_zlib_compress to use a custom zlib-style compress function
   for PNG compression (instead of the builtin one), it must have the following signature:
   unsigned char * my_compress(unsigned char *data, int data_len, int *out_len, int quality);
   The returned data will be freed with STBIW_free() (free() by default),
   so it must be heap allocated with STBIW_malloc() (malloc() by default),

   You can define STBIW_FREESTANDING to not use stdio.h at all.

   Each function returns 0 on failure and non-0 on success.

   The functions create an image file defined by the parameters. The image
   is a rectangle of pixels stored from left-to-right, top-to-bottom.
   Each pixel contains 'comp' channels of data stored interleaved with 8-bits
   per channel, in the following order: 1=Y, 2=YA, 3=RGB, 4=RGBA. (Y is
   monochrome color.) The rectangle is 'w' pixels wide and 'h' pixels tall.
   The *data pointer points to the first byte of the top-left-most pixel.
   For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
   a row of pixels to the first byte of the next row of pixels.

   PNG creates output files with the same number of components as the input.
   The BMP format expands Y to RGB in the file format and does not
   output alpha.

   PNG supports writing rectangles of data even when the bytes storing rows of
   data are not consecutive in memory (e.g. sub-rectangles of a larger image),
   by supplying the stride between the beginning of adjacent rows. The other
   formats do not. (Thus you cannot write a native-format BMP through the BMP
   writer, both because it is in BGR order and because it may have padding
   at the end of the line.)

   PNG allows you to set the deflate compression level by setting the global
   variable 'stbi_write_png_compression_level' (it defaults to 8).

   HDR expects linear float data. Since the format is always 32-bit rgb(e)
   data, alpha (if provided) is discarded, and for monochrome data it is
   replicated across all three channels.

   TGA supports RLE or non-RLE compressed data. To use non-RLE-compressed
   data, set the global variable 'stbi_write_tga_with_rle' to 0.

   JPEG does ignore alpha channels in input data; quality is between 1 and 100.
   Higher quality looks better but results in a bigger image.
   JPEG baseline (no JPEG progressive).
*/

#pragma once
// ------------------- Freestanding-friendly Includes -------------------------
#include <cstddef>
#include <cstdint>

#include "detail/libc_integration.hpp" // Checkout this file for freestanding intergration
#include "detail/zlib.hpp"

namespace stbiw {
    // -------------------------------- Tokens --------------------------------

    struct b1_t { std::uint8_t  v; };
    struct le16_t { std::uint16_t v; };
    struct le32_t { std::uint32_t v; }; // bitwise payload
    struct be32_t { std::uint32_t v; };
    struct raw_t { const void* p; int n; };

    constexpr inline b1_t b1(std::uint32_t x) noexcept {
        return { static_cast<std::uint8_t>(x & 0xFFU) };
    }
    constexpr inline le16_t le16(std::uint32_t x) noexcept {
        return { static_cast<std::uint16_t>(x & 0xFFFFU) };
    }
    constexpr inline le32_t le32(std::uint32_t x) noexcept { return { x }; }
    constexpr inline be32_t be32(std::uint32_t x) noexcept { return { x }; }
    constexpr inline le32_t le32i(std::int32_t x) noexcept {
        return { static_cast<std::uint32_t>(x) }; // keeps signed bit-pattern
    }
    constexpr inline raw_t raw(const void* p, int n) noexcept {
        return { p, n };
    }

    static inline void be32_store(std::uint8_t out[4], std::uint32_t v) noexcept {
        out[0] = static_cast<std::uint8_t>(v >> 24);
        out[1] = static_cast<std::uint8_t>(v >> 16);
        out[2] = static_cast<std::uint8_t>(v >> 8);
        out[3] = static_cast<std::uint8_t>(v >> 0);
    }

    // --- buffering sizes ---
    constexpr inline int token_size(b1_t)   noexcept { return 1; }
    constexpr inline int token_size(le16_t) noexcept { return 2; }
    constexpr inline int token_size(le32_t) noexcept { return 4; }
    constexpr inline int token_size(be32_t) noexcept { return 4; }
    constexpr inline int token_size(raw_t)  noexcept { return -1; }






    enum class PngFilter : std::uint8_t {
        None = 0, Sub = 1, Up = 2, Avg = 3, Paeth = 4
    };

    inline void png_apply_filter(
        PngFilter f,
        const std::uint8_t* cur,
        const std::uint8_t* prev,
        int row_bytes,
        int comp,
        std::uint8_t* dst
    ) noexcept;

    struct PngScanlineSink {
        virtual void emit(std::uint8_t filter, const std::uint8_t* data, int n) noexcept = 0;
    };


    inline int png_choose_best_filter(
        const std::uint8_t* cur,
        const std::uint8_t* prev,
        int row_bytes,
        int comp,
        std::uint8_t* tmp,
        std::uint8_t* best
    ) noexcept;


    struct Writer {
        using Func = void (*)(void* ctx, const void* data, int size);

        Writer() = default;

    private:
        Func _func{ nullptr };
        void* _ctx{ nullptr };
        unsigned char _buf[64]{ 0 };
        int _used = 0;

        int _png_compression_level{ 8 };
        int _force_png_filter{ -1 };
        bool _tga_with_rle{ true };
        bool _flip_vertically_on_write{ false };

    public:

        // defaults to true; to disable RLE set to false
        inline void set_tga_rle(bool v) noexcept { _tga_with_rle = v; }
        inline bool has_tga_rle() const noexcept { return _tga_with_rle; }

        // defaults to false; set to true to flip image on write
        inline void set_flip_vertically(bool v) noexcept { _flip_vertically_on_write = v; }
        inline bool is_flipped_vertically() const noexcept { return _flip_vertically_on_write; }

        // defaults to -1; set to [0,5] to force a filter mode
        inline void set_force_png_filter(int v) noexcept { _force_png_filter = v; }
        inline int get_forced_png_filter() const noexcept { return _force_png_filter; }

        // defaults to 8; set to higher for more compression
        inline void set_png_compression_level(int v) noexcept { _png_compression_level = v; }
        inline int get_png_compression_level() const noexcept { return _png_compression_level; }

        inline bool is_exceeds_buffer_size(int size) const noexcept {
            return size > static_cast<int>(sizeof(_buf));
        }

        inline void start_callbacks(Func c, void* ctx) noexcept {
            _func = c;
            _ctx = ctx;
            _used = 0;
        }

        inline void flush() noexcept {
            if (_used && _func) {
                _func(_ctx, _buf, _used);
                _used = 0;
            }
        }

        inline void write_bytes_direct(const void* data, int n) noexcept {
            if (!_func || !data || n <= 0) return;
            if (_used) flush();
            _func(_ctx, data, n);
        }

        inline void write_byte(std::uint8_t byte) noexcept {
            if (is_exceeds_buffer_size(_used + 1)) flush();
            _buf[_used++] = byte;
        }

        inline void write3(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept {
            if (is_exceeds_buffer_size(_used + 3)) flush();
            int n = _used;
            _used = n + 3;
            _buf[n + 0] = a;
            _buf[n + 1] = b;
            _buf[n + 2] = c;
        }

        inline void putc(std::uint8_t c) noexcept { // for JPEG
            flush();
            write_bytes_direct(&c, 1);
        }

    private:

        // we avoid using va_args in favor of type-safe emitter
        inline void emit(b1_t t) noexcept { write_byte(t.v); }
        inline void emit(le16_t t) noexcept {
            if (is_exceeds_buffer_size(_used + 2)) flush();
            _buf[_used++] = static_cast<std::uint8_t>(t.v & 0xFFU);
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 8) & 0xFFU);
        }
        inline void emit(le32_t t) noexcept {
            if (is_exceeds_buffer_size(_used + 4)) flush();
            _buf[_used++] = static_cast<std::uint8_t>(t.v & 0xFFU);
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 8) & 0xFFU);
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 16) & 0xFFU);
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 24) & 0xFFU);
        }
        inline void emit(be32_t t) noexcept {
            if (is_exceeds_buffer_size(_used + 4)) flush();
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 24) & 0xFFu);
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 16) & 0xFFu);
            _buf[_used++] = static_cast<std::uint8_t>((t.v >> 8) & 0xFFu);
            _buf[_used++] = static_cast<std::uint8_t>(t.v & 0xFFu);
        }
        inline void emit(raw_t r) noexcept {
            flush();
            write_bytes_direct(r.p, r.n);
        }

        template <typename... Ts>
        inline void write_tokens(Ts... ts) noexcept {
            int dummy[] = { 0, (emit(ts), 0)... }; // C++11 pack expansion
            (void)dummy;
        }

        // At the end of headers/strings, it is usually useful to flush
        inline void write_flush() noexcept { flush(); }


    public:
        inline void write_pixel(int rgb_dir, int comp,
                                int write_alpha,
                                int expand_mono,
                                const std::uint8_t* d) noexcept;

        inline void write_pixels(int rgb_dir, int vdir,
                                 int x, int y, int comp,
                                 const void* data,
                                 int write_alpha, 
                                 int scanline_pad,
                                 int expand_mono) noexcept;

        template <typename... HeaderTokens>
        inline bool outfile(int rgb_dir, int vdir,
                            int x, int y, int comp,
                            int expand_mono,
                            const void* data,
                            int alpha,
                            int pad,
                            HeaderTokens... header) noexcept {
            if (y < 0 || x < 0) return false;
            write_tokens(header...);
            write_pixels(rgb_dir, vdir, x, y, comp, data, alpha, pad, expand_mono);
            return true;
        }

        inline bool write_bmp(int x, int y, int comp, const void* data) noexcept;
        inline bool write_tga(int x, int y, int comp, const void* data) noexcept;
        inline bool write_png(int x, int y, int comp, const void* data, int stride_in_bytes) noexcept;

        // ---- MAIN IDEA: stream rows -> filter -> zlib stored -> idat chunker ----
        bool write_png_stream_uncompressed(int x, int y, int comp, const void* data,
            int stride_bytes, void* scratch, std::size_t scratch_bytes, int idat_buf_bytes = 8192) noexcept;
    private:


    private: // --- private static helper-members ---
        static inline bool pixel_equal(const std::uint8_t* a, const std::uint8_t* b, int comp) noexcept;
    }; // struct Writer






    void Writer::write_pixel(int rgb_dir, int comp, int write_alpha,
        int expand_mono, const std::uint8_t* d) noexcept {
        static constexpr std::uint8_t bg[3]{ 255, 0, 255 };
        std::uint8_t px[3];

        if (write_alpha < 0)
            write_byte(d[comp - 1]);

        switch (comp)
        {
        case 2: // mono + alpha
        case 1:
            expand_mono ? write3(d[0], d[0], d[0])
                        : write_byte(d[0]);
            break;
        case 4:
            if (!write_alpha) {
                // composite against pink background
                for (int k = 0; k < 3; ++k) {
                    const int j = static_cast<int>(d[k]) - static_cast<int>(bg[k]);
                    const int d3 = static_cast<int>(d[3]);
                    px[k] = static_cast<std::uint8_t>(bg[k] + j * d3 / 255);
                }
                write3(px[1 - rgb_dir], px[1], px[1 + rgb_dir]);
                break;
            }
            // fallthrough
        case 3:
            write3(d[1 - rgb_dir], d[1], d[1 + rgb_dir]);
            break;
        } // switch

        if (write_alpha > 0)
            write_byte(d[comp - 1]);
    } // write_pixel

    void Writer::write_pixels(int rgb_dir, int vdir, int x, int y,
        int comp, const void* data, int write_alpha,
        int scanline_pad, int expand_mono) noexcept {
        if (y <= 0) return;
        if (_flip_vertically_on_write) vdir *= -1;

        int j, j_end;
        j = j_end = 0;

        if (vdir < 0) { j = y - 1;  j_end = -1; }
        else { j = 0;    j_end = y; }

        static constexpr std::uint8_t zeros4[4]{ 0,0,0,0 };

        auto* base = static_cast<const std::uint8_t*>(data);

        for (; j != j_end; j += vdir) {
            for (int i = 0; i < x; ++i) {
                const std::size_t off = static_cast<std::size_t>(j)
                    * static_cast<std::size_t>(x)
                    + static_cast<std::size_t>(i);
                const std::uint8_t* d = base + off * static_cast<std::size_t>(comp);
                write_pixel(rgb_dir, comp, write_alpha, expand_mono, d);
            }
            flush();
            if (scanline_pad)
                write_bytes_direct(zeros4, scanline_pad); // pad 0..3 for BMP
        }
    }


    bool Writer::write_bmp(int x, int y, int comp, const void* data) noexcept {
        if (!_func) return false;
        if (comp != 4) {
            // RGB bitmap
            int pad = (-x * 3) & 3;

            // file size:
            std::uint32_t file_size = static_cast<std::uint32_t>(
                14 + 40 + (x * 3 + pad) * y);
            std::uint32_t pixel_off = static_cast<std::uint32_t>(14 + 40);

            return outfile(-1, -1, x, y, comp, /*expand_mono*/ 1, data,
                /*alpha*/ 0, pad,

                // BITMAPFILEHEADER (14 bytes)
                b1('B'), b1('M'),
                le32(file_size),
                le16(0), le16(0),
                le32(pixel_off),

                // BITMAPINFOHEADER (40 bytes)
                le32(40),
                le32i(static_cast<std::int32_t>(x)),
                le32i(static_cast<std::int32_t>(y)),
                le16(1),
                le16(24),
                le32(0),
                le32(0),
                le32(0), le32(0),
                le32(0), le32(0)
            );
        }
        else {
            // RGBA -> V4 header (108 bytes) + BI_BITFIELDS + alpha mask
            std::uint32_t file_size = static_cast<std::uint32_t>(
                14 + 108 + static_cast<std::uint32_t>(x * y * 4));
            std::uint32_t pixel_off = static_cast<std::uint32_t>(14 + 108);

            return outfile(-1, -1, x, y, comp, /*expand_mono*/ 1, data,
                /*alpha*/ 1, /*pad*/ 0,

                // BITMAPFILEHEADER
                b1('B'), b1('M'),
                le32(file_size),
                le16(0), le16(0),
                le32(pixel_off),

                // BITMAPV4HEADER (108 bytes)
                le32(108),
                le32i(static_cast<std::int32_t>(x)),
                le32i(static_cast<std::int32_t>(y)),
                le16(1),
                le16(32),
                le32(3),            // BI_BITFIELDS
                le32(0), le32(0), le32(0), le32(0), le32(0), // sizeImage + resolution/...
                le32(0xFF0000u),    // red mask
                le32(0x00FF00u),    // green mask
                le32(0x0000FFu),    // blue mask
                le32(0xFF000000u),  // alpha mask
                le32(0),            // CSType

                // CIEXYZTRIPLE endpoints (9*4 bytes) - zeros
                le32(0), le32(0), le32(0),
                le32(0), le32(0), le32(0),
                le32(0), le32(0), le32(0),
                // gamma red/green/blue
                le32(0), le32(0), le32(0)
            );
        }
    } // write_bmp_core

    bool Writer::write_tga(int x, int y, int comp, const void* data) noexcept {
        if (!data || !_func) return false;
        if (x <= 0 || y <= 0) return false;
        if (comp < 1 || comp > 4) return false;

        const bool has_alpha = (comp == 2 || comp == 4);
        const int  colorbytes = has_alpha ? (comp - 1) : comp;
        const int  format = (colorbytes < 2) ? 3 : 2; // 3=grayscale, 2=truecolor

        // --- TGA header (18 bytes) ---
        // idlength, colormaptype, imagetype,
        // cmap_first (le16), cmap_len   (le16), cmap_depth,
        // x_origin   (le16), y_origin   (le16),
        // width      (le16), height     (le16),
        // pixel_depth, image_descriptor
        auto write_tga_header = [&](int image_type) noexcept {
            const std::uint8_t pixel_depth =
                static_cast<std::uint8_t>((colorbytes + (has_alpha ? 1 : 0)) * 8);

            const std::uint8_t descriptor =
                static_cast<std::uint8_t>((has_alpha ? 8 : 0)); // alpha bits count (0 or 8)

            write_tokens(
                b1(0),                   // id length
                b1(0),                   // color map type
                b1(static_cast<std::uint8_t>(image_type)),// image type (2/3 or 10/11)
                le16(0), le16(0), b1(0), // color map spec
                le16(0), le16(0),        // x_origin, y_origin
                le16(static_cast<std::uint16_t>(x)),
                le16(static_cast<std::uint16_t>(y)),
                b1(pixel_depth),
                b1(descriptor)
            );
        };

        // -----------------------------------------------
        //                   No RLE
        // -----------------------------------------------
        if (!_tga_with_rle) {
            // format: 2 (RGB) or 3 (Y)
            // rgb_dir=-1 => BGR
            // vdir=-1 => bottom-top (TGA origin bottom-left when descriptor bit 5 = 0)
            return outfile(
                /*rgb_dir*/ -1, /*vdir*/ -1,
                x, y, comp,
                /*expand_mono*/ 0,
                data,
                /*alpha*/ (has_alpha ? 1 : 0),
                /*pad*/ 0,
                // header tokens:
                b1(0), b1(0), b1(static_cast<std::uint8_t>(format)),
                le16(0), le16(0), b1(0),
                le16(0), le16(0),
                le16(static_cast<std::uint16_t>(x)), le16(static_cast<std::uint16_t>(y)),
                b1(static_cast<std::uint8_t>((colorbytes + (has_alpha ? 1 : 0)) * 8)),
                b1(static_cast<std::uint8_t>(has_alpha ? 8 : 0))
            );
        }

        // -----------------------------------------------
        //      RLE: write header, then RLE strings
        // -----------------------------------------------
        write_tga_header(format + 8); // 10/11

        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
        size_t j, jend, jdir;
        j = jend = jdir = 0;

        if (_flip_vertically_on_write) {
            j = 0;      jend = y;   jdir = 1;
        }
        else {
            j = y - 1;  jend = -1;  jdir = -1;
        }

        for (; j != jend; j += jdir) {
            const auto row = bytes + (j * x*comp);
            
            size_t i = 0;
            while (i < x) {
                const auto begin = row + i*comp;
                int len = 1;
                bool diff = true; // true => RAW packet, false => RLE run packet

                if (i < x-1) {
                    // try determine the packet type with first comparison
                    diff = !pixel_equal(begin, row + (i+1)*comp, comp);
                    len = 2;

                    if (diff) {
                        // RAW packet: Increase while pixels are DIFFERENT from the previous ones (to avoid run)
                        const std::uint8_t* prev = begin;
                        for (size_t k = i+2; k<x && len<128; ++k) {
                            const auto cur = row + k *
                                    static_cast<std::size_t>(comp);

                            if (!pixel_equal(prev, cur, comp)) {
                                prev += comp;
                                ++len;
                            }
                            else {
                                --len; // rollback; to run again
                                break;
                            }
                        }
                    }
                    else {
                        // RLE run packet: Increase while pixels are ALL THE SAME.
                        for (size_t k = i + 2; k < x && len < 128; ++k) {
                            const auto cur = row + k *
                                      static_cast<std::size_t>(comp);

                            if (!pixel_equal(begin, cur, comp)) break;
                            ++len;
                        }
                    }
                }

                if (diff) {
                    // RAW: header = len-1 (0..127)
                    const std::uint8_t header = static_cast<std::uint8_t>((len-1) & 0xFF);
                    write_byte(header);
                    for (int k = 0; k < len; ++k) {
                        const std::uint8_t* px = begin + static_cast<std::size_t>(k) * static_cast<std::size_t>(comp);
                        write_pixel(-1, comp, (has_alpha ? 1:0), 0, px);
                    }
                }
                else {
                    // RLE: header = len-129 (128..255 as unsigned), that is (len-1)|0x80
                    const std::uint8_t header = static_cast<std::uint8_t>(((len-1) | 0x80) & 0xFF);
                    write_byte(header);
                    write_pixel(-1, comp, (has_alpha ? 1:0), 0, begin);
                }

                i += len;
            } // while y < x-1
        } // for j != jend

        flush();
        return true;
    } // write_tga_core

    bool Writer::write_png(int x, int y, int comp, const void* data, int stride_in_bytes) noexcept {
        if (!_func || !data) return false;
        if (x <= 0 || y <= 0) return false;
        if (comp < 1 || comp > 4) return false;

        const int row_bytes = x * comp;
        if (stride_in_bytes == 0) stride_in_bytes = row_bytes;
        if (stride_in_bytes < row_bytes) return false;

        int force_filter = _force_png_filter;
        if (force_filter >= 5) force_filter = -1;

        const std::size_t filt_stride = static_cast<std::size_t>(row_bytes) + 1u;
        const std::size_t filt_size = filt_stride * static_cast<std::size_t>(y);

        std::uint8_t* filt = reinterpret_cast<std::uint8_t*>(STBIW_malloc(filt_size, nullptr));
        if (!filt) return false;

        signed char* line = reinterpret_cast<signed char*>(
            STBIW_malloc(static_cast<std::size_t>(row_bytes), nullptr));
        if (!line) {
            STBIW_free(filt);
            return false;
        }

        const auto* pixels = reinterpret_cast<const std::uint8_t*>(data);

        for (int j = 0; j < y; ++j) {
            const int src_row = _flip_vertically_on_write ? (y-1-j) : j;
            const std::uint8_t* cur =
                pixels + (std::size_t)src_row * (std::size_t)stride_in_bytes;

            int chosen = 0;

            if (force_filter >= 0) {
                chosen = force_filter;
                png_apply_filter(
                    static_cast<PngFilter>(chosen), cur,
                    j>0 ? pixels + static_cast<std::size_t>(src_row-1) * stride_in_bytes
                        : nullptr,
                    row_bytes, comp,
                    reinterpret_cast<std::uint8_t*>(line)
                );
            }
            else {
                const std::uint8_t* prev =
                    (j > 0)
                    ? (pixels + static_cast<std::size_t>((_flip_vertically_on_write ? y-j : j) - 1) * stride_in_bytes)
                    : nullptr;

                chosen = png_choose_best_filter(cur, prev ? prev:nullptr,
                    row_bytes, comp,
                    reinterpret_cast<std::uint8_t*>(line),
                    reinterpret_cast<std::uint8_t*>(line)
                );
            }

            std::uint8_t* dst = filt + static_cast<std::size_t>(j) * filt_stride;
            dst[0] = (std::uint8_t)chosen;
            STBIW_memmove(dst + 1, line, static_cast<std::size_t>(row_bytes));
        }


        STBIW_free(line);

        int zlen = 0;
        unsigned char* zlib = zlib::zlib_compress((unsigned char*)filt, (int)filt_size, &zlen, _png_compression_level);
        STBIW_free(filt);
        if (!zlib || zlen <= 0) {
            if (zlib) STBIW_free(zlib);
            return false;
        }

        // --- PNG signature ---
        static const std::uint8_t sig[8] = { 137,80,78,71,13,10,26,10 };
        write_bytes_direct(sig, 8);

        // color type
        // 1->0 (grayscale), 2->4 (grayscale+alpha), 3->2 (rgb), 4->6 (rgba)
        static const std::uint8_t ctype[5] = { 0xFF, 0, 4, 2, 6 };
        const std::uint8_t color_type = ctype[comp];
        if (color_type == 0xFF) {
            STBIW_free(zlib);
            return false;
        }

        // --- IHDR ---
        std::uint8_t ihdr[13];
        zlib::store_be32(ihdr + 0, (std::uint32_t)x);
        zlib::store_be32(ihdr + 4, (std::uint32_t)y);
        ihdr[8] = 8;           // bit depth
        ihdr[9] = color_type;  // color type
        ihdr[10] = 0;           // compression method
        ihdr[11] = 0;           // filter method
        ihdr[12] = 0;           // interlace method

        const std::uint8_t IHDR_tag[4] = { 'I','H','D','R' };
        std::uint32_t ihdr_crc = ~0u;
        ihdr_crc = zlib::crc32_update(ihdr_crc, IHDR_tag, 4);
        ihdr_crc = zlib::crc32_update(ihdr_crc, ihdr, 13);
        ihdr_crc = ~ihdr_crc;

        write_tokens(
            be32(13),
            raw(IHDR_tag, 4),
            raw(ihdr, 13),
            be32(ihdr_crc)
        );

        // --- IDAT ---
        const std::uint8_t IDAT_tag[4] = { 'I','D','A','T' };
        std::uint32_t idat_crc = ~0u;
        idat_crc = zlib::crc32_update(idat_crc, IDAT_tag, 4);
        idat_crc = zlib::crc32_update(idat_crc, (std::uint8_t*)zlib, zlen);
        idat_crc = ~idat_crc;

        write_tokens(be32((std::uint32_t)zlen), raw(IDAT_tag, 4));
        write_bytes_direct(zlib, zlen);
        write_tokens(be32(idat_crc));

        STBIW_free(zlib);

        // --- IEND ---
        const std::uint8_t IEND_tag[4] = { 'I','E','N','D' };
        std::uint32_t iend_crc = ~0u;
        iend_crc = zlib::crc32_update(iend_crc, IEND_tag, 4);
        iend_crc = ~iend_crc;

        write_tokens(
            be32(0),
            raw(IEND_tag, 4),
            be32(iend_crc)
        );

        flush();
        return true;
    }

    namespace png_stream {

        struct IdatChunker {
        private:
            std::uint8_t* _buf{};
            std::uint32_t _cap{};
            std::uint32_t _n{};

        public:
            void begin(std::uint8_t* storage, int storage_bytes) noexcept {
                _buf = storage;
                STBIW_assert(storage_bytes > 0);
                _cap = storage_bytes;
                _n = 0;
            }

            inline void flush_chunk(Writer& w) noexcept;

            void put(Writer& w, const void* p, int bytes) noexcept {
                const std::uint8_t* s = (const std::uint8_t*)p;
                while (bytes > 0) {
                    if (_n == _cap) flush_chunk(w);
                    const int space = static_cast<int>(_cap) - _n;
                    const int take = bytes < space ? bytes : space;
                    STBIW_memmove(_buf + _n, s, static_cast<std::size_t>(take));
                    _n += take;
                    s += take;
                    bytes -= take;
                }
            }

            void end(Writer& w) noexcept { flush_chunk(w); }
        }; // struct IdatChunker

        // streaming zlib: stored blocks (BTYPE=00)
        struct ZlibStoredWriter {
        private:
            std::uint32_t _adler_s1{ 1 }, _adler_s2{ 0 };
            std::uint32_t _bitbuf{ 0 }; // bit writer (for header `stored block` only)
            int _bitcount{ 0 };

        public:
            void begin(Writer& w, IdatChunker& o) noexcept {
                _adler_s1 = 1; _adler_s2 = 0;
                _bitbuf = 0; _bitcount = 0;

                // zlib header for "no compression": 0x78 0x01 (CMF/FLG, check bits ok)
                const std::uint8_t hdr[2] = { 0x78, 0x01 };
                o.put(w, hdr, 2);
            }

            void put_bits(Writer& w, IdatChunker& o, std::uint32_t bits, int nbits) noexcept {
                _bitbuf |= (bits << _bitcount);
                _bitcount += nbits;
                while (_bitcount >= 8) {
                    std::uint8_t b = static_cast<std::uint8_t>(_bitbuf & 0xFFu);
                    o.put(w, &b, 1);
                    _bitbuf >>= 8;
                    _bitcount -= 8;
                }
            }

            void align_byte(Writer& w, IdatChunker& o) noexcept {
                if (_bitcount) put_bits(w, o, 0, 8 - _bitcount);
            }

            void adler_update(const std::uint8_t* p, int n) noexcept {
                // classic Adler-32
                while (n > 0) {
                    int block = (n > 5552) ? 5552 : n;
                    for (int i = 0; i < block; ++i) {
                        _adler_s1 += p[i];
                        _adler_s2 += _adler_s1;
                    }
                    _adler_s1 %= 65521u;
                    _adler_s2 %= 65521u;
                    p += block;
                    n -= block;
                }
            }

            // write stored block(s), split by 65535
            void write_data(Writer& w, IdatChunker& o, const std::uint8_t* data, int len, bool is_final) noexcept {
                while (len > 0) {
                    const int chunk = (len > 65535) ? 65535 : len;
                    const bool final_now = is_final && (chunk == len);

                    // stored block header:
                    // BFINAL (1 bit), BTYPE=00 (2 bits)
                    put_bits(w, o, final_now ? 1 : 0, 1);
                    put_bits(w, o, 0u, 2);
                    align_byte(w, o);

                    // LEN / NLEN (little endian)
                    const std::uint16_t L = (std::uint16_t)chunk;
                    const std::uint16_t NL = (std::uint16_t)~L;

                    std::uint8_t hdr[4] = {
                        static_cast<std::uint8_t>(L & 0xFF),
                        static_cast<std::uint8_t>(L >> 8),
                        static_cast<std::uint8_t>(NL & 0xFF),
                        static_cast<std::uint8_t>(NL >> 8),
                    };
                    o.put(w, hdr, 4);

                    // data
                    o.put(w, data, chunk);
                    adler_update(data, chunk);

                    data += chunk;
                    len -= chunk;
                }
            }

            void end(Writer& w, IdatChunker& o) noexcept {
                // write empty final stored block
                put_bits(w, o, 1, 1); // BFINAL = 1
                put_bits(w, o, 0, 2); // BTYPE = 00
                align_byte(w, o);

                static constexpr std::uint8_t hdr[4]{ 0,0, 0xFF,0xFF };
                o.put(w, hdr, 4);

                align_byte(w, o);
                std::uint32_t adler = _adler_s2 << 16 | _adler_s1 & 0xFFFF;
                std::uint8_t a[4];
                be32_store(a, adler);
                o.put(w, a, 4);
            }
        };
    } // namespace png_stream

    bool Writer::write_png_stream_uncompressed(int x, int y, int comp, const void* data,
            int stride_bytes, void* scratch, std::size_t scratch_bytes,
            int idat_buf_bytes) noexcept {
        if (!_func || !data) return false;
        if (x <= 0 || y <= 0) return false;
        if (comp < 1 || comp > 4) return false;

        const int row_bytes = x * comp;
        if (stride_bytes == 0) stride_bytes = row_bytes;
        if (stride_bytes < row_bytes) return false;

        // scratch layout:
        // [prev_row row_bytes]
        // [work_row row_bytes]  (best filtered bytes)
        // [temp_row row_bytes]  (optional for trying filters; we can reuse)
        // [idat_buf idat_buf_bytes]
        const std::size_t need = static_cast<std::size_t>(row_bytes) * 3u
                               + static_cast<std::size_t>(idat_buf_bytes);

        if (!scratch || scratch_bytes < need) return false;

        std::uint8_t* mem = static_cast<std::uint8_t*>(scratch);
        std::uint8_t* prev = mem; mem += row_bytes;
        std::uint8_t* best = mem; mem += row_bytes;
        std::uint8_t* tmp  = mem; mem += row_bytes;
        std::uint8_t* idat = mem; // idat_buf_bytes

        STBIW_memset(prev, 0, static_cast<std::size_t>(row_bytes));

        // PNG signature
        static const std::uint8_t sig[8] = { 137,80,78,71,13,10,26,10 };
        write_bytes_direct(sig, 8);

        // IHDR
        static constexpr std::uint8_t IHDR[4]{ 'I','H','D','R' };
        static constexpr std::uint8_t IEND[4]{ 'I','E','N','D' };
        static constexpr std::uint8_t ctype[5]{ 0xFF, 0, 4, 2, 6 };

        const std::uint8_t color_type = ctype[comp];
        if (color_type == 0xFF) return false;

        std::uint8_t ihdr[13];
        be32_store(ihdr+0, static_cast<std::uint32_t>(x));
        be32_store(ihdr+4, static_cast<std::uint32_t>(y));
        ihdr[8] = 8;
        ihdr[9] = color_type;
        ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

        std::uint32_t ihdr_crc = ~0u;
        ihdr_crc = zlib::crc32_update(ihdr_crc, IHDR, 4);
        ihdr_crc = zlib::crc32_update(ihdr_crc, ihdr, 13);
        ihdr_crc = ~ihdr_crc;

        std::uint8_t len13[4], crc_be[4];
        be32_store(len13, 13);
        be32_store(crc_be, ihdr_crc);

        write_bytes_direct(len13, 4);
        write_bytes_direct(IHDR, 4);
        write_bytes_direct(ihdr, 13);
        write_bytes_direct(crc_be, 4);

        // IDAT stream
        png_stream::IdatChunker chunker;
        chunker.begin(idat, idat_buf_bytes);

        png_stream::ZlibStoredWriter z;
        z.begin(*this, chunker);

        const std::uint8_t* base = static_cast<const std::uint8_t*>(data);
        const int force_filter = _force_png_filter>=5 ? -1 : _force_png_filter;

        for (int row = 0; row < y; ++row) {
            const int src_row = _flip_vertically_on_write ? (y-1 - row) : row;
            const std::uint8_t* cur =
                base + static_cast<std::size_t>(src_row)
                     * static_cast<std::size_t>(stride_bytes);

            int chosen = 0;

            if (_force_png_filter >= 0 && _force_png_filter <= 4) {
                chosen = _force_png_filter;
                png_apply_filter(static_cast<PngFilter>(chosen), cur, prev, row_bytes, comp, best);
            }
            else {
                chosen = png_choose_best_filter(
                    cur, prev, row_bytes, comp, tmp, best);
            }

            const std::uint8_t fbyte = static_cast<std::uint8_t>(chosen);
            z.write_data(*this, chunker, &fbyte, 1, false);
            z.write_data(*this, chunker, best, row_bytes, false);

            STBIW_memmove(prev, cur, static_cast<std::size_t>(row_bytes));
        }


        z.end(*this, chunker);
        chunker.end(*this);

        // IEND
        std::uint8_t zero[4]{ 0,0,0,0 };
        std::uint32_t iend_crc = ~0u;
        iend_crc = zlib::crc32_update(iend_crc, IEND, 4);
        iend_crc = ~iend_crc;

        std::uint8_t iend_crc_be[4];
        be32_store(iend_crc_be, iend_crc);
        write_bytes_direct(zero, 4);
        write_bytes_direct(IEND, 4);
        write_bytes_direct(iend_crc_be, 4);

        flush();
        return true;
    }


    void png_stream::IdatChunker::flush_chunk(Writer& w) noexcept {
        if ( !_n || !_cap ) return;

        static constexpr std::uint8_t tag[4]{ 'I','D','A','T' };

        std::uint8_t len_be[4];
        be32_store(len_be, _n);

        std::uint32_t crc = ~0u;
        crc = zlib::crc32_update(crc, tag, 4);
        crc = zlib::crc32_update(crc, _buf, _n);
        crc = ~crc;

        std::uint8_t crc_be[4];
        be32_store(crc_be, crc);

        w.write_bytes_direct(len_be, 4);
        w.write_bytes_direct(tag, 4);
        w.write_bytes_direct(_buf, _n);
        w.write_bytes_direct(crc_be, 4);

        _n = 0;
    }



    inline void png_apply_filter(
        PngFilter f,
        const std::uint8_t* cur,
        const std::uint8_t* prev,
        int row_bytes,
        int comp,
        std::uint8_t* dst
    ) noexcept {
        switch (f) {
        case PngFilter::None:
            for (int i = 0; i < row_bytes; ++i) dst[i] = cur[i];
            break;

        case PngFilter::Sub:
            for (int i = 0; i < row_bytes; ++i) {
                dst[i] = cur[i] - (i >= comp ? cur[i - comp] : 0);
            }
            break;

        case PngFilter::Up:
            for (int i = 0; i < row_bytes; ++i) {
                dst[i] = cur[i] - (prev ? prev[i] : 0);
            }
            break;

        case PngFilter::Avg:
            for (int i = 0; i < row_bytes; ++i) {
                const int a = (i >= comp ? cur[i - comp] : 0);
                const int b = (prev ? prev[i] : 0);
                dst[i] = cur[i] - ((a + b) >> 1);
            }
            break;

        case PngFilter::Paeth:
            for (int i = 0; i < row_bytes; ++i) {
                const int a = (i >= comp ? cur[i - comp] : 0);
                const int b = (prev ? prev[i] : 0);
                const int c = (i >= comp && prev ? prev[i - comp] : 0);
                int p = a + b - c;
                int pa = p-a;
                int pb = p-b;
                int pc = p-c;
                pa = pa<0?-pa:pa;
                pb = pb<0?-pb:pb;
                pc = pc<0?-pc:pc;
                int pr = (pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
                dst[i] = cur[i] - pr;
            }
            break;
        }
    }

    inline int png_choose_best_filter(
        const std::uint8_t* cur,
        const std::uint8_t* prev,
        int row_bytes,
        int comp,
        std::uint8_t* tmp,
        std::uint8_t* best
    ) noexcept {
        int best_f = 0;
        int best_est = 0x7fffffff;

        for (int f = 0; f < 5; ++f) {
            png_apply_filter(static_cast<PngFilter>(f), cur, prev, row_bytes, comp, tmp);
            int est = 0;
            for (int i = 0; i < row_bytes; ++i) {
                const std::int8_t v = static_cast<std::int8_t>(tmp[i]);
                est += (v < 0) ? -v : v;
            }
            if (est < best_est) {
                best_est = est;
                best_f = f;
                STBIW_memmove(best, tmp, static_cast<std::size_t>(row_bytes));
            }
        }
        return best_f;
    }


    // --- static private helpers ---

    bool Writer::pixel_equal(const std::uint8_t* a, const std::uint8_t* b, int comp) noexcept {
        // comp in [1..4]
        switch (comp) {
        case 1: return a[0] == b[0];
        case 2: return a[0] == b[0] && a[1] == b[1];
        case 3: return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
        case 4: return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
        default: return false;
        }
    }
} // namespace stbiw