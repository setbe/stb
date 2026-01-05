#pragma once

// ----------------------------------------------
// #define your own functions "STBTT_malloc" / "STBTT_free" to avoid OS calls.
//
// Fallbacks for freestanding builds without stdlib
// Use VirtualAlloc/VirtualFree or mmap/munmap
// ----------------------------------------------
#ifdef STBTT_FREESTANDING

#if !defined(STBTT_malloc) || !defined(STBTT_free)
#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>

static void* STBTT_win_alloc(size_t sz, void* userdata) {
    const int commit = 0x00001000;
    const int reserve = 0x00002000;
    const int page_readwrite = 0x04;
    (void)userdata;
    return VirtualAlloc(nullptr, sz, commit | reserve, page_readwrite);
}

static void STBTT_win_free(void* ptr, void* userdata) {
    const int release = 0x00008000;
    (void)userdata;
    if (ptr) VirtualFree(ptr, 0, release);
}

#   ifndef STBTT_malloc
#       define STBTT_malloc(x,u)  STBTT_win_alloc(x,u)
#   endif
#   ifndef STBTT_free
#       define STBTT_free(x,u)    STBTT_win_free(x,u)
#   endif

#else // POSIX fallback
#   include <sys/mman.h>
#   include <unistd.h>

static void* STBTT_posix_alloc(size_t sz, void* userdata) {
    (void)userdata;
    size_t total = sz + sizeof(size_t);
    void* p = mmap(nullptr, total, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    *((size_t*)p) = total; // keep the size at the beginning of the block
    // return the pointer after size field
    return (uint8_t*)p + sizeof(size_t);
}

static void STBTT_posix_free(void* ptr, void* userdata) {
    (void)userdata;
    if (!ptr) return;
    // restore the start address and read the size
    uint8_t* base = (uint8_t*)ptr - sizeof(size_t);
    size_t total = *((size_t*)base);
    munmap(base, total); // return mmap back to the system
}

#   ifndef STBTT_malloc
#       define STBTT_malloc(x,u)  STBTT_posix_alloc(x,u)
#   endif
#   ifndef STBTT_free
#       define STBTT_free(x,u)    STBTT_posix_free(x,u)
#   endif

#endif // platform
#endif // missing malloc/free

#ifndef STBTT_strlen
static size_t STBTT_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) ++len;
    return len;
}
#define STBTT_strlen(x) STBTT_strlen(x)
#endif

#ifndef STBTT_memcpy
static void* STBTT_memcpy(void* dst, const void* src, size_t sz) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (sz--) *d++ = *s++;
    return dst;
}
static void* STBTT_memset(void* dst, int val, size_t sz) {
    unsigned char* d = (unsigned char*)dst;
    while (sz--) *d++ = (unsigned char)val;
    return dst;
}
#define STBTT_memcpy STBTT_memcpy
#define STBTT_memset STBTT_memset
#endif

#endif // STBTT_FREESTANDING