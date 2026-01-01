/*
MIT License
Copyright (c) 2017 Sean Barrett
Copyright (c) 2025 setbe

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


ABOUT:

   This header file is a library for writing images to C stdio or a callback.

   The PNG output is not optimal; it is 20-50% larger than the file
   written by a decent optimizing implementation; though providing a custom
   zlib compress function (see STBIW_ZLIB_COMPRESS) can mitigate that.
   This library is designed for source code compactness and simplicity,
   not optimal image file size or run-time performance.

BUILDING:

   You can #define STBIW_ASSERT(x) before the #include to avoid using assert.h.
   You can #define STBIW_MALLOC(), STBIW_REALLOC(), and STBIW_FREE() to replace
   malloc,realloc,free.
   You can #define STBIW_MEMMOVE() to replace memmove()
   You can #define STBIW_ZLIB_COMPRESS to use a custom zlib-style compress function
   for PNG compression (instead of the builtin one), it must have the following signature:
   unsigned char * my_compress(unsigned char *data, int data_len, int *out_len, int quality);
   The returned data will be freed with STBIW_FREE() (free() by default),
   so it must be heap allocated with STBIW_MALLOC() (malloc() by default),

UNICODE:

   If compiling for Windows and you wish to use Unicode filenames, compile
   with
       #define STBIW_WINDOWS_UTF8
   and pass utf8-encoded filenames. Call stbiw_convert_wchar_to_utf8 to convert
   Windows wchar_t filenames to utf8.

USAGE:

   There are five functions, one for each image file format:

     int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
     int stbi_write_bmp(char const *filename, int w, int h, int comp, const void *data);
     int stbi_write_tga(char const *filename, int w, int h, int comp, const void *data);
     int stbi_write_jpg(char const *filename, int w, int h, int comp, const void *data, int quality);
     int stbi_write_hdr(char const *filename, int w, int h, int comp, const float *data);

     void stbi_flip_vertically_on_write(int flag); // flag is non-zero to flip data vertically

   There are also five equivalent functions that use an arbitrary write function. You are
   expected to open/close your file-equivalent before and after calling these:

     int stbi_write_png_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data, int stride_in_bytes);
     int stbi_write_bmp_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
     int stbi_write_tga_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const void  *data);
     int stbi_write_hdr_to_func(stbi_write_func *func, void *context, int w, int h, int comp, const float *data);
     int stbi_write_jpg_to_func(stbi_write_func *func, void *context, int x, int y, int comp, const void *data, int quality);

   where the callback is:
      void stbi_write_func(void *context, void *data, int size);

   You can configure it with these global variables:
      int stbi_write_tga_with_rle;             // defaults to true; set to 0 to disable RLE
      int stbi_write_png_compression_level;    // defaults to 8; set to higher for more compression
      int stbi_write_force_png_filter;         // defaults to -1; set to 0..5 to force a filter mode


   You can define STBI_WRITE_NO_STDIO to disable the file variant of these
   functions, so the library will not use stdio.h at all. However, this will
   also disable HDR writing, because it requires stdio for formatted output.

   Each function returns 0 on failure and non-0 on success.

   The functions create an image file defined by the parameters. The image
   is a rectangle of pixels stored from left-to-right, top-to-bottom.
   Each pixel contains 'comp' channels of data stored interleaved with 8-bits
   per channel, in the following order: 1=Y, 2=YA, 3=RGB, 4=RGBA. (Y is
   monochrome color.) The rectangle is 'w' pixels wide and 'h' pixels tall.
   The *data pointer points to the first byte of the top-left-most pixel.
   For PNG, "stride_in_bytes" is the distance in bytes from the first byte of
   a row of pixels to the first byte of the next row of pixels.

   PNG creates output files with the same number of components as the input.
   The BMP format expands Y to RGB in the file format and does not
   output alpha.

   PNG supports writing rectangles of data even when the bytes storing rows of
   data are not consecutive in memory (e.g. sub-rectangles of a larger image),
   by supplying the stride between the beginning of adjacent rows. The other
   formats do not. (Thus you cannot write a native-format BMP through the BMP
   writer, both because it is in BGR order and because it may have padding
   at the end of the line.)

   PNG allows you to set the deflate compression level by setting the global
   variable 'stbi_write_png_compression_level' (it defaults to 8).

   HDR expects linear float data. Since the format is always 32-bit rgb(e)
   data, alpha (if provided) is discarded, and for monochrome data it is
   replicated across all three channels.

   TGA supports RLE or non-RLE compressed data. To use non-RLE-compressed
   data, set the global variable 'stbi_write_tga_with_rle' to 0.

   JPEG does ignore alpha channels in input data; quality is between 1 and 100.
   Higher quality looks better but results in a bigger image.
   JPEG baseline (no JPEG progressive).

CREDITS:


   Sean Barrett           -    PNG/BMP/TGA
   Baldur Karlsson        -    HDR
   Jean-Sebastien Guay    -    TGA monochrome
   Tim Kelsey             -    misc enhancements
   Alan Hickman           -    TGA RLE
   Emmanuel Julien        -    initial file IO callback implementation
   Jon Olick              -    original jo_jpeg.cpp code
   Daniel Gibson          -    integrate JPEG, allow external zlib
   Aarni Koskela          -    allow choosing PNG filter

   bugfixes:
      github:Chribba
      Guillaume Chereau
      github:jry2
      github:romigrou
      Sergio Gonzalez
      Jonas Karlsson
      Filip Wasil
      Thatcher Ulrich
      github:poppolopoppo
      Patrick Boettcher
      github:xeekworx
      Cap Petschulat
      Simon Rodriguez
      Ivan Tikhonov
      github:ignotion
      Adam Schackart
      Andrew Kensler
*/

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
////
////   INTEGRATION WITH YOUR CODEBASE
////
////   The following sections allow you to supply alternate definitions
////   of C library functions used by stb_image_write, e.g. if you don't
////   link with the C runtime library.

