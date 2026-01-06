#pragma once

// ----------------------------------------------
// Freestanding libc integration for stb_image_write
// Define STBIW_FREESTANDING to avoid hosted libc.
//
// You can override any of these macros:
//   STBIW_malloc(sz, ud)
//   STBIW_free(ptr, ud)
//   STBIW_realloc(ptr, newsz, ud)
//   STBIW_realloc_sized(ptr, oldsz, newsz, ud)   (optional)
//
// This fallback uses:
//   - Windows: VirtualAlloc/VirtualFree
//   - POSIX  : mmap/munmap
//
// NOTE: We store allocation total size in a header (size_t) right before user pointer,
// so free/realloc work correctly without external tracking.
// ----------------------------------------------


// If you want freestanding mode, `#define STBIW_FREESTANDING`
#ifdef STBIW_FREESTANDING

#include <stddef.h>
#include <stdint.h>

#if !defined(STBIW_malloc) || !defined(STBIW_free) || !defined(STBIW_realloc)

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>

static void* STBIW_win_alloc(size_t sz, void* userdata) {
    (void)userdata;
    // header: store total size for free/realloc
    size_t total = sz + sizeof(size_t);

    const DWORD MEM_COMMIT_ = 0x00001000;
    const DWORD MEM_RESERVE_ = 0x00002000;
    const DWORD PAGE_RW_ = 0x04;

    void* base = VirtualAlloc(nullptr, total, MEM_COMMIT_ | MEM_RESERVE_, PAGE_RW_);
    if (!base) return nullptr;

    *((size_t*)base) = total;
    return (uint8_t*)base + sizeof(size_t);
}

static void STBIW_win_free(void* ptr, void* userdata) {
    (void)userdata;
    if (!ptr) return;

    void* base = (uint8_t*)ptr - sizeof(size_t);

    const DWORD MEM_RELEASE_ = 0x00008000;
    VirtualFree(base, 0, MEM_RELEASE_);
}

static void* STBIW_win_realloc(void* ptr, size_t newsz, void* userdata) {
    (void)userdata;

    if (!ptr) return STBIW_win_alloc(newsz, userdata);
    if (newsz == 0) { STBIW_win_free(ptr, userdata); return nullptr; }

    // read old size
    uint8_t* base = (uint8_t*)ptr - sizeof(size_t);
    size_t old_total = *((size_t*)base);
    size_t oldsz = (old_total >= sizeof(size_t)) ? (old_total - sizeof(size_t)) : 0;

    void* newp = STBIW_win_alloc(newsz, userdata);
    if (!newp) return nullptr;

    // minimal memcpy (byte copy) here to avoid relying on libc
    size_t copy_sz = (oldsz < newsz) ? oldsz : newsz;
    {
        uint8_t* d = (uint8_t*)newp;
        const uint8_t* s = (const uint8_t*)ptr;
        for (size_t i = 0; i < copy_sz; ++i) d[i] = s[i];
    }

    STBIW_win_free(ptr, userdata);
    return newp;
}

#   ifndef STBIW_malloc
#       define STBIW_malloc(sz,ud)        STBIW_win_alloc((sz),(ud))
#   endif
#   ifndef STBIW_free
#       define STBIW_free(ptr,ud)         STBIW_win_free((ptr),(ud))
#   endif
#   ifndef STBIW_realloc
#       define STBIW_realloc(ptr,newsz,ud) STBIW_win_realloc((ptr),(newsz),(ud))
#   endif

#else // POSIX fallback

#   include <sys/mman.h>
#   include <unistd.h>

static void* STBIW_posix_alloc(size_t sz, void* userdata) {
    (void)userdata;

    // header: store total mapping size for munmap/free/realloc
    size_t total = sz + sizeof(size_t);

    void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return nullptr;

    *((size_t*)base) = total;
    return (uint8_t*)base + sizeof(size_t);
}

static void STBIW_posix_free(void* ptr, void* userdata) {
    (void)userdata;
    if (!ptr) return;

    uint8_t* base = (uint8_t*)ptr - sizeof(size_t);
    size_t total = *((size_t*)base);

    munmap(base, total);
}

