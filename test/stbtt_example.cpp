#define STBTT_FREESTANDING
#include "../stb_truetype/stb_truetype.hpp"

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>

extern "C" int _fltused = 0;

extern "C" void* __cdecl memset(void* dest, int ch, size_t count) {
    unsigned char* p = static_cast<unsigned char*>(dest);
    while (count--) *p++ = static_cast<unsigned char>(ch);
    return dest;
}

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

// globals for painting
static HBITMAP h_bmp;
static unsigned char* pixels = nullptr;
static int w, h;
static float g_glyph_px = 16.f;

// render single glyph into memory bitmap
static void render_A(const char* font_path) {
    size_t sz;
    uint8_t* font = load_font(font_path, &sz);
    if (!font) return;

    stbtt::Font tt;
    if (!tt.ReadBytes(font)) return;

    float scale = tt.ScaleForPixelHeight(g_glyph_px);
    int glyph = tt.FindGlyphIndex('a');

    stbtt::Box box;
    if (!tt.GetGlyphBox(glyph, box)) return;

    w = (int)((box.x1 - box.x0) * scale);
    h = (int)((box.y1 - box.y0) * scale);
    int stride = (w + 3) & ~3;

    if (pixels) VirtualFree(pixels, 0, MEM_RELEASE);
    pixels = (unsigned char*)VirtualAlloc(0, stride * h, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    float shift_x = -box.x0 * scale;
    float shift_y = -box.y0 * scale;

    tt.MakeGlyphBitmap(pixels, glyph, w, h, stride, scale, scale, shift_x, shift_y);
    VirtualFree(font, 0, MEM_RELEASE);
}

// GDI paint
static void paint(HDC dc) {
    if (!pixels) return;

    RECT r;
    GetClientRect(WindowFromDC(dc), &r);
    HBRUSH brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(dc, &r, brush);
    
    // Allocate enough memory for header + 256 colors
    const SIZE_T bmi_size = sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256;
    auto* bmi = (BITMAPINFO*)VirtualAlloc(nullptr, bmi_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!bmi) return;

    ZeroMemory(bmi, bmi_size);
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = w;
    bmi->bmiHeader.biHeight = -h;
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 8;
    bmi->bmiHeader.biCompression = BI_RGB;
    bmi->bmiHeader.biClrUsed = 256;

    for (int i = 0; i < 256; ++i)
        bmi->bmiColors[i].rgbRed = bmi->bmiColors[i].rgbGreen = bmi->bmiColors[i].rgbBlue = (BYTE)i;

    StretchDIBits(dc, 0, 0, w, h, 0, 0, w, h, pixels, bmi, DIB_RGB_COLORS, SRCCOPY);

    VirtualFree(bmi, 0, MEM_RELEASE);
}



// window proc
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_SIZE:
        g_glyph_px = LOWORD(l); // window width
        render_A("C:\\Windows\\Fonts\\arialbd.ttf"); // load .ttf file again and render A
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
        // fallthrough
    case WM_PAINT:
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(hwnd, msg, w, l);
}

# ifndef CONSOLE
// entry
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    render_A("C:\\Windows\\Fonts\\arialbd.ttf");

    WNDCLASSA wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h;
    wc.lpszClassName = "TTWin";
    RegisterClassA(&wc);

    HWND win = CreateWindowA("TTWin", "Freestanding Glyph A",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 400, 400,
        0, 0, h, 0);

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
#else // defined CONSOLE
# error "This test runs in NoConsole mode only"
#endif

#endif // ifdef _WIN32