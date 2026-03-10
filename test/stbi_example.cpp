#include <stddef.h>
#include <stdint.h>

#include "../stb_image/stb_image.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if !defined(IO_HAS_STD)
extern "C" int _fltused = 0;
#pragma comment(linker, "/alternatename:__chkstk=___chkstk")
#pragma comment(linker, "/alternatename:__lrotl=___lrotl")

namespace {
struct MiniHeapHdr {
    size_t total{};
};
} // namespace

#if defined(_M_IX86)
extern "C" __declspec(naked) void __cdecl __chkstk(void) {
    __asm {
        push ecx
        lea ecx, [esp + 8]
        cmp eax, 1000h
        jb chk_last
    chk_probe:
        sub ecx, 1000h
        test dword ptr [ecx], eax
        sub eax, 1000h
        cmp eax, 1000h
        jae chk_probe
    chk_last:
        sub ecx, eax
        test dword ptr [ecx], eax
        mov eax, esp
        mov esp, ecx
        mov ecx, [eax]
        mov eax, [eax + 4]
        push eax
        ret
    }
}
#else
extern "C" void __cdecl __chkstk(void) {}
#endif

extern "C" void* __cdecl memset(void* dst, int v, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)v;
    return dst;
}

extern "C" void* __cdecl memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

extern "C" int __cdecl strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        ++a;
        ++b;
    }
    return (int)((unsigned char)*a - (unsigned char)*b);
}

extern "C" int __cdecl strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) {
        ++a;
        ++b;
        --n;
    }
    if (n == 0) return 0;
    return (int)((unsigned char)*a - (unsigned char)*b);
}

extern "C" int __cdecl abs(int x) {
    return x < 0 ? -x : x;
}

extern "C" unsigned long __cdecl __lrotl(unsigned long value, int shift) {
    const unsigned int s = (unsigned int)(shift & 31);
    return (value << s) | (value >> ((32u - s) & 31u));
}

extern "C" double __cdecl ldexp(double x, int exp) {
    while (exp > 0) {
        x *= 2.0;
        --exp;
    }
    while (exp < 0) {
        x *= 0.5;
        ++exp;
    }
    return x;
}

extern "C" double __cdecl pow(double a, double b) {
    if (a <= 0.0) return 0.0;

    const long bi = (long)b;
    if ((double)bi == b) {
        double base = a;
        unsigned long e = (unsigned long)(bi < 0 ? -bi : bi);
        double r = 1.0;
        while (e) {
            if (e & 1u) r *= base;
            base *= base;
            e >>= 1u;
        }
        return bi < 0 ? 1.0 / r : r;
    }

    union {
        double d;
        unsigned long long i;
    } u{};
    u.d = a;
    int exp2 = (int)((u.i >> 52) & 0x7ffu) - 1023;
    u.i = (u.i & ((1ull << 52) - 1ull)) | (1023ull << 52);
    const double m = u.d;
    const double log2a = (double)exp2 + (m - 1.0);
    const double y = b * log2a;
    const int yi = (int)y;
    const double yf = y - (double)yi;
    const double ln2 = 0.6931471805599453;
    const double p = 1.0 + ln2 * yf + 0.2402265069591007 * yf * yf;
    return ldexp(p, yi);
}

extern "C" long __cdecl strtol(const char* s, char** endptr, int base) {
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') ++p;

    int sign = 1;
    if (*p == '+') ++p;
    else if (*p == '-') { sign = -1; ++p; }

    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') { base = 8; ++p; }
        else base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    long value = 0;
    const char* start_digits = p;
    for (;;) {
        int digit = -1;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;

        if (digit >= base) break;
        value = value * base + digit;
        ++p;
    }

    if (endptr) *endptr = (char*)(p == start_digits ? s : p);
    return sign < 0 ? -value : value;
}

extern "C" void* __cdecl malloc(size_t sz) {
    if (sz == 0) sz = 1;
    const size_t total = sz + sizeof(MiniHeapHdr);
    void* base = VirtualAlloc(nullptr, (SIZE_T)total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!base) return nullptr;
    ((MiniHeapHdr*)base)->total = total;
    return (uint8_t*)base + sizeof(MiniHeapHdr);
}

extern "C" void __cdecl free(void* p) {
    if (!p) return;
    void* base = (uint8_t*)p - sizeof(MiniHeapHdr);
    VirtualFree(base, 0, MEM_RELEASE);
}

extern "C" void* __cdecl realloc(void* p, size_t newsz) {
    if (!p) return malloc(newsz);
    if (newsz == 0) {
        free(p);
        return nullptr;
    }

    MiniHeapHdr* old_hdr = (MiniHeapHdr*)((uint8_t*)p - sizeof(MiniHeapHdr));
    const size_t oldsz = old_hdr->total > sizeof(MiniHeapHdr) ? old_hdr->total - sizeof(MiniHeapHdr) : 0;

    void* np = malloc(newsz);
    if (!np) return nullptr;
    const size_t copy_n = oldsz < newsz ? oldsz : newsz;
    memcpy(np, p, copy_n);
    free(p);
    return np;
}
#endif