#ifdef STBIW_FREESTANDING

// If freestanding: you must provide own math macros.
// e.g. #define your own STBIW_ifloor/STBIW_iceil() before including this file.

// STBIW_ifloor(x)
// STBIW_iceil(x)
// STBIW_sqrt(x)
// STBIW_pow(x, y)
// STBIW_fmod(x, y)
// STBIW_cos(x)
// STBIW_acos(x)
// STBIW_fabs(x)

#else // !STBIW_FREESTANDING

// Floor / Ceil
#   ifndef STBIW_ifloor
#       include <math.h>
#       define STBIW_ifloor(x)   ((int)floor(x))
#       define STBIW_iceil(x)    ((int)ceil(x))
#   endif

// Square root / power
#   ifndef STBIW_sqrt
#       include <math.h>
#       define STBIW_sqrt(x)     sqrt(x)
#       define STBIW_pow(x,y)    pow(x,y)
#   endif

// fmod
#   ifndef STBIW_fmod
#       include <math.h>
#       define STBIW_fmod(x,y)   fmod(x,y)
#   endif

// cos / acos
#   ifndef STBIW_cos
#       include <math.h>
#       define STBIW_cos(x)      cos(x)
#       define STBIW_acos(x)     acos(x)
#   endif

// fabs
#   ifndef STBIW_fabs
#       include <math.h>
#       define STBIW_fabs(x)     fabs(x)
#   endif

#endif // !STBIW_FREESTANDING


// ----------------------------------------------
// Fallbacks for completely math.h-free environments
// (only compiled if STBIW_FREESTANDING is defined
//  and user hasn't provided replacements)
// ----------------------------------------------
#ifdef STBIW_FREESTANDING

#ifndef STBIW_ifloor
static inline int STBIW_ifloor(float x) noexcept {
    return (int)(x >= 0 ? (int)x : (int)x - (x != (int)x));
}
#   define STBIW_ifloor(x) STBIW_ifloor(x)
#endif

#ifndef STBIW_iceil
static inline int STBIW_iceil(float x) noexcept {
    int i = (int)x; return (x > i) ? i + 1 : i;
}
#   define STBIW_iceil(x) STBIW_iceil(x)
#endif

#ifndef STBIW_fabs
static inline float STBIW_fabs(float x) noexcept { return x < 0 ? -x : x; }
#define STBIW_fabs(x) STBIW_fabs(x)
#endif

#ifndef STBIW_sqrt
// Basic Newton-Raphson sqrt approximation
static inline float STBIW_sqrt(float x) noexcept {
    if (x <= 0) return 0;
    float r = x;
    for (int i = 0; i < 5; ++i)
        r = 0.5f * (r + x / r);
    return r;
}
#define STBIW_sqrt(x) STBIW_sqrt(x)
#endif

#ifndef STBIW_pow
static inline float STBIW_pow(float base, float exp) noexcept {
    // crude exp/log approximation (only for small exp)
    float result = 1.0f;
    int e = (int)exp;
    for (int i = 0; i < e; ++i)
        result *= base;
    return result;
}
#define STBIW_pow(x,y) STBIW_pow(x,y)
#endif

#ifndef STBIW_fmod
static inline float STBIW_fmod(float x, float y) noexcept {
    return x - (int)(x / y) * y;
}
#    define STBIW_fmod(x,y) STBIW_fmod(x,y)
#endif

#ifndef STBIW_cos
// Taylor approximation of cos(x) for small angles
static inline float STBIW_cos(float x) noexcept {
    const float PI = 3.14159265358979323846f;
    while (x > PI) x -= 2 * PI;
    while (x < -PI) x += 2 * PI;
    float x2 = x * x;
    return 1.0f - x2 / 2 + x2 * x2 / 24 - x2 * x2 * x2 / 720;
}
#    define STBIW_cos(x) STBIW_cos(x)
#endif

