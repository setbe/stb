#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#include "../stb_truetype_stream/stb_truetype_stream.hpp"

extern "C" static inline int __cdecl _purecall(void) { ExitProcess(0xDEAD); }
extern "C" int _fltused = 0;

extern "C" void* __cdecl memset(void* dst, int c, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}
extern "C" void* __cdecl _memset(void* dst, int c, size_t n) { return memset(dst, c, n); }

void* __cdecl operator new(size_t sz) {
    if (!sz) sz = 1;
    void* p = VirtualAlloc(nullptr, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) ExitProcess(0xBAD0);
    return p;
}
void* __cdecl operator new[](size_t sz) { return operator new(sz); }
void __cdecl operator delete(void* p) noexcept { if (p) VirtualFree(p, 0, MEM_RELEASE); }
void __cdecl operator delete[](void* p) noexcept { operator delete(p); }
void __cdecl operator delete(void* p, unsigned int) noexcept { operator delete(p); }
void __cdecl operator delete[](void* p, unsigned int) noexcept { operator delete(p); }

// --- config ---
using u8 = unsigned char;
constexpr int width = 128;
constexpr int height = 128;
constexpr const char* font_ttf = "C:\\Windows\\Fonts\\arialbd.ttf";

static stbtt_stream::Font g_font;

static u8* pixels_sdf;   // 1 byte/px
static u8* pixels_msdf;  // 4 bytes/px (BGRA for DIB32)

static HWND g_hwnd_sdf = nullptr;
static HWND g_hwnd_msdf = nullptr;

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

static void render_codepoint_mode(int codepoint,
    stbtt_stream::DfMode mode,
    u8* out_pixels,
    uint32_t out_stride_bytes) {
    uint32_t cps[1] = { (uint32_t)codepoint };

    stbtt_stream::PlanInput in{};
    in.mode = mode;
    in.pixel_height = (uint16_t)height;
    in.spread_px = 6.0f;
    in.codepoints = cps;
    in.codepoint_count = 1;

    const size_t plan_bytes = g_font.PlanBytes(in);
    if (!plan_bytes) return;

    void* plan_mem = VirtualAlloc(nullptr, plan_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!plan_mem) fail();

    stbtt_stream::FontPlan plan{};
    if (!g_font.Plan(in, plan_mem, plan_bytes, plan)) {
        VirtualFree(plan_mem, 0, MEM_RELEASE);
        return;
    }

    if (plan.atlas_side > (uint16_t)width || plan.atlas_side > (uint16_t)height) {
        VirtualFree(plan_mem, 0, MEM_RELEASE);
        return;
    }

    // clear output buffer
    const uint32_t comp = (mode == stbtt_stream::DfMode::MSDF) ? 4u : 1u; // for our output (BGRA or Gray)
    memset(out_pixels, 0, (size_t)width * (size_t)height * comp);

    // Build writes:
    // - SDF: 1 byte/px
    // - MSDF: 3 bytes/px (RGB)
    //
    // Our MSDF buffer is BGRA (4 bytes/px), so we build into a temp RGB buffer,
    // then expand to BGRA for display.
    if (mode == stbtt_stream::DfMode::SDF) {
        if (!g_font.Build(plan, out_pixels, out_stride_bytes)) {
            // optional debug
        }
    }
    else {
        // temp RGB buffer (width*height*3)
        u8* tmp_rgb = (u8*)VirtualAlloc(nullptr, (size_t)width * (size_t)height * 3u,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!tmp_rgb) { VirtualFree(plan_mem, 0, MEM_RELEASE); return; }
        memset(tmp_rgb, 0, (size_t)width * (size_t)height * 3u);

        const uint32_t tmp_stride = (uint32_t)width * 3u;
        if (g_font.Build(plan, tmp_rgb, tmp_stride)) {
            // RGB -> BGRA
            for (int y = 0; y < height; ++y) {
                const u8* src = tmp_rgb + (size_t)y * (size_t)width * 3u;
                u8* dst = out_pixels + (size_t)y * (size_t)width * 4u;
                for (int x = 0; x < width; ++x) {
                    const u8 r = src[x * 3 + 0];
                    const u8 g = src[x * 3 + 1];
                    const u8 b = src[x * 3 + 2];
                    dst[x * 4 + 0] = b;
                    dst[x * 4 + 1] = g;
                    dst[x * 4 + 2] = r;
                    dst[x * 4 + 3] = 255;
                }
            }
        }

        VirtualFree(tmp_rgb, 0, MEM_RELEASE);
    }

    VirtualFree(plan_mem, 0, MEM_RELEASE);
}

static void paint_sdf(HDC dc) {
    RECT r{};
    GetClientRect(WindowFromDC(dc), &r);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

    BITMAPINFO* bmi = (BITMAPINFO*)VirtualAlloc(
        0, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
    );
    if (!bmi) return;

    // zero
    for (size_t i = 0; i < sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD); ++i)
        ((u8*)bmi)[i] = 0;

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = width;
    bmi->bmiHeader.biHeight = -height;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 8;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biClrUsed = 256;

    for (int i = 0; i < 256; i++) {
        bmi->bmiColors[i].rgbRed = (BYTE)i;
        bmi->bmiColors[i].rgbGreen = (BYTE)i;
        bmi->bmiColors[i].rgbBlue = (BYTE)i;
    }

    StretchDIBits(dc, 0, 0, width, height, 0, 0, width, height,
        pixels_sdf, bmi, DIB_RGB_COLORS, SRCCOPY);

    VirtualFree(bmi, 0, MEM_RELEASE);
}

static void paint_msdf(HDC dc) {
    RECT r{};
    GetClientRect(WindowFromDC(dc), &r);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

    BITMAPINFO bmi{};
    // manual zero (no CRT)
    u8* z = (u8*)&bmi;
    for (size_t i = 0; i < sizeof(bmi); ++i) z[i] = 0;

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;          // BGRA
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(dc, 0, 0, width, height, 0, 0, width, height,
        pixels_msdf, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static void render_both(int codepoint) {
    render_codepoint_mode(codepoint, stbtt_stream::DfMode::SDF,
        pixels_sdf, (uint32_t)width);

    render_codepoint_mode(codepoint, stbtt_stream::DfMode::MSDF,
        pixels_msdf, (uint32_t)width * 4u);

    if (g_hwnd_sdf)  InvalidateRect(g_hwnd_sdf, 0, TRUE);
    if (g_hwnd_msdf) InvalidateRect(g_hwnd_msdf, 0, TRUE);
}

LRESULT CALLBACK wnd_proc_sdf(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_sdf(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CHAR: {
        render_both((int)(wchar_t)w);
        return 0;
    }
    case WM_DESTROY: {
        // закриваємо обидва: коли одне закрили — виходимо
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, w, l);
}

LRESULT CALLBACK wnd_proc_msdf(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_msdf(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CHAR: {
        render_both((int)(wchar_t)w);
        return 0;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    pixels_sdf = (u8*)VirtualAlloc(nullptr, (size_t)width * (size_t)height,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    pixels_msdf = (u8*)VirtualAlloc(nullptr, (size_t)width * (size_t)height * 4u,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pixels_sdf || !pixels_msdf) fail();

    size_t sz = 0;
    uint8_t* font_data = load_font(font_ttf, &sz);
    if (!font_data) fail();
    if (!g_font.ReadBytes(font_data)) fail();

    // initial
    render_both(0x0416); // Ж

    WNDCLASSW wc1{};
    wc1.lpfnWndProc = wnd_proc_sdf;
    wc1.hInstance = h;
    wc1.lpszClassName = L"TTWinSDF";
    RegisterClassW(&wc1);

    WNDCLASSW wc2{};
    wc2.lpfnWndProc = wnd_proc_msdf;
    wc2.hInstance = h;
    wc2.lpszClassName = L"TTWinMSDF";
    RegisterClassW(&wc2);

    g_hwnd_sdf = CreateWindowW(L"TTWinSDF", L"SDF (8-bit)", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 420, 420, 0, 0, h, 0);

    g_hwnd_msdf = CreateWindowW(L"TTWinMSDF", L"MSDF (RGB)", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        560, 100, 420, 420, 0, 0, h, 0);

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    VirtualFree(font_data, 0, MEM_RELEASE);
    VirtualFree(pixels_sdf, 0, MEM_RELEASE);
    VirtualFree(pixels_msdf, 0, MEM_RELEASE);
    ExitProcess(0);
}