namespace {

struct Buffer {
    uint8_t* ptr{};
    size_t bytes{};
};

struct ImageView {
    Buffer file{};
    Buffer scratch{};
    Buffer rgba{};
    Buffer bgra{};
    uint32_t w{};
    uint32_t h{};
    uint8_t channels_in_file{};
    bool ready{};
};

struct AppState {
    HWND hwnd{};
    wchar_t repo_root[MAX_PATH]{};
    int index{};
    ImageView img{};
};

static const wchar_t* kFormats[] = {
    L"jpg", L"pnm", L"psd", L"hdr", L"bmp", L"tga", L"gif", L"png"
};
static const int kFormatCount = (int)(sizeof(kFormats) / sizeof(kFormats[0]));

static void clear_buffer(Buffer& b) {
    if (b.ptr) {
        VirtualFree(b.ptr, 0, MEM_RELEASE);
        b.ptr = nullptr;
    }
    b.bytes = 0;
}

static bool alloc_buffer(Buffer& b, size_t bytes) {
    clear_buffer(b);
    if (bytes == 0) bytes = 1;
    b.ptr = (uint8_t*)VirtualAlloc(nullptr, (SIZE_T)bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!b.ptr) return false;
    b.bytes = bytes;
    return true;
}

static void clear_image(ImageView& img) {
    clear_buffer(img.file);
    clear_buffer(img.scratch);
    clear_buffer(img.rgba);
    clear_buffer(img.bgra);
    img.w = 0;
    img.h = 0;
    img.channels_in_file = 0;
    img.ready = false;
}

static size_t wlen(const wchar_t* s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static bool wcpy(wchar_t* dst, size_t cap, const wchar_t* src) {
    if (!dst || cap == 0 || !src) return false;
    size_t i = 0;
    while (src[i]) {
        if (i + 1 >= cap) return false;
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
    return true;
}

static bool wcat(wchar_t* dst, size_t cap, const wchar_t* src) {
    const size_t d = wlen(dst);
    size_t i = 0;
    while (src[i]) {
        if (d + i + 1 >= cap) return false;
        dst[d + i] = src[i];
        ++i;
    }
    dst[d + i] = 0;
    return true;
}

static bool pop_last_path_component(wchar_t* path) {
    if (!path) return false;
    size_t n = wlen(path);
    while (n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/')) {
        path[n - 1] = 0;
        --n;
    }
    while (n > 0) {
        const wchar_t c = path[n - 1];
        if (c == L'\\' || c == L'/') {
            path[n - 1] = 0;
            return true;
        }
        --n;
    }
    return false;
}

static bool init_repo_root(wchar_t* out_root, size_t cap) {
    if (!out_root || cap == 0) return false;
    DWORD got = GetModuleFileNameW(nullptr, out_root, (DWORD)cap);
    if (got == 0 || got >= cap) return false;

    // exe: <repo>\build\<config>\i_example.exe -> repo root is 2 levels up
    if (!pop_last_path_component(out_root)) return false; // i_example.exe
    if (!pop_last_path_component(out_root)) return false; // <config>
    if (!pop_last_path_component(out_root)) return false; // build
    return true;
}

static bool build_image_path(const AppState& app, const wchar_t* ext, wchar_t* out_path, size_t cap) {
    if (!wcpy(out_path, cap, app.repo_root)) return false;
    if (!wcat(out_path, cap, L"\\img\\cat.")) return false;
    if (!wcat(out_path, cap, ext)) return false;
    return true;
}

static bool read_file_win32(const wchar_t* path, Buffer& out) {
    clear_buffer(out);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || (uint64_t)sz.QuadPart > (uint64_t)(SIZE_MAX)) {
        CloseHandle(h);
        return false;
    }

    const size_t bytes = (size_t)sz.QuadPart;
    if (!alloc_buffer(out, bytes)) {
        CloseHandle(h);
        return false;
    }

    size_t off = 0;
    while (off < bytes) {
        const size_t left = bytes - off;
        const DWORD chunk = (DWORD)(left > 0x70000000u ? 0x70000000u : left);
        DWORD got = 0;
        if (!ReadFile(h, out.ptr + off, chunk, &got, nullptr) || got == 0) {
            clear_buffer(out);
            CloseHandle(h);
            return false;
        }
        off += (size_t)got;
    }

    CloseHandle(h);
    return true;
}

static void rgba_to_bgra(const uint8_t* src, uint8_t* dst, uint32_t w, uint32_t h) {
    const size_t px = (size_t)w * (size_t)h;
    for (size_t i = 0; i < px; ++i) {
        const size_t o = i * 4u;
        dst[o + 0] = src[o + 2];
        dst[o + 1] = src[o + 1];
        dst[o + 2] = src[o + 0];
        dst[o + 3] = src[o + 3];
    }
}

static void set_title(AppState& app, bool ok) {
    wchar_t title[256]{};
    if (ok) {
        wsprintfW(title, L"stbi viewer: cat.%s (%lu x %lu, comp=%u)  [<- / ->]",
                  kFormats[app.index],
                  (unsigned long)app.img.w,
                  (unsigned long)app.img.h,
                  (unsigned)app.img.channels_in_file);
    } else {
        wsprintfW(title, L"stbi viewer: cat.%s (load failed)  [<- / ->]", kFormats[app.index]);
    }
    SetWindowTextW(app.hwnd, title);
}

static bool load_current_image(AppState& app) {
    clear_image(app.img);

    wchar_t path[MAX_PATH]{};
    if (!build_image_path(app, kFormats[app.index], path, MAX_PATH)) {
        return false;
    }
    if (!read_file_win32(path, app.img.file)) {
        return false;
    }

    stbi::Decoder dec;
    if (!dec.ReadBytes(app.img.file.ptr, app.img.file.bytes)) {
        return false;
    }

    stbi::DecodeOptions opt{};
    opt.desired_channels = 4;
    opt.sample_type = stbi::SampleType::U8;

    stbi::ImagePlan plan{};
    if (!dec.Plan(opt, plan)) {
        return false;
    }

    if (!alloc_buffer(app.img.scratch, plan.scratch_bytes ? plan.scratch_bytes : 1u)) {
        return false;
    }
    if (!alloc_buffer(app.img.rgba, plan.pixel_bytes)) {
        return false;
    }
    if (!alloc_buffer(app.img.bgra, plan.pixel_bytes)) {
        return false;
    }

    if (!dec.Decode(plan,
                    app.img.scratch.ptr, app.img.scratch.bytes,
                    app.img.rgba.ptr, app.img.rgba.bytes)) {
        return false;
    }

    app.img.w = plan.width;
    app.img.h = plan.height;
    app.img.channels_in_file = plan.channels_in_file;
    rgba_to_bgra(app.img.rgba.ptr, app.img.bgra.ptr, app.img.w, app.img.h);
    app.img.ready = true;
    return true;
}

static void paint_image(HWND hwnd, AppState& app) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(24, 24, 24));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    if (!app.img.ready || !app.img.bgra.ptr || app.img.w == 0 || app.img.h == 0) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220, 220, 220));
        const wchar_t* msg = L"Failed to load current image.";
        DrawTextW(dc, msg, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return;
    }

    const int cw = rc.right - rc.left;
    const int ch = rc.bottom - rc.top;

    double sx = (double)cw / (double)app.img.w;
    double sy = (double)ch / (double)app.img.h;
    double s = sx < sy ? sx : sy;
    if (s <= 0.0) s = 1.0;

    const int dw = (int)((double)app.img.w * s);
    const int dh = (int)((double)app.img.h * s);
    const int dx = (cw - dw) / 2;
    const int dy = (ch - dh) / 2;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)app.img.w;
    bmi.bmiHeader.biHeight = -(LONG)app.img.h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(dc,
                  dx, dy, dw, dh,
                  0, 0, (int)app.img.w, (int)app.img.h,
                  app.img.bgra.ptr, &bmi, DIB_RGB_COLORS, SRCCOPY);

    EndPaint(hwnd, &ps);
}