#ifndef STBIW_acos
// Rough acos approximation
static inline float STBIW_acos(float x) noexcept {
    // Clamp
    if (x < -1) x = -1;
    if (x > 1) x = 1;
    // Polynomial approximation
    float negate = x < 0;
    x = STBIW_fabs(x);
    float ret = -0.0187293f;
    ret = ret * x + 0.0742610f;
    ret = ret * x - 0.2121144f;
    ret = ret * x + 1.5707288f;
    ret = ret * STBIW_sqrt(1.0f - x);
    return negate ? 3.14159265f - ret : ret;
}
#    define STBIW_acos(x) STBIW_acos(x)
#endif

#endif // STBIW_FREESTANDING

// ----------------------------------------------
// #define your own functions "STBIW_malloc" / "STBIW_free" to avoid OS calls.
// ----------------------------------------------

#ifdef STBIW_FREESTANDING

// STBIW_malloc(x,u)
// STBIW_free(x,u)
// STBIW_strlen(x)
// STBIW_memcpy
// STBIW_memset

#else // !STBIW_FREESTANDING

#include <stdlib.h>
#include <string.h>

// Default to malloc/free
#ifndef STBIW_malloc
#   define STBIW_malloc(x,u)  ((void)(u), malloc(x))
#   define STBIW_free(x,u)    ((void)(u), free(x))
#endif

#ifndef STBIW_strlen
#   define STBIW_strlen(x)    strlen(x)
#endif

#ifndef STBIW_memcpy
#   define STBIW_memcpy       memcpy
#   define STBIW_memset       memset
#endif

#endif // !STBIW_FREESTANDING

// ----------------------------------------------
// Fallbacks for freestanding builds without stdlib
// Use VirtualAlloc/VirtualFree or mmap/munmap
// ----------------------------------------------
#ifdef STBIW_FREESTANDING

#if !defined(STBIW_malloc) || !defined(STBIW_free)
#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>

static void* STBIW_win_alloc(size_t sz, void* userdata) {
    const int commit = 0x00001000;
    const int reserve = 0x00002000;
    const int page_readwrite = 0x04;
    (void)userdata;
    return VirtualAlloc(nullptr, sz, commit | reserve, page_readwrite);
}

static void STBIW_win_free(void* ptr, void* userdata) {
    const int release = 0x00008000;
    (void)userdata;
    if (ptr) VirtualFree(ptr, 0, release);
}

#   ifndef STBIW_malloc
#       define STBIW_malloc(x,u)  STBIW_win_alloc(x,u)
#   endif
#   ifndef STBIW_free
#       define STBIW_free(x,u)    STBIW_win_free(x,u)
#   endif

#else // POSIX fallback
#   include <sys/mman.h>
#   include <unistd.h>

static void* STBIW_posix_alloc(size_t sz, void* userdata) {
    (void)userdata;
    size_t total = sz + sizeof(size_t);
    void* p = mmap(nullptr, total, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    *((size_t*)p) = total; // keep the size at the beginning of the block
    // return the pointer after size field
    return (uint8_t*)p + sizeof(size_t);
}

static void STBIW_posix_free(void* ptr, void* userdata) {
    (void)userdata;
    if (!ptr) return;
    // restore the start address and read the size
    uint8_t* base = (uint8_t*)ptr - sizeof(size_t);
    size_t total = *((size_t*)base);
    munmap(base, total); // return mmap back to the system
}

#   ifndef STBIW_malloc
#       define STBIW_malloc(x,u)  STBIW_posix_alloc(x,u)
#   endif
#   ifndef STBIW_free
#       define STBIW_free(x,u)    STBIW_posix_free(x,u)
#   endif

#endif // platform
#endif // missing malloc/free

#ifndef STBIW_strlen
static size_t STBIW_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) ++len;
    return len;
}
#define STBIW_strlen(x) STBIW_strlen(x)
#endif

#ifndef STBIW_memcpy
static void* STBIW_memcpy(void* dst, const void* src, size_t sz) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (sz--) *d++ = *s++;
    return dst;
}
static void* STBIW_memset(void* dst, int val, size_t sz) {
    unsigned char* d = (unsigned char*)dst;
    while (sz--) *d++ = (unsigned char)val;
    return dst;
}
#define STBIW_memcpy STBIW_memcpy
#define STBIW_memset STBIW_memset
#endif

#endif // STBIW_FREESTANDING

namespace stb {
    namespace img {
struct Writer {
private:
    bool _flip_vertically;
public:
    explicit Writer(bool flip_vertically_on_write) noexcept 
        : _flip_vertically{ flip_vertically_on_write } {

    }
}; // struct Writer
    } // namespace img
} // namespace stb