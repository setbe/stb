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

   This header file is a library for writing images to C stdio or a callback.

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

   You can define STBI_WRITE_NO_STDIO to disable the file variant of these
   functions, so the library will not use stdio.h at all. However, this will
   also disable HDR writing, because it requires stdio for formatted output.

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

// ------------------- Freestanding-friendly Includes -------------------------
#include <cstddef>
#include <cstdint>

#include "detail/libc_integration.hpp" // Checkout this file for freestanding intergration

namespace stbiw {
    // -------------------------------- Tokens --------------------------------

    struct b1_t { std::uint8_t  v; };
    struct le16_t { std::uint16_t v; };
    struct le32_t { std::uint32_t v; }; // bitwise payload
    struct raw_t { const void* p; int n; };

    constexpr inline b1_t b1(std::uint32_t x) noexcept {
        return { static_cast<std::uint8_t>(x & 0xFFU) };
    }

    constexpr inline le16_t le16(std::uint32_t x) noexcept {
        return { static_cast<std::uint16_t>(x & 0xFFFFU) };
    }

    constexpr inline le32_t le32(std::uint32_t x) noexcept {
        return { x };
    }

    constexpr inline le32_t le32i(std::int32_t x) noexcept {
        // keeps signed bit-pattern
        return { static_cast<std::uint32_t>(x) };
    }

    constexpr inline raw_t raw(const void* p, int n) noexcept {
        return { p, n };
    }

    // --- buffering sizes ---
    constexpr inline int token_size(b1_t)   noexcept { return 1; }
    constexpr inline int token_size(le16_t) noexcept { return 2; }
    constexpr inline int token_size(le32_t) noexcept { return 4; }
    constexpr inline int token_size(raw_t)  noexcept { return -1; }






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
        inline void emit(raw_t r) noexcept {
            flush();
            write_bytes_direct(r.p, r.n);
        }

        template <typename... Ts>
        inline void write_tokens(Ts... ts) noexcept {
            // C++11 pack expansion
            int dummy[] = { 0, (emit(ts), 0)... };
            (void)dummy;
        }

        // At the end of headers/strings, it is usually useful to flush
        inline void write_flush() noexcept { flush(); }


    public:
        inline void write_pixel(int rgb_dir,
            int comp,
            int write_alpha,
            int expand_mono,
            const std::uint8_t* d) noexcept;

        inline void write_pixels(int rgb_dir,
            int vdir,
            int x, int y,
            int comp,
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

        inline bool write_bmp_core(int x, int y, int comp, const void* data) noexcept;
        inline bool write_tga_core(int x, int y, int comp, const void* data) noexcept;



        // --- private static helper-members ---
        static inline bool pixel_equal(const std::uint8_t* a, const std::uint8_t* b, int comp) noexcept;
    }; // struct Writer






