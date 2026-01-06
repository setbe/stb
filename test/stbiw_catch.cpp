#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <cstdint>
#include <vector>
#include <cstring>   // std::memcmp
#include <string>

// C++ port
#include "../stb_image_write/stb_image_write.hpp"

// C reference
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb_image_write/stb_image_write.h"

namespace {



    // --------------------------- Helpers: byte sink ---------------------------

    static void cb_const(void* ctx, const void* data, int size) {
        auto& out = *static_cast<std::vector<std::uint8_t>*>(ctx);
        const auto* p = static_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + size);
    }

    static void cb_legacy(void* ctx, void* data, int size) {
        auto& out = *static_cast<std::vector<std::uint8_t>*>(ctx);
        const auto* p = static_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + size);
    }

    struct StbGuard {
        int old_rle = 1;
        StbGuard(bool rle, bool flip) {
            old_rle = stbi_write_tga_with_rle;
            stbi_write_tga_with_rle = rle ? 1 : 0;
            stbi_flip_vertically_on_write(flip ? 1 : 0);
        }
        ~StbGuard() {
            stbi_write_tga_with_rle = old_rle;
            stbi_flip_vertically_on_write(0);
        }
    };



    // --------------------------- Helpers: TGA header checks ---------------------------

    static std::uint16_t rd_le16(const std::vector<std::uint8_t>& v, std::size_t off) {
        REQUIRE(v.size() >= off + 2);
        return (std::uint16_t)(v[off] | (std::uint16_t(v[off + 1]) << 8));
    }

    static void require_tga_header(
        const std::vector<std::uint8_t>& bytes,
        int w, int h, int comp,
        bool rle_enabled
    ) {
        REQUIRE(bytes.size() >= 18);

        const bool has_alpha = (comp == 2 || comp == 4);
        const int  colorbytes = has_alpha ? (comp - 1) : comp;
        const int  format = (colorbytes < 2) ? 3 : 2;          // grayscale=3, truecolor=2
        const int  image_type = rle_enabled ? (format + 8) : format; // RLE => +8

        const std::uint8_t pixel_depth = (std::uint8_t)((colorbytes + (has_alpha ? 1 : 0)) * 8);
        const std::uint8_t descriptor = (std::uint8_t)(has_alpha ? 8 : 0);

        // idlength, colormaptype
        REQUIRE(bytes[0] == 0);
        REQUIRE(bytes[1] == 0);

        // imagetype
        REQUIRE(bytes[2] == (std::uint8_t)image_type);

        // color map spec = zeros
        REQUIRE(rd_le16(bytes, 3) == 0);
        REQUIRE(rd_le16(bytes, 5) == 0);
        REQUIRE(bytes[7] == 0);

        // x_origin/y_origin = zeros
        REQUIRE(rd_le16(bytes, 8) == 0);
        REQUIRE(rd_le16(bytes, 10) == 0);

        // width/height
        REQUIRE(rd_le16(bytes, 12) == (std::uint16_t)w);
        REQUIRE(rd_le16(bytes, 14) == (std::uint16_t)h);

        // pixel depth + descriptor
        REQUIRE(bytes[16] == pixel_depth);
        REQUIRE(bytes[17] == descriptor);
    }




    // --------------------------- BMP helpers ---------------------------

    static std::uint32_t rd_le32(const std::vector<std::uint8_t>& v, std::size_t off) {
        REQUIRE(v.size() >= off + 4);
        return (std::uint32_t)v[off + 0]
            | ((std::uint32_t)v[off + 1] << 8)
            | ((std::uint32_t)v[off + 2] << 16)
            | ((std::uint32_t)v[off + 3] << 24);
    }

    static std::int32_t rd_le32s(const std::vector<std::uint8_t>& v, std::size_t off) {
        return static_cast<std::int32_t>(rd_le32(v, off));
    }

    struct StbFlipGuard {
        int old_rle = 1;
        StbFlipGuard(bool flip) {
            // preserve unrelated global too (stb uses it elsewhere)
            old_rle = stbi_write_tga_with_rle;
            stbi_flip_vertically_on_write(flip ? 1 : 0);
        }
        ~StbFlipGuard() {
            stbi_flip_vertically_on_write(0);
            stbi_write_tga_with_rle = old_rle;
        }
    };

    static void require_bmp_header_24(
        const std::vector<std::uint8_t>& bytes,
        int w, int h,
        int pad
    ) {
        // BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40)
        REQUIRE(bytes.size() >= 54);

        REQUIRE(bytes[0] == 'B');
        REQUIRE(bytes[1] == 'M');

        const std::uint32_t file_size = rd_le32(bytes, 2);
        REQUIRE(file_size == bytes.size());

        REQUIRE(rd_le16(bytes, 6) == 0);
        REQUIRE(rd_le16(bytes, 8) == 0);

        const std::uint32_t pixel_off = rd_le32(bytes, 10);
        REQUIRE(pixel_off == 54);

        const std::uint32_t dib_size = rd_le32(bytes, 14);
        REQUIRE(dib_size == 40);

        REQUIRE(rd_le32s(bytes, 18) == w);
        REQUIRE(rd_le32s(bytes, 22) == h);
        REQUIRE(rd_le16(bytes, 26) == 1);
        REQUIRE(rd_le16(bytes, 28) == 24);

        REQUIRE(rd_le32(bytes, 30) == 0); // BI_RGB

        // stb keeps these 0
        REQUIRE(rd_le32(bytes, 34) == 0); // sizeImage
        REQUIRE(rd_le32(bytes, 38) == 0); // xppm
        REQUIRE(rd_le32(bytes, 42) == 0); // yppm
        REQUIRE(rd_le32(bytes, 46) == 0); // clrUsed
        REQUIRE(rd_le32(bytes, 50) == 0); // clrImportant

        const std::size_t expected = 14ull + 40ull + (std::size_t)(w * 3 + pad) * (std::size_t)h;
        REQUIRE(bytes.size() == expected);
    }

    static void require_bmp_header_32_v4(
        const std::vector<std::uint8_t>& bytes,
        int w, int h
    ) {
        // BITMAPFILEHEADER (14) + BITMAPV4HEADER (108)
        REQUIRE(bytes.size() >= 14 + 108);

        REQUIRE(bytes[0] == 'B');
        REQUIRE(bytes[1] == 'M');

        const std::uint32_t file_size = rd_le32(bytes, 2);
        REQUIRE(file_size == bytes.size());

        const std::uint32_t pixel_off = rd_le32(bytes, 10);
        REQUIRE(pixel_off == 14 + 108);

        const std::uint32_t dib_size = rd_le32(bytes, 14);
        REQUIRE(dib_size == 108);

        REQUIRE(rd_le32s(bytes, 18) == w);
        REQUIRE(rd_le32s(bytes, 22) == h);
        REQUIRE(rd_le16(bytes, 26) == 1);
        REQUIRE(rd_le16(bytes, 28) == 32);

        REQUIRE(rd_le32(bytes, 30) == 3); // BI_BITFIELDS

        // masks @ offsets in BITMAPV4HEADER
        // DIB starts at 14, so:
        // redMask   @ 14+40 = 54
        // greenMask @ 58
        // blueMask  @ 62
        // alphaMask @ 66
        REQUIRE(rd_le32(bytes, 54) == 0x00FF0000u);
        REQUIRE(rd_le32(bytes, 58) == 0x0000FF00u);
        REQUIRE(rd_le32(bytes, 62) == 0x000000FFu);
        REQUIRE(rd_le32(bytes, 66) == 0xFF000000u);

        const std::size_t expected = 14ull + 108ull + (std::size_t)w * (std::size_t)h * 4ull;
        REQUIRE(bytes.size() == expected);
    }

    static std::vector<std::uint8_t> write_bmp_cpp(
        int w, int h, int comp,
        const std::vector<std::uint8_t>& pixels,
        bool flip
    ) {
        std::vector<std::uint8_t> out;
        stbiw::Writer wr;
        wr.start_callbacks(&cb_const, &out);
        wr.set_flip_vertically(flip);

        const bool ok = wr.write_bmp_core(w, h, comp, pixels.data());
        wr.flush();

        REQUIRE(ok);
        REQUIRE(out.size() >= 54);
        return out;
    }

    static std::vector<std::uint8_t> write_bmp_stb(
        int w, int h, int comp,
        const std::vector<std::uint8_t>& pixels,
        bool flip
    ) {
        std::vector<std::uint8_t> out;
        StbFlipGuard guard(flip);

        const int ok = stbi_write_bmp_to_func(&cb_legacy, &out, w, h, comp, pixels.data());
        REQUIRE(ok != 0);
        REQUIRE(out.size() >= 54);
        return out;
    }




    // --------------------------- Writers (cpp vs stb) ---------------------------

    static std::vector<std::uint8_t> write_tga_cpp(
        int w, int h, int comp,
        const std::vector<std::uint8_t>& pixels,
        bool rle, bool flip
    ) {
        std::vector<std::uint8_t> out;
        stbiw::Writer wr;
        wr.start_callbacks(&cb_const, &out);
        wr.set_tga_rle(rle);
        wr.set_flip_vertically(flip);

        const bool ok = wr.write_tga_core(w, h, comp, pixels.data());
        wr.flush();

        REQUIRE(ok);
        REQUIRE(out.size() >= 18);
        return out;
    }

    static std::vector<std::uint8_t> write_tga_stb(
        int w, int h, int comp,
        const std::vector<std::uint8_t>& pixels,
        bool rle, bool flip
    ) {
        std::vector<std::uint8_t> out;
        StbGuard guard(rle, flip);

        const int ok = stbi_write_tga_to_func(&cb_legacy, &out, w, h, comp, pixels.data());
        REQUIRE(ok != 0);
        REQUIRE(out.size() >= 18);
        return out;
    }




    // --------------------------- Pixel generators ---------------------------

    static std::vector<std::uint8_t> make_pattern(int w, int h, int comp) {
        std::vector<std::uint8_t> p((std::size_t)w * (std::size_t)h * (std::size_t)comp);

        // deterministic pattern: different bytes, alpha also changes
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t idx = ((std::size_t)y * (std::size_t)w + (std::size_t)x) * (std::size_t)comp;

                // base value depends on position
                const std::uint8_t a = (std::uint8_t)((x + 1) * 17 + (y + 1) * 23);
                if (comp == 1) {
                    p[idx + 0] = a;
                }
                else if (comp == 2) {
                    p[idx + 0] = a;                       // Y
                    p[idx + 1] = (std::uint8_t)(255 - a); // A
                }
                else if (comp == 3) {
                    p[idx + 0] = a;                        // R
                    p[idx + 1] = (std::uint8_t)(a ^ 0x5A); // G
                    p[idx + 2] = (std::uint8_t)(a + 11);   // B
                }
                else { // comp == 4
                    p[idx + 0] = a;                              // R
                    p[idx + 1] = (std::uint8_t)(a ^ 0x5A);       // G
                    p[idx + 2] = (std::uint8_t)(a + 11);         // B
                    p[idx + 3] = (std::uint8_t)(50 + (a % 200)); // A (not 0/255)
                }
            }
        }
        return p;
    }

    static std::vector<std::uint8_t> make_rle_friendly_row_rgb(int w) {
        // 1xW, comp=3
        // A A A  B C  C C  D  (creates: run(3), raw(2), run(3), raw(1))
        REQUIRE(w == 8);
        const int comp = 3;
        std::vector<std::uint8_t> p((std::size_t)w * (std::size_t)comp);

        auto set = [&](int x, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const std::size_t i = (std::size_t)x * comp;
            p[i + 0] = r; p[i + 1] = g; p[i + 2] = b;
        };

        // A
        for (int i = 0; i < 3; ++i) set(i, 10, 20, 30);
        // B
        set(3, 40, 50, 60);
        // C C C
        for (int i = 4; i < 7; ++i) set(i, 70, 80, 90);
        // D
        set(7, 1, 2, 3);

        return p;
    }

} // namespace