static void* STBIW_posix_realloc(void* ptr, size_t newsz, void* userdata) {
    (void)userdata;

    if (!ptr) return STBIW_posix_alloc(newsz, userdata);
    if (newsz == 0) { STBIW_posix_free(ptr, userdata); return nullptr; }

    // read old size from header
    uint8_t* base = (uint8_t*)ptr - sizeof(size_t);
    size_t old_total = *((size_t*)base);
    size_t oldsz = (old_total >= sizeof(size_t)) ? (old_total - sizeof(size_t)) : 0;

    void* newp = STBIW_posix_alloc(newsz, userdata);
    if (!newp) return nullptr;

    // minimal memcpy (byte copy)
    size_t copy_sz = (oldsz < newsz) ? oldsz : newsz;
    {
        uint8_t* d = (uint8_t*)newp;
        const uint8_t* s = (const uint8_t*)ptr;
        for (size_t i = 0; i < copy_sz; ++i) d[i] = s[i];
    }

    STBIW_posix_free(ptr, userdata);
    return newp;
}

#   ifndef STBIW_malloc
#       define STBIW_malloc(sz,ud)         STBIW_posix_alloc((sz),(ud))
#   endif
#   ifndef STBIW_free
#       define STBIW_free(ptr,ud)          STBIW_posix_free((ptr),(ud))
#   endif
#   ifndef STBIW_realloc
#       define STBIW_realloc(ptr,newsz,ud) STBIW_posix_realloc((ptr),(newsz),(ud))
#   endif

#endif // platform

#endif // missing malloc/free/realloc

// Optional realloc_sized: if you have sizes already, you can route here.
// Default implementation just ignores oldsz and uses realloc.
#ifndef STBIW_realloc_sized
#   define STBIW_realloc_sized(ptr,oldsz,newsz,ud) STBIW_realloc((ptr),(newsz),(ud))
#endif

// -------------------- tiny helpers sometimes needed --------------------

#ifndef STBIW_strlen
static size_t STBIW_strlen_impl(const char* s) {
    size_t len = 0;
    while (s && s[len]) ++len;
    return len;
}
#   define STBIW_strlen(x) STBIW_strlen_impl((x))
#endif

#ifndef STBIW_memcpy
static void* STBIW_memcpy_impl(void* dst, const void* src, size_t sz) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (sz--) *d++ = *s++;
    return dst;
}
#   define STBIW_memcpy(d,s,n) STBIW_memcpy_impl((d),(s),(n))
#endif

#ifndef STBIW_memset
static void* STBIW_memset_impl(void* dst, int val, size_t sz) {
    uint8_t* d = (uint8_t*)dst;
    while (sz--) *d++ = (uint8_t)val;
    return dst;
}
#   define STBIW_memset(d,v,n) STBIW_memset_impl((d),(v),(n))
#endif




# else // -------------------- Hosted (NOT freestanding) ----------------------

#ifndef STBIW_malloc
#   define STBIW_malloc(sz)        malloc(sz)
#   define STBIW_realloc(p,newsz)  realloc(p,newsz)
#   define STBIW_free(p)           free(p)
#endif

#ifndef STBIW_realloc_sized
#   define STBIW_realloc_sized(p,oldsz,newsz) STBIW_realloc(p,newsz)
#endif


#ifndef STBIW_memmove
#   define STBIW_memmove(a,b,sz) memmove(a,b,sz)
#endif


#ifndef STBIW_assert
#   include <assert.h>
#   define STBIW_assert(x) assert(x)
#endif

#endif // STBIW_FREESTANDING



// ------------------------ Validation ------------------------

// Require: malloc/free AND (realloc OR realloc_sized)
#if defined(STBIW_malloc) && defined(STBIW_free) && (defined(STBIW_realloc) || defined(STBIW_realloc_sized))
    // ok
#else
#   error "Must define STBIW_malloc/STBIW_free and either STBIW_realloc or STBIW_realloc_sized."
#endif