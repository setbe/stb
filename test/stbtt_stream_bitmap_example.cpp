#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "../stb_truetype_stream/stb_truetype_stream.hpp"
#include "../stb_image_write/stb_image_write.h"

using u8 = unsigned char;
using u32 = uint32_t;

// --- globals ---
static u32 glyphs_ok = 0;
static u32 glyphs_failed = 0;

// --- simple file loader ---
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

// --- callback for StreamSDF ---
static void glyph_callback(uint32_t codepoint, bool ok) {
    if (ok) ++glyphs_ok;
    else ++glyphs_failed;
}

// --- helper: write PNG using stbi_write_png_to_func ---
struct Win32File {
    HANDLE h;
    bool ok;
};

static void stbi_write_callback(void* context, void* data, int size) {
    Win32File* f = (Win32File*)context;
    if (!f->ok) return;

    DWORD written = 0;
    if (!WriteFile(f->h, data, (DWORD)size, &written, nullptr) || written != (DWORD)size)
        f->ok = false;
}

static bool save_sdf_png(const wchar_t* path, const unsigned char* atlas, int atlas_size_px) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    Win32File f{ h, true };

    stbi_flip_vertically_on_write(false);
    stbi_write_png_to_func(
        stbi_write_callback,
        &f,
        atlas_size_px,
        atlas_size_px,
        1,          // comp
        atlas,
        atlas_size_px  // stride
    );

    CloseHandle(h);
    return f.ok;
}

// --- helper: generate atlas ---
template<typename... Scripts>
static bool generate_atlas(stbtt_stream::Font& font,
    const wchar_t* filename,
    uint8_t cell_size,
    uint8_t padding,
    float spread,
    Scripts... scripts) {
    glyphs_ok = glyphs_failed = 0;

    auto info = font.AtlasStream(cell_size, padding, scripts...);
    size_t atlas_bytes = (size_t)info.width * (size_t)info.width;
    u8* atlas = (u8*)VirtualAlloc(nullptr, atlas_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!atlas) return false;

    memset(atlas, 0, atlas_bytes);

    font.StreamSDF(atlas, spread, info.width, cell_size, padding, &glyph_callback, scripts...);

    bool ok = save_sdf_png(filename, atlas, info.width);

    VirtualFree(atlas, 0, MEM_RELEASE);

    wchar_t msg[256];
    wsprintfW(msg, L"Wrote %s\nGlyphs OK: %u, Failed: %u", filename, glyphs_ok, glyphs_failed);
    MessageBoxW(NULL, msg, L"SDF Atlas", MB_OK);

    return ok;
}

// --- main ---
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    const uint8_t cell_size = 64;
    const uint8_t padding = 2;
    const float spread = 8.f;

    // ----------------- 1) Latin + Cyrillic + ... -----------------
    {
        size_t sz;
        uint8_t* data = load_font("C:\\Windows\\Fonts\\arialbd.ttf", &sz);
        if (!data) { MessageBoxW(nullptr, L"Failed to load font", L"Error", MB_ICONERROR); return 1; }

        stbtt_stream::Font font;
        font.ReadBytes(data);

        generate_atlas(font, L"sdf_atlas_1.png", cell_size, padding, spread,
            ::stbtt_codepoints::Script::Latin,
            ::stbtt_codepoints::Script::Cyrillic,
            ::stbtt_codepoints::Script::Greek,
            ::stbtt_codepoints::Script::Arabic,
            ::stbtt_codepoints::Script::Hebrew,
            ::stbtt_codepoints::Script::Devanagari);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    // ----------------- 2) Japanese -----------------
    {
        size_t sz;
        uint8_t* data = load_font("C:\\Users\\cnota\\Desktop\\test_stb\\Gen_Jyuu_Gothic_Monospace_Bold.ttf", &sz);
        if (!data) { MessageBoxW(nullptr, L"Failed to load font", L"Error", MB_ICONERROR); return 1; }

        stbtt_stream::Font font;
        font.ReadBytes(data);

        generate_atlas(font, L"sdf_atlas_2.png", cell_size, padding, spread,
            ::stbtt_codepoints::Script::Kana,
            ::stbtt_codepoints::Script::JouyouKanji);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    // ----------------- 3) Chinese -----------------
    {
        size_t sz;
        uint8_t* data = load_font("C:\\Users\\cnota\\Desktop\\test_stb\\D2CodingBold.ttf", &sz);
        if (!data) { MessageBoxW(nullptr, L"Failed to load font", L"Error", MB_ICONERROR); return 1; }

        stbtt_stream::Font font;
        font.ReadBytes(data);

        generate_atlas(font, L"sdf_atlas_3.png", cell_size, padding, spread,
            ::stbtt_codepoints::Script::CJK);

        VirtualFree(data, 0, MEM_RELEASE);
    }

    return 0;
}