    void Writer::write_pixel(int rgb_dir,
                                    int comp,
                                    int write_alpha,
                                    int expand_mono,
                                    const std::uint8_t* d) noexcept {
        static constexpr std::uint8_t bg[3]{ 255, 0, 255 };
        std::uint8_t px[3];

        if (write_alpha < 0)
            write_byte(d[comp-1]);

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
                    px[k] = static_cast<std::uint8_t>( bg[k] + j * d3 / 255);
                }
                write3(px[1-rgb_dir], px[1], px[1+rgb_dir]);
                break;
            }
            // fallthrough
        case 3:
            write3(d[1-rgb_dir], d[1], d[1+rgb_dir]);
            break;
        } // switch

        if (write_alpha > 0)
            write_byte(d[comp-1]);
    } // write_pixel

    void Writer::write_pixels(int rgb_dir,
                                     int vdir,
                                     int x, int y,
                                     int comp,
                                     const void* data,
                                     int write_alpha,
                                     int scanline_pad,
                                     int expand_mono) noexcept {
        if (y <= 0) return;
        if (_flip_vertically_on_write) vdir *= -1;

        int j, j_end;
        j = j_end = 0;

        if (vdir < 0) { j = y-1;  j_end = -1; }
        else          { j = 0;    j_end = y;  }

        static constexpr std::uint8_t zeros4[4]{ 0,0,0,0 };

        auto* base = static_cast<const std::uint8_t*>(data);

        for (; j != j_end; j += vdir) {
            for (int i=0; i<x; ++i) {
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


    bool Writer::write_bmp_core(int x, int y, int comp, const void* data) noexcept {
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
    

    bool Writer::write_tga_core(int x, int y, int comp, const void* data) noexcept {
        if (!data || !_func) return false;
        if (x <= 0 || y <= 0) return false;
        if (comp < 1 || comp > 4) return false;

        const bool has_alpha  = (comp==2 || comp==4);
        const int  colorbytes = has_alpha ? (comp-1) : comp;
        const int  format     = (colorbytes < 2) ? 3 : 2; // 3=grayscale, 2=truecolor

        // --- TGA header (18 bytes) ---
        // idlength, colormaptype, imagetype,
        // cmap_first (le16), cmap_len   (le16), cmap_depth,
        // x_origin   (le16), y_origin   (le16),
        // width      (le16), height     (le16),
        // pixel_depth, image_descriptor
        auto write_tga_header = [&](int image_type) noexcept {
            const std::uint8_t pixel_depth =
                static_cast<std::uint8_t>((colorbytes + (has_alpha ? 1 : 0)) * 8);

            const std::uint8_t descriptor  =
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
                b1(static_cast<std::uint8_t>((colorbytes + (has_alpha ? 1 : 0)) * 8) ),
                b1(static_cast<std::uint8_t>(has_alpha ? 8 : 0) )
            );
        }

        // -----------------------------------------------
        //      RLE: write header, then RLE strings
        // -----------------------------------------------
        write_tga_header(format + 8); // 10/11

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        int j, jend, jdir;
        j = jend = jdir = 0;

        if (_flip_vertically_on_write) {
            j = 0;   jend = y;  jdir = 1;
        } else {
            j = y-1; jend = -1; jdir = -1;
        }

        for (; j != jend; j += jdir) {
            const std::uint8_t* row = bytes + (std::size_t)j * (std::size_t)x * (std::size_t)comp;
            int i = 0;

            while (i < x) {
                const std::uint8_t* begin = row + static_cast<std::size_t>(i) * static_cast<std::size_t>(comp);
                int len = 1;
                bool diff = true; // true => RAW packet, false => RLE run packet

                if (i < x - 1) {
                    // try determine the packet type with first comparison
                    diff = !pixel_equal(begin, row + static_cast<std::size_t>(i+1) * static_cast<std::size_t>(comp), comp);
                    len = 2;

                    if (diff) {
                        // RAW packet: Increase while pixels are DIFFERENT from the previous ones (to avoid run)
                        const std::uint8_t* prev = begin;
                        for (int k = i + 2; k < x && len < 128; ++k) {
                            const std::uint8_t* cur = row + (std::size_t)k * (std::size_t)comp;
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
                        for (int k = i + 2; k < x && len < 128; ++k) {
                            const std::uint8_t* cur = row + static_cast<std::size_t>(k) * static_cast<std::size_t>(comp);

                            if (pixel_equal(begin, cur, comp))
                                ++len;
                            else
                                break;
                        }
                    }
                }

                if (diff) {
                    // RAW: header = len-1 (0..127)
                    const std::uint8_t header = static_cast<std::uint8_t>((len - 1) & 0xFF);
                    write_byte(header);
                    for (int k = 0; k < len; ++k) {
                        const std::uint8_t* px = begin + static_cast<std::size_t>(k) * static_cast<std::size_t>(comp);
                        write_pixel(/*rgb_dir*/ -1, comp, /*write_alpha*/ (has_alpha ? 1 : 0), /*expand_mono*/ 0, px);
                    }
                }
                else {
                    // RLE: header = len-129 (128..255 as unsigned), that is (len-1)|0x80
                    const std::uint8_t header = static_cast<std::uint8_t>(((len - 1) | 0x80) & 0xFF);
                    write_byte(header);
                    write_pixel(/*rgb_dir*/ -1, comp, /*write_alpha*/ (has_alpha ? 1 : 0), /*expand_mono*/ 0, begin);
                }

                i += len;
            } // while y < x-1
        } // for j != jend

        flush();
        return true;
    } // write_tga_core

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