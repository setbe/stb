#define STBTT_FREESTANDING
#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_image_write.h"

#include "stb_truetype.hpp"
#include <fstream>
#include <vector>
#include <iostream>

using namespace stb;

int main() {
    const char* font_path = "C:\\Windows\\Fonts\\arialbd.ttf";
    const char* output_path = "text.png";

    // 1. Load font file
    std::ifstream ifs(font_path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cerr << "Couldn't open font file: " << font_path << std::endl;
        return 1;
    }

    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> font_buffer(size);
    if (!ifs.read(reinterpret_cast<char*>(font_buffer.data()), size)) {
        std::cerr << "Error while reading font file." << std::endl;
        return 1;
    }

    // 2. Init TT
    TrueType tt;
    if (!tt.ReadBytes(font_buffer.data())) {
        std::cerr << "Error while font initialization." << std::endl;
        return 1;
    }

    // 3. Adjustments
    const float pixel_height = 64.0f;
    float scale = tt.ScaleForPixelHeight(pixel_height);
    int glyph = tt.FindGlyphIndex('A');  // letter 'A'

    Box glyph_box;
    if (!tt.GetGlyphBox(glyph, glyph_box)) {
        std::cerr << "Couldn't receive glyph box" << std::endl;
        return 1;
    }

    // Scale box to pixel space
    int width = int((glyph_box.x1 - glyph_box.x0) * scale);
    int height = int((glyph_box.y1 - glyph_box.y0) * scale);

    std::vector<unsigned char> bitmap(width * height, 0);

    // NOTE: shifts compensate for baseline alignment
    float shift_x = -glyph_box.x0 * scale;
    float shift_y = -glyph_box.y0 * scale;
    // 4. Make bitmap
    tt.MakeGlyphBitmap(bitmap.data(), glyph,
        width, height, width, scale, scale, shift_x, shift_y);

    // 5. Save bitmap as image via stb_image_write
    if (!stbi_write_png(output_path, width, height, 1, bitmap.data(), width)) {
        std::cerr << "Couldn't save PNG." << std::endl;
        return 1;
    }
    return 0;
}