static void cycle_format(AppState& app, int dir) {
    int next = app.index + dir;
    if (next < 0) next = kFormatCount - 1;
    if (next >= kFormatCount) next = 0;
    app.index = next;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    AppState* app = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)l;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }

        case WM_CREATE: {
            app = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (!app) return -1;
            app->hwnd = hwnd;
            const bool ok = load_current_image(*app);
            set_title(*app, ok);
            return 0;
        }

        case WM_KEYDOWN: {
            if (!app) return 0;
            if (w == VK_RIGHT || w == VK_LEFT) {
                cycle_format(*app, (w == VK_RIGHT) ? +1 : -1);
                const bool ok = load_current_image(*app);
                set_title(*app, ok);
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            break;
        }

        case WM_PAINT:
            if (app) paint_image(hwnd, *app);
            return 0;

        case WM_DESTROY:
            if (app) clear_image(app->img);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, w, l);
}

} // namespace

#ifndef CONSOLE
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    AppState app{};
    if (!init_repo_root(app.repo_root, MAX_PATH)) {
        MessageBoxW(nullptr, L"Failed to resolve repository root path.", L"stbi viewer", MB_ICONERROR);
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"STBI_EXAMPLE_WIN32";
    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassW failed.", L"stbi viewer", MB_ICONERROR);
        return 2;
    }

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"stbi viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800,
        nullptr, nullptr, h, &app
    );
    if (!hwnd) {
        MessageBoxW(nullptr, L"CreateWindowExW failed.", L"stbi viewer", MB_ICONERROR);
        return 3;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
#else
#error "This example runs in NoConsole mode only"
#endif

#else
#error "stbi_example.cpp is Windows-only."
#endif
