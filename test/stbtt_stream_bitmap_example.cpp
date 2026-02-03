#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../stb_image_write/stb_image_write.h"

#include "../stb_truetype_stream/stb_truetype_stream.hpp"
#include "../stb_truetype_stream/codepoints/stbtt_codepoints_stream.hpp"

// Latin, Cyrillic, Greek, Arabic, Hebrew, Devanagari
#define FONT_MINIMAL "C:\\Users\\cnota\\Desktop\\test_stb\\arialbd.ttf"
// Japanese
#define FONT_JAPANESE "C:\\Users\\cnota\\Desktop\\test_stb\\Gen_Jyuu_Gothic_Monospace_Bold.ttf"
// CJK
#define FONT_CJK "C:\\Users\\cnota\\Desktop\\test_stb\\D2CodingBold.ttf"

constexpr float PIXEL_HEIGHT = 32.f;
constexpr float SPREAD_PX = 4.f;

using u8 = unsigned char;
using u32 = uint32_t;

// --- loader ---
static uint8_t* load_font(const char* path, size_t* out_size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    DWORD size = GetFileSize(f, nullptr);
    uint8_t* data = (uint8_t*)VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!data) { CloseHandle(f); return nullptr; }
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

static bool save_png(const wchar_t* path,
                     const unsigned char* pixels,
                     int w, int h,
                     int comp,
                     int stride_bytes) {
    HANDLE hfile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hfile == INVALID_HANDLE_VALUE) {
        wchar_t msg[256];
        wsprintfW(msg, L"CreateFileW failed\n%s", path);
        MessageBoxW(nullptr, msg, L"save_png", MB_ICONERROR);
        return false;
    }

    Win32File f{ hfile, true };
    stbi_flip_vertically_on_write(false);
    stbi_write_png_to_func(stbi_write_callback, &f, w, h, comp, pixels, stride_bytes);

    CloseHandle(hfile);
    if (!f.ok) {
        MessageBoxW(nullptr, L"WriteFile failed during PNG write", L"save_png", MB_ICONERROR);
    }
    return f.ok;
}

