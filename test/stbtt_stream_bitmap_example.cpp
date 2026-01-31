#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb_image_write/stb_image_write.h"

#include "../stb_truetype_stream/stb_truetype_stream.hpp"
#include "../stb_truetype_stream/codepoints/stbtt_codepoints_stream.hpp"

using u8 = unsigned char;
using u32 = uint32_t;

// --- loader ---
static uint8_t* load_font(const char* path, size_t* out_size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(f, nullptr);
    uint8_t* data = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD readed = 0;
    ReadFile(f, data, size, &readed, nullptr);
    CloseHandle(f);
    *out_size = readed;
    return data;
}

struct Win32File { HANDLE h; bool ok; };

static void stbi_write_callback(void* context, void* data, int size) {
    Win32File* f = (Win32File*)context;
    if (!f->ok) return;
    DWORD written = 0;
    if (!WriteFile(f->h, data, (DWORD)size, &written, nullptr) || written != (DWORD)size)
        f->ok = false;
}

static bool save_sdf_png(const wchar_t* path, const unsigned char* atlas, int side) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    Win32File f{ h, true };
    stbi_flip_vertically_on_write(false);
    stbi_write_png_to_func(stbi_write_callback, &f, side, side, 1, atlas, side);

    CloseHandle(h);
    return f.ok;
}

template<typename... Scripts>
static bool generate_atlas_planned(stbtt_stream::Font& font,
    const wchar_t* out_png,
    float pixel_height,
    float spread_px,
    uint16_t max_xs,
    Scripts... scripts)
{
    // --------- 0) collect codepoints using stbtt_codepoints (optional module) ---------
    const u32 count = stbtt_codepoints::PlanGlyphs(font, scripts...);
    if (!count) return false;

    u32* codepoints = (u32*)VirtualAlloc(nullptr, (size_t)count * sizeof(u32),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!codepoints) return false;

    u32 at = 0;
    auto sink = [&](u32 cp, int /*glyph*/) {
        if (at < count) codepoints[at++] = cp;
    };

    // expand scripts manually (no STL)
    int dummy[] = { (stbtt_codepoints::BuildGlyphs(font, scripts, sink), 0)... };
    (void)dummy;

    // --------- 1) PASS 1: PlanSDF ---------
    stbtt_stream::FontPlan plan{};
    plan.mode = stbtt_stream::DfMode::SDF;
    plan.cell_size = (uint8_t)pixel_height;         // informational
    plan.scale = font.ScaleForPixelHeight(pixel_height);
    plan.spread = spread_px / plan.scale;           // font units

    const u32 glyph_cap = at;

    stbtt_stream::GlyphPlan* glyphs = (stbtt_stream::GlyphPlan*)VirtualAlloc(
        nullptr, (size_t)glyph_cap * sizeof(stbtt_stream::GlyphPlan),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!glyphs) { VirtualFree(codepoints, 0, MEM_RELEASE); return false; }

    const u32 node_cap = 2 * glyph_cap + 16;
    stbtt_stream::SkylineNode* nodes = (stbtt_stream::SkylineNode*)VirtualAlloc(
        nullptr, (size_t)node_cap * sizeof(stbtt_stream::SkylineNode),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!nodes) {
        VirtualFree(glyphs, 0, MEM_RELEASE);
        VirtualFree(codepoints, 0, MEM_RELEASE);
        return false;
    }

    stbtt_stream::PlanResult pr = font.PlanSDF(
        plan,
        codepoints, glyph_cap,
        glyphs, glyph_cap,
        nodes, (int)node_cap,
        max_xs
    );

    VirtualFree(nodes, 0, MEM_RELEASE);
    VirtualFree(codepoints, 0, MEM_RELEASE);

    if (!pr.ok || pr.planned == 0) {
        VirtualFree(glyphs, 0, MEM_RELEASE);
        return false;
    }

    // IMPORTANT: plan.glyphs must point to glyphs for BuildSDF
    plan.glyphs = glyphs;
    plan.glyph_count = pr.planned;
    plan.max_points = pr.max_points;
    plan.max_area = pr.max_area;
    plan.max_xs = max_xs;
    plan.atlas_side = pr.atlas_side;

    // --------- 2) allocate atlas ---------
    const u32 side = plan.atlas_side;
    const size_t atlas_bytes = (size_t)side * (size_t)side;
    u8* atlas = (u8*)VirtualAlloc(nullptr, atlas_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!atlas) { VirtualFree(glyphs, 0, MEM_RELEASE); return false; }
    // zero
    for (size_t i = 0; i < atlas_bytes; ++i) atlas[i] = 0;

    // --------- 3) allocate scratch ---------
    const size_t scratch_bytes = stbtt_stream::glyph_scratch_bytes(
        plan.max_points, plan.max_area, plan.max_xs, plan.mode);

    void* scratch = VirtualAlloc(nullptr, scratch_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!scratch) {
        VirtualFree(atlas, 0, MEM_RELEASE);
        VirtualFree(glyphs, 0, MEM_RELEASE);
        return false;
    }

    // --------- 4) PASS 2: BuildSDF ---------
    const bool ok_build = font.BuildSDF(plan, atlas, side, scratch, scratch_bytes);

    // --------- 5) write png ---------
    bool ok = ok_build && save_sdf_png(out_png, atlas, (int)side);

    VirtualFree(scratch, 0, MEM_RELEASE);
    VirtualFree(atlas, 0, MEM_RELEASE);
    VirtualFree(glyphs, 0, MEM_RELEASE);

    return ok;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const float pixel_height = 24.f;
    const float spread_px = 4.f;
    const uint16_t max_xs = 256;

    // 1) Latin+Cyrillic+...
    {
        size_t sz;
        uint8_t* data = load_font("C:\\Windows\\Fonts\\arialbd.ttf", &sz);
        if (!data) return 1;

        stbtt_stream::Font font;
        if (!font.ReadBytes(data)) return 1;

        generate_atlas_planned(font, L"sdf_atlas_1.png", pixel_height, spread_px, max_xs,
            stbtt_codepoints::Script::Latin,
            stbtt_codepoints::Script::Cyrillic,
            stbtt_codepoints::Script::Greek,
            stbtt_codepoints::Script::Arabic,
            stbtt_codepoints::Script::Hebrew,
            stbtt_codepoints::Script::Devanagari);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    // 2) Japanese
    {
        size_t sz;
        uint8_t* data = load_font("C:\\Users\\cnota\\Desktop\\test_stb\\Gen_Jyuu_Gothic_Monospace_Bold.ttf", &sz);
        if (!data) return 1;

        stbtt_stream::Font font;
        if (!font.ReadBytes(data)) return 1;

        generate_atlas_planned(font, L"sdf_atlas_2.png", pixel_height, spread_px, max_xs,
            stbtt_codepoints::Script::Kana,
            stbtt_codepoints::Script::JouyouKanji);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    // 3) Chinese
    {
        size_t sz;
        uint8_t* data = load_font("C:\\Users\\cnota\\Desktop\\test_stb\\D2CodingBold.ttf", &sz);
        if (!data) return 1;

        stbtt_stream::Font font;
        if (!font.ReadBytes(data)) return 1;

        generate_atlas_planned(font, L"sdf_atlas_3.png", pixel_height, spread_px, max_xs,
            stbtt_codepoints::Script::CJK);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    MessageBoxW(nullptr, L"Done", L"SDF", MB_OK);
    return 0;
}