// --------------------------------- TGA Tests --------------------------------

TEST_CASE("TGA: byte-diff vs stb (no RLE, no flip), comp=1..4", "[tga][byte-diff][strict]") {
    const int w = 3, h = 2;

    for (int comp = 1; comp <= 4; ++comp) {
        auto pixels = make_pattern(w, h, comp);

        auto a = write_tga_cpp(w, h, comp, pixels, /*rle*/false, /*flip*/false);
        auto b = write_tga_stb(w, h, comp, pixels, /*rle*/false, /*flip*/false);

        REQUIRE(a == b);

        require_tga_header(a, w, h, comp, /*rle_enabled*/false);

        // no-RLE: strict size = 18 + w*h*comp
        const std::size_t expected_size = 18ull + (std::size_t)w * (std::size_t)h * (std::size_t)comp;
        REQUIRE(a.size() == expected_size);

        // Check the first pixel in the file (after the header):
        // TGA writes from bottom to top (descriptor bit 5 = 0),
        // so the first pixel = bottom-left (x=0,y=h-1)
        const std::size_t src_idx = ((std::size_t)(h - 1) * (std::size_t)w + 0ull) * (std::size_t)comp;

        if (comp == 1) {
            REQUIRE(a[18] == pixels[src_idx + 0]);
        }
        else if (comp == 2) {
            REQUIRE(a[18] == pixels[src_idx + 0]); // Y
            REQUIRE(a[19] == pixels[src_idx + 1]); // A
        }
        else if (comp == 3) {
            // output is BGR
            REQUIRE(a[18] == pixels[src_idx + 2]);
            REQUIRE(a[19] == pixels[src_idx + 1]);
            REQUIRE(a[20] == pixels[src_idx + 0]);
        }
        else if (comp == 4) {
            // output is BGRA
            REQUIRE(a[18] == pixels[src_idx + 2]);
            REQUIRE(a[19] == pixels[src_idx + 1]);
            REQUIRE(a[20] == pixels[src_idx + 0]);
            REQUIRE(a[21] == pixels[src_idx + 3]);
        }
    }
}