template<typename... Scripts>
static bool generate_atlas_planned(stbtt_stream::Font& font,
                                   const wchar_t* out_png,
                                   stbtt_stream::DfMode mode,
                                   float pixel_height,
                                   float spread_px,
                                   Scripts... scripts) {
    // --------- 0) collect codepoints ----------
    const u32 count = stbtt_codepoints::PlanGlyphs(font, scripts...);
    if (!count) return false;

    u32* codepoints = (u32*)VirtualAlloc(nullptr, (size_t)count * sizeof(u32),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!codepoints) return false;

    u32 at = 0;
    auto sink = [&](u32 cp, int /*glyph*/) {
        if (at < count) codepoints[at++] = cp;
    };

    int dummy[] = { (stbtt_codepoints::BuildGlyphs(font, scripts, sink), 0)... };
    (void)dummy;

    // --------- 1) PlanBytes ----------
    stbtt_stream::PlanInput in{};
    in.mode = mode;
    in.pixel_height = (uint16_t)pixel_height;
    in.spread_px = spread_px;
    in.codepoints = codepoints;
    in.codepoint_count = at;

    const size_t plan_bytes = font.PlanBytes(in);
    if (!plan_bytes) { VirtualFree(codepoints, 0, MEM_RELEASE); return false; }

    void* plan_mem = VirtualAlloc(nullptr, plan_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!plan_mem) { VirtualFree(codepoints, 0, MEM_RELEASE); return false; }

    // --------- 2) Plan ----------
    stbtt_stream::FontPlan plan{};
    if (!font.Plan(in, plan_mem, plan_bytes, plan)) {
        VirtualFree(plan_mem, 0, MEM_RELEASE);
        VirtualFree(codepoints, 0, MEM_RELEASE);
        return false;
    }

    VirtualFree(codepoints, 0, MEM_RELEASE);

    // --------- 3) allocate atlas ----------
    const u32 side = plan.atlas_side;
    const u32 comp = plan.mode==stbtt_stream::DfMode::SDF ? 1u :
                     plan.mode==stbtt_stream::DfMode::MSDF ? 3u : 4u;
    const u32 stride_bytes = side * comp;
    const size_t atlas_bytes = (size_t)side * (size_t)side * (size_t)comp;

    u8* atlas = (u8*)VirtualAlloc(nullptr, atlas_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!atlas) {
        VirtualFree(plan_mem, 0, MEM_RELEASE);
        return false;
    }
    for (size_t i = 0; i < atlas_bytes; ++i) atlas[i] = 0;

    // --------- 4) Build ----------
    const bool ok_build = font.Build(plan, atlas, stride_bytes);
    if (!ok_build)
        MessageBoxW(nullptr, L"Build failed", L"Error", MB_ICONERROR);

    // --------- 5) write png ----------
    bool ok = ok_build && save_png(out_png, atlas, (int)side, (int)side, (int)comp, (int)stride_bytes);

    VirtualFree(atlas, 0, MEM_RELEASE);
    VirtualFree(plan_mem, 0, MEM_RELEASE);
    return ok;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const float pixel_height = PIXEL_HEIGHT;
    const float spread_px = SPREAD_PX;

    // 1) Latin+Cyrillic+...
    {
        size_t sz;
        uint8_t* data = load_font(FONT_MINIMAL, &sz);
        if (!data) return 1;

        stbtt_stream::Font font;
        if (!font.ReadBytes(data)) return 1;

        generate_atlas_planned(font, L"sdf_atlas_1.png", stbtt_stream::DfMode::SDF, pixel_height, spread_px,
            stbtt_codepoints::Script::Latin,
            stbtt_codepoints::Script::Cyrillic,
            stbtt_codepoints::Script::Greek,
            stbtt_codepoints::Script::Arabic,
            stbtt_codepoints::Script::Hebrew,
            stbtt_codepoints::Script::Devanagari);

        generate_atlas_planned(font, L"msdf_atlas_1.png", stbtt_stream::DfMode::MSDF, pixel_height, spread_px,
            stbtt_codepoints::Script::Latin,
            stbtt_codepoints::Script::Cyrillic,
            stbtt_codepoints::Script::Greek,
            stbtt_codepoints::Script::Arabic,
            stbtt_codepoints::Script::Hebrew,
            stbtt_codepoints::Script::Devanagari);

        generate_atlas_planned(font, L"mtsdf_atlas_1.png", stbtt_stream::DfMode::MTSDF, pixel_height, spread_px,
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
        uint8_t* data = load_font(FONT_JAPANESE, &sz);
        if (!data) return 1;

        stbtt_stream::Font font;
        if (!font.ReadBytes(data)) return 1;

        generate_atlas_planned(font, L"sdf_atlas_2.png", stbtt_stream::DfMode::SDF, pixel_height, spread_px,
            stbtt_codepoints::Script::Kana,
            stbtt_codepoints::Script::JouyouKanji);

        generate_atlas_planned(font, L"msdf_atlas_2.png", stbtt_stream::DfMode::MSDF, pixel_height, spread_px,
            stbtt_codepoints::Script::Kana,
            stbtt_codepoints::Script::JouyouKanji);

        generate_atlas_planned(font, L"mtsdf_atlas_2.png", stbtt_stream::DfMode::MTSDF, pixel_height, spread_px,
            stbtt_codepoints::Script::Kana,
            stbtt_codepoints::Script::JouyouKanji);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    // 3) CJK
    {
        size_t sz;
        uint8_t* data = load_font(FONT_CJK, &sz);
        if (!data) return 1;

        stbtt_stream::Font font;
        if (!font.ReadBytes(data)) return 1;

        generate_atlas_planned(font, L"sdf_atlas_3.png", stbtt_stream::DfMode::SDF, pixel_height, spread_px,
            stbtt_codepoints::Script::CJK);

        generate_atlas_planned(font, L"msdf_atlas_3.png", stbtt_stream::DfMode::MSDF, pixel_height, spread_px,
            stbtt_codepoints::Script::CJK);

        generate_atlas_planned(font, L"mtsdf_atlas_3.png", stbtt_stream::DfMode::MTSDF, pixel_height, spread_px,
            stbtt_codepoints::Script::CJK);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    MessageBoxW(nullptr, L"Done (SDF + MSDF)", L"DF", MB_OK);
    return 0;
}
