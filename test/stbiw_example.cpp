#include <stddef.h>
#include <stdint.h>

extern "C" void* memset(void* dst, int val, size_t sz) {
    uint8_t* d = (uint8_t*)dst;
    while (sz--) *d++ = (uint8_t)val;
    return dst;
}

#define STBIW_memset(dst, val, sz) memset(dst, val, sz)

#define STBIW_FREESTANDING
#include "../stb_image_write/stb_image_write.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace {

    using u8 = unsigned char;
    using u32 = unsigned int;

    struct FileSink {
        HANDLE h = INVALID_HANDLE_VALUE;
        bool ok = true;
        u32 written_total = 0;
    };

    static void win32_write_cb(void* ctx, const void* data, int size) {
        FileSink* s = static_cast<FileSink*>(ctx);
        if (!s || !s->ok || s->h == INVALID_HANDLE_VALUE || !data || size <= 0) return;

        const u8* p = static_cast<const u8*>(data);
        u32 remaining = (u32)size;

        while (remaining) {
            DWORD chunk_written = 0;
            // WriteFile accepts DWORD size, so write chunks
            DWORD chunk = remaining;
            if (!WriteFile(s->h, p, chunk, &chunk_written, NULL)) {
                s->ok = false;
                return;
            }
            if (chunk_written == 0) {
                s->ok = false;
                return;
            }
            p += chunk_written;
            remaining -= (u32)chunk_written;
            s->written_total += (u32)chunk_written;
        }
    }

    static bool write_file_bmp(const wchar_t* path, const void* pixels, int w, int h, int comp, bool flip) {
        FileSink sink;
        sink.h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (sink.h == INVALID_HANDLE_VALUE) return false;

        stbiw::Writer wr;
        wr.start_callbacks(&win32_write_cb, &sink);
        wr.set_flip_vertically(flip);

        const bool ok = wr.write_bmp_core(w, h, comp, pixels);
        wr.flush();

        CloseHandle(sink.h);
        sink.h = INVALID_HANDLE_VALUE;

        return ok && sink.ok;
    }

    static bool write_file_tga(const wchar_t* path, const void* pixels, int w, int h, int comp, bool flip, bool rle) {
        FileSink sink;
        sink.h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (sink.h == INVALID_HANDLE_VALUE) return false;

        stbiw::Writer wr;
        wr.start_callbacks(&win32_write_cb, &sink);
        wr.set_flip_vertically(flip);
        wr.set_tga_rle(rle);

        const bool ok = wr.write_tga_core(w, h, comp, pixels);
        wr.flush();

        CloseHandle(sink.h);
        sink.h = INVALID_HANDLE_VALUE;

        return ok && sink.ok;
    }

    static inline void set_px_rgba(u8* img, int w, int x, int y, u8 r, u8 g, u8 b, u8 a) {
        const u32 i = (u32)(y * w + x) * 4u;
        img[i + 0] = r;
        img[i + 1] = g;
        img[i + 2] = b;
        img[i + 3] = a;
    }

    static void draw_demo_image(u8* img, int w, int h) {
        // 1) Background: grey RGB, alpha gradient top->bottom
        for (int y = 0; y < h; ++y) {
            // alpha: 255 -> 0
            u8 a = 0;
            if (h <= 1) a = 255;
            else {
                const u32 t = (u32)y * 255u;
                const u32 denom = (u32)(h - 1);
                a = (u8)(255u - (t / denom));
            }

            for (int x = 0; x < w; ++x) {
                set_px_rgba(img, w, x, y, 128, 128, 128, a);
            }
        }

        // 2) Three horizontal lines: red, green, blue (opaque)
        const int yR = (h * 1) / 4;
        const int yG = (h * 2) / 4;
        const int yB = (h * 3) / 4;

        for (int x = 0; x < w; ++x) {
            if (yR >= 0 && yR < h) set_px_rgba(img, w, x, yR, 255, 0, 0, 255);
            if (yG >= 0 && yG < h) set_px_rgba(img, w, x, yG, 0, 255, 0, 255);
            if (yB >= 0 && yB < h) set_px_rgba(img, w, x, yB, 0, 0, 255, 255);
        }
    }

} // namespace

#ifndef CONSOLE
// entry
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {
    const int W = 512;
    const int H = 256;
    const int COMP = 4; // RGBA

    const SIZE_T bytes = (SIZE_T)W * (SIZE_T)H * 4u;

    u8* img = (u8*)VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!img) {
        MessageBoxW(NULL, L"VirtualAlloc failed", L"stbiw", MB_ICONERROR);
        return 1;
    }

    draw_demo_image(img, W, H);

    // Where to write images:
    // For demo — next to .exe (current dir related to the process).
    const wchar_t* bmp_path = L"demo_rgba.bmp";
    const wchar_t* tga_path = L"demo_rgba.tga";
    const bool flip = false;

    const bool ok_bmp = write_file_bmp(bmp_path, img, W, H, COMP, flip);
    const bool ok_tga = write_file_tga(tga_path, img, W, H, COMP, flip, /*rle*/true);

    VirtualFree(img, 0, MEM_RELEASE);

    if (!ok_bmp || !ok_tga) {
        MessageBoxW(NULL, L"Write failed (BMP or TGA)", L"stbiw", MB_ICONERROR);
        return 2;
    }

    MessageBoxW(NULL, L"Wrote demo_rgba.bmp and demo_rgba.tga", L"stbiw", MB_OK);
    return 0;
    ExitProcess(0);
}
#else // defined CONSOLE
# error "This test runs in NoConsole mode only"
#endif

#endif // ifdef _WIN32