TEST_CASE("TGA: byte-diff vs stb (RLE on, no flip), comp=1..4", "[tga][byte-diff][rle][strict]") {
    const int w = 13, h = 7;

    for (int comp = 1; comp <= 4; ++comp) {
        auto pixels = make_pattern(w, h, comp);

        auto a = write_tga_cpp(w, h, comp, pixels, /*rle*/true, /*flip*/false);
        auto b = write_tga_stb(w, h, comp, pixels, /*rle*/true, /*flip*/false);

        REQUIRE(a == b);
        require_tga_header(a, w, h, comp, /*rle_enabled*/true);

        // RLE: The size can vary, but it must be >= header.
        REQUIRE(a.size() > 18);
    }
}

TEST_CASE("TGA: byte-diff vs stb (RLE on, flip on), comp=3 and comp=4", "[tga][byte-diff][flip][strict]") {
    const int w = 9, h = 5;

    for (int comp : {3, 4}) {
        auto pixels = make_pattern(w, h, comp);

        auto a = write_tga_cpp(w, h, comp, pixels, /*rle*/true, /*flip*/true);
        auto b = write_tga_stb(w, h, comp, pixels, /*rle*/true, /*flip*/true);

        REQUIRE(a == b);
        require_tga_header(a, w, h, comp, /*rle_enabled*/true);
    }
}

TEST_CASE("TGA: strict RLE packet structure (first packet is run len=3)", "[tga][rle][packet][strict]") {
    const int w = 8, h = 1, comp = 3;
    auto pixels = make_rle_friendly_row_rgb(w);

    auto out_cpp = write_tga_cpp(w, h, comp, pixels, /*rle*/true, /*flip*/false);
    auto out_stb = write_tga_stb(w, h, comp, pixels, /*rle*/true, /*flip*/false);

    REQUIRE(out_cpp == out_stb);
    require_tga_header(out_cpp, w, h, comp, /*rle_enabled*/true);

    // After header (18) comes the first packet header.
    // For run length=3 => header = (len-1)|0x80 = 0x82
    REQUIRE(out_cpp.size() >= 18 + 1 + 3);
    REQUIRE(out_cpp[18] == 0x82);

    // Next has to be 1 pixel in BGR (because rgb_dir=-1).
    // A = (R=10,G=20,B=30) => output BGR = 30,20,10
    REQUIRE(out_cpp[19] == 30);
    REQUIRE(out_cpp[20] == 20);
    REQUIRE(out_cpp[21] == 10);
}

TEST_CASE("TGA: 1x1 byte-diff vs stb (RLE off/on, flip off/on), comp=1..4", "[tga][byte-diff][1x1][strict]") {
    const int w = 1, h = 1;

    for (int comp = 1; comp <= 4; ++comp) {
        auto pixels = make_pattern(w, h, comp);

        for (bool rle : {false, true}) {
            for (bool flip : {false, true}) {
                auto a = write_tga_cpp(w, h, comp, pixels, rle, flip);
                auto b = write_tga_stb(w, h, comp, pixels, rle, flip);

                REQUIRE(a == b);
                require_tga_header(a, w, h, comp, rle);
            }
        }
    }
}

