#define STBTT_FREESTANDING
#include <windows.h>
#include "stb_truetype.hpp"
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
static unsigned char* pixels;
static int w, h;

// render single glyph into memory bitmap
static void render_A(const char* font_path) {
    size_t sz;
    uint8_t* font = load_font(font_path, &sz);
    if (!font) return;

    stb::TrueType tt;
    if (!tt.ReadBytes(font)) return;

    float scale = tt.ScaleForPixelHeight(32.0f);
    int glyph = tt.FindGlyphIndex('A');

    stb::Box box;
    if (!tt.GetGlyphBox(glyph, box)) return;

    w = (int)((box.x1 - box.x0) * scale);
    h = (int)((box.y1 - box.y0) * scale);
    pixels = (unsigned char*)VirtualAlloc(0, w * h, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    float shift_x = -box.x0 * scale;
    float shift_y = -box.y0 * scale;

    tt.MakeGlyphBitmap(pixels, glyph, w, h, w, scale, scale, shift_x, shift_y);
}

// GDI paint
static void paint(HDC dc) {
    if (!pixels) return;

    // Allocate enough memory for header + 256 colors
    BITMAPINFO* bmi = (BITMAPINFO*)_alloca(sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256);
    ZeroMemory(bmi, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256);

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
}


// window proc
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(hwnd, msg, w, l);
}

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
    return 0;
    exit(0);
}
