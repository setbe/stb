#define STBI_FREESTANDING
#include "../stb_image/stb_image.hpp"

#ifdef _WIN32
#include <windows.h>

// window proc
LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
        // fallthrough
    case WM_PAINT:
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProc(hwnd, msg, w, l);
}


#ifndef CONSOLE
// entry
int WINAPI WinMain(HINSTANCE h, HINSTANCE, LPSTR, int) {

    WNDCLASSA wc{};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = h;
    wc.lpszClassName = "TTWin";
    RegisterClassA(&wc);

    HWND win = CreateWindowA("TTWin", "Image Viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 400, 400,
        0, 0, h, 0);

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
    ExitProcess(0);
}
#else // defined CONSOLE
# error "This test runs in NoConsole mode only"
#endif

#endif // ifdef _WIN32