TEST_CASE("TGA: C++ writer rejects invalid args strictly", "[tga][invalid][strict]") {
    // Here we do NOT call stb with incorrect arguments,
    // because the reference stb does not check data==nullptr for tga and may crash.
    stbiw::Writer wr;
    std::vector<std::uint8_t> sink;
    wr.start_callbacks(&cb_const, &sink);

    SECTION("null data") {
        REQUIRE_FALSE(wr.write_tga_core(1, 1, 3, nullptr));
        REQUIRE(sink.empty());
    }

    SECTION("bad comp") {
        std::vector<std::uint8_t> px(4);
        REQUIRE_FALSE(wr.write_tga_core(1, 1, 0, px.data()));
        REQUIRE_FALSE(wr.write_tga_core(1, 1, 5, px.data()));
    }

    SECTION("non-positive dims") {
        std::vector<std::uint8_t> px(4);
        REQUIRE_FALSE(wr.write_tga_core(0, 1, 1, px.data()));
        REQUIRE_FALSE(wr.write_tga_core(1, 0, 1, px.data()));
        REQUIRE_FALSE(wr.write_tga_core(-1, 1, 1, px.data()));
        REQUIRE_FALSE(wr.write_tga_core(1, -1, 1, px.data()));
    }
}






// ---------------------------------- BMP Tests ----------------------------------

TEST_CASE("BMP: byte-diff vs stb (no flip), comp=1..4", "[bmp][byte-diff][strict]") {
    const int w = 5, h = 3;

    for (int comp = 1; comp <= 4; ++comp) {
        auto pixels = make_pattern(w, h, comp);

        auto a = write_bmp_cpp(w, h, comp, pixels, /*flip*/false);
        auto b = write_bmp_stb(w, h, comp, pixels, /*flip*/false);

        REQUIRE(a == b);

        if (comp != 4) {
            const int pad = (-w * 3) & 3;
            require_bmp_header_24(a, w, h, pad);

            // strict: padding bytes are zero for each scanline
            const std::size_t pixel_off = rd_le32(a, 10);
            const std::size_t row_bytes = (std::size_t)(w * 3 + pad);
            REQUIRE(pixel_off + row_bytes * (std::size_t)h == a.size());

            for (int row = 0; row < h; ++row) {
                const std::size_t row_end = pixel_off + row_bytes * (std::size_t)row + (std::size_t)(w * 3);
                for (int k = 0; k < pad; ++k) {
                    REQUIRE(a[row_end + (std::size_t)k] == 0);
                }
            }

            // strict: first pixel in file is bottom-left in BGR (BMP is bottom-up, positive height)
            const std::size_t src_idx = ((std::size_t)(h - 1) * (std::size_t)w + 0ull) * (std::size_t)comp;
            const std::size_t pix0 = pixel_off;

            if (comp == 1) {
                // expanded mono => B=Y, G=Y, R=Y
                REQUIRE(a[pix0 + 0] == pixels[src_idx + 0]);
                REQUIRE(a[pix0 + 1] == pixels[src_idx + 0]);
                REQUIRE(a[pix0 + 2] == pixels[src_idx + 0]);
            }
            else if (comp == 2) {
                // Y + A, A ignored for 24bpp BMP, still expanded mono
                REQUIRE(a[pix0 + 0] == pixels[src_idx + 0]);
                REQUIRE(a[pix0 + 1] == pixels[src_idx + 0]);
                REQUIRE(a[pix0 + 2] == pixels[src_idx + 0]);
            }
            else { // comp == 3
                // RGB input => BGR output
                REQUIRE(a[pix0 + 0] == pixels[src_idx + 2]);
                REQUIRE(a[pix0 + 1] == pixels[src_idx + 1]);
                REQUIRE(a[pix0 + 2] == pixels[src_idx + 0]);
            }
        }
        else {
            require_bmp_header_32_v4(a, w, h);

            const std::size_t pixel_off = rd_le32(a, 10);
            REQUIRE(pixel_off == 14 + 108);

            // strict: first pixel in file is bottom-left in BGRA
            const std::size_t src_idx = ((std::size_t)(h - 1) * (std::size_t)w + 0ull) * 4ull;
            const std::size_t pix0 = pixel_off;

            REQUIRE(a[pix0 + 0] == pixels[src_idx + 2]); // B
            REQUIRE(a[pix0 + 1] == pixels[src_idx + 1]); // G
            REQUIRE(a[pix0 + 2] == pixels[src_idx + 0]); // R
            REQUIRE(a[pix0 + 3] == pixels[src_idx + 3]); // A
        }
    }
}

TEST_CASE("BMP: byte-diff vs stb (flip on), comp=1..4", "[bmp][byte-diff][flip][strict]") {
    const int w = 7, h = 4;

    for (int comp = 1; comp <= 4; ++comp) {
        auto pixels = make_pattern(w, h, comp);

        auto a = write_bmp_cpp(w, h, comp, pixels, /*flip*/true);
        auto b = write_bmp_stb(w, h, comp, pixels, /*flip*/true);

        REQUIRE(a == b);

        if (comp != 4) {
            const int pad = (-w * 3) & 3;
            require_bmp_header_24(a, w, h, pad);
        }
        else {
            require_bmp_header_32_v4(a, w, h);
        }
    }
}

TEST_CASE("BMP: 1x1 byte-diff vs stb (flip off/on), comp=1..4", "[bmp][byte-diff][1x1][strict]") {
    const int w = 1, h = 1;

    for (int comp = 1; comp <= 4; ++comp) {
        auto pixels = make_pattern(w, h, comp);

        for (bool flip : {false, true}) {
            auto a = write_bmp_cpp(w, h, comp, pixels, flip);
            auto b = write_bmp_stb(w, h, comp, pixels, flip);

            REQUIRE(a == b);

            if (comp != 4) {
                const int pad = (-w * 3) & 3; // for w=1 => pad=1
                require_bmp_header_24(a, w, h, pad);
            }
            else {
                require_bmp_header_32_v4(a, w, h);
            }
        }
    }
}

TEST_CASE("BMP: negative dims return false (C++ writer)", "[bmp][invalid][strict]") {
    stbiw::Writer wr;
    std::vector<std::uint8_t> sink;
    wr.start_callbacks(&cb_const, &sink);

    std::vector<std::uint8_t> px(16, 0x7F);

    REQUIRE_FALSE(wr.write_bmp_core(-1, 1, 3, px.data()));
    REQUIRE_FALSE(wr.write_bmp_core(1, -1, 3, px.data()));
    REQUIRE(sink.empty());
}