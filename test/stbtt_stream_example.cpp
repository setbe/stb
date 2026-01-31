#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#include "../stb_truetype_stream/stb_truetype_stream.hpp"

extern "C" static inline int __cdecl _purecall(void) { ExitProcess(0xDEAD); }
// x86 floating-point helper symbol (needed if you use float/double on x86)
extern "C" int _fltused = 0;

// ---- C intrinsics replacements (provide both names for x86) ----
extern "C" void* __cdecl memset(void* dst, int c, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}
extern "C" void* __cdecl _memset(void* dst, int c, size_t n) { // x86 name
    return memset(dst, c, n);
}

// ---- minimal global new/delete ----
void* __cdecl operator new(size_t sz) {
    if (!sz) sz = 1;
    void* p = VirtualAlloc(nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) ExitProcess(0xBAD0);
    return p;
}
void* __cdecl operator new[](size_t sz) { return operator new(sz); }

void __cdecl operator delete(void* p) noexcept {
    if (p) VirtualFree(p, 0, MEM_RELEASE);
}
void __cdecl operator delete[](void* p) noexcept { operator delete(p); }
void __cdecl operator delete(void* p, unsigned int) noexcept { operator delete(p); }
void __cdecl operator delete[](void* p, unsigned int) noexcept { operator delete(p); }


// --- config ---
using u8 = unsigned char;
constexpr int width = 64;
constexpr int height = 64;
constexpr const char* font_ttf = "C:\\Windows\\Fonts\\arialbd.ttf";

// --- globals ---
static u8 pixels[width * height];
static stbtt_stream::Font g_font;

static void fail() { ExitProcess(1337); }

static uint8_t* load_font(const char* path, size_t* out_size) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(f, 0);
    uint8_t* data = (uint8_t*)VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD readed = 0;
    ReadFile(f, data, size, &readed, 0);
    CloseHandle(f);
    *out_size = readed;
    return data;
}

static void render_codepoint(int codepoint) {
    const int glyph = g_font.FindGlyphIndex(codepoint);
    if (glyph <= 0) return;

    stbtt_stream::GlyphPlanInfo gpi{};
    if (!g_font.GetGlyphPlanInfo(glyph, gpi) || gpi.is_empty) return;

    // fixed output bitmap (NOT an atlas, just a 1-cell image)
    stbtt_stream::GlyphPlan gp{};
    gp.codepoint = (uint32_t)codepoint;
    gp.glyph_index = (uint16_t)glyph;
    gp.x_min = gpi.x_min; gp.y_min = gpi.y_min;
    gp.x_max = gpi.x_max; gp.y_max = gpi.y_max;
    gp.num_points = gpi.max_points_in_tree;

    gp.rect.x = 0; gp.rect.y = 0;
    gp.rect.w = (uint16_t)width;
    gp.rect.h = (uint16_t)height;

    const float scale = g_font.ScaleForPixelHeight((float)height);
    const float spread_px = 8.0f;
    const float spread_fu = spread_px / scale; // spread in font units

    const uint16_t max_points = gp.num_points;
    const uint32_t max_area = (uint32_t)width * (uint32_t)height;
    const uint16_t max_xs = 256;

    const size_t scratch_bytes = stbtt_stream::glyph_scratch_bytes(
        max_points, max_area, max_xs, stbtt_stream::DfMode::SDF);

    void* scratch_mem = VirtualAlloc(nullptr, scratch_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!scratch_mem) fail();

    stbtt_stream::GlyphScratch scratch = stbtt_stream::bind_glyph_scratch(
        scratch_mem, max_points, max_area, max_xs, stbtt_stream::DfMode::SDF);

    memset(pixels, 0, sizeof(pixels));

    // 1 glyph streaming
    g_font.StreamSDF(gp,
        pixels, (uint32_t)width,
        scale, spread_fu,
        scratch,
        max_points, max_area, max_xs
    );

    VirtualFree(scratch_mem, 0, MEM_RELEASE);
}

static void paint(HDC dc) {
    RECT r{};
    GetClientRect(WindowFromDC(dc), &r);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

    BITMAPINFO* bmi = (BITMAPINFO*)VirtualAlloc(
        0, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
    );
    if (!bmi) return;

    // no CRT: manual zero
    for (size_t i = 0; i < sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD); ++i)
        ((u8*)bmi)[i] = 0;

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = width;
    bmi->bmiHeader.biHeight = -height;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 8;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biClrUsed = 256;

    for (int i = 0; i < 256; i++)
        bmi->bmiColors[i].rgbRed =
        bmi->bmiColors[i].rgbGreen =
        bmi->bmiColors[i].rgbBlue = (BYTE)i;

    StretchDIBits(dc, 0, 0, width, height, 0, 0, width, height,
        pixels, bmi, DIB_RGB_COLORS, SRCCOPY);

    VirtualFree(bmi, 0, MEM_RELEASE);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CHAR: {
        wchar_t wc = (wchar_t)w;
        render_codepoint((int)wc);
        InvalidateRect(hwnd, 0, TRUE);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    size_t sz = 0;
    uint8_t* font_data = load_font(font_ttf, &sz);
    if (!font_data) fail();
    if (!g_font.ReadBytes(font_data)) fail();

    render_codepoint(0x0416); // Ж

    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h;
    wc.lpszClassName = L"TTWin";
    RegisterClassW(&wc);

    CreateWindowW(L"TTWin", L"1-glyph StreamSDF", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 400, 0, 0, h, 0);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    VirtualFree(font_data, 0, MEM_RELEASE);
    ExitProcess(0);
}