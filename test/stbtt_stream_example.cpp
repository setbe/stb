#include <windows.h>
#include <stdint.h>
#include <stddef.h>

#include "../stb_truetype_stream/stb_truetype_stream.hpp"

extern "C" static inline int __cdecl _purecall(void) {
    ExitProcess(0xDEAD);
}

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

// MSVC sized-delete variants (your error is exactly this one)
void __cdecl operator delete(void* p, unsigned int) noexcept { operator delete(p); }
void __cdecl operator delete[](void* p, unsigned int) noexcept { operator delete(p); }

using u8 = unsigned char;

// --- configuration ---
constexpr int width = 64;
constexpr int height = 64;
// constexpr const char* font_ttf = "C:/Users/cnota/Desktop/test_stb/Inkfree.ttf";
constexpr const char* font_ttf = "C:\\Windows\\Fonts\\arialbd.ttf";

// --- globals ---
static u8 pixels[width * height]; // final 8-bit bitmap
stbtt_stream::Font g_font;

// --- helpers ---
void fail() { ExitProcess(1337); }

// simple file loader (no CRT)
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

// render single codepoint into memory bitmap
static void render_codepoint(const char* font_path, int codepoint = 0x0416) {
    size_t sz;
    uint8_t* font_data = load_font(font_path, &sz);
    if (!font_data) fail();

    if (!g_font.ReadBytes(font_data)) {
        VirtualFree(font_data, 0, MEM_RELEASE);
        fail();
    }

    const int glyph = g_font.FindGlyphIndex(codepoint);
    if (glyph == 0) {
        VirtualFree(font_data, 0, MEM_RELEASE);
        return;
    }

    const float scale = g_font.ScaleForPixelHeight((float)height);
    const float spread = 8.f / scale;

    // --- render SDF into pixels ---
    memset(pixels, 0, width * height);
    g_font.MakeGlyphSDF(
        glyph,
        pixels,    // atlas buffer
        width,     // stride
        0, 0,     // shift x,y
        width,    // cell size
        scale,
        spread
    );

    VirtualFree(font_data, 0, MEM_RELEASE);
}

// GDI paint
static void paint(HDC dc) {
    RECT r{};
    GetClientRect(WindowFromDC(dc), &r);
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));

    BITMAPINFO* bmi = (BITMAPINFO*)VirtualAlloc(
        0,
        sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    ZeroMemory(bmi, sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));

    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = width;
    bmi->bmiHeader.biHeight = -height;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 8;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biClrUsed = 256;

    for (int i = 0; i < 256; i++)
        bmi->bmiColors[i].rgbRed = bmi->bmiColors[i].rgbGreen = bmi->bmiColors[i].rgbBlue = (BYTE)i;

    StretchDIBits(
        dc,
        0, 0, width, height, // dest
        0, 0, width, height, // src
        pixels,
        bmi,
        DIB_RGB_COLORS,
        SRCCOPY
    );

    VirtualFree(bmi, 0, MEM_RELEASE);
}

// window proc
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CHAR: {
        wchar_t wc = (wchar_t)w;
        render_codepoint(font_ttf, wc);
        InvalidateRect(hwnd, 0, TRUE);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, w, l);
}

// entry
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    render_codepoint(font_ttf, 0x0416); // Ж

    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h;
    wc.lpszClassName = L"TTWin";
    RegisterClassW(&wc);

    HWND win = CreateWindowW(
        L"TTWin",
        L"Freestanding Glyph Ж",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 400,
        0, 0, h, 0
    );

    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ExitProcess(0);
}