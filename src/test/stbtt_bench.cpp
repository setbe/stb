// ENV:
//  STBTT_TEST_FONT    : one primary font path
//  STBTT_TEST_FONTS   : additional font paths separated by ';' or ':'
//  STBTT_BENCH_ITERS  : iterations per font (default 100000)
//  STBTT_BENCH_MODE   : "glyph" (default) or "text"
//  STBTT_BENCH_REF    : "1" to enable reference bench (default 1 if reference header present)

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <limits>

#if defined(_WIN32)
#   include <windows.h>
#   include <mmsystem.h>
#   pragma comment(lib, "winmm.lib")
#   undef max
static void set_high_perf_timer() { timeBeginPeriod(1); }
#else
static void set_high_perf_timer() {}
#endif

// ---------------- Canary allocator ----------------
namespace stbtt_bench {

    static constexpr std::uint64_t kMagic = 0xC0FFEEBADC0DEULL;
    static constexpr std::uint64_t kTail = 0xF00DFACEDEADF00DULL;

    struct AllocStats {
        std::size_t live_blocks = 0;
        std::size_t live_bytes = 0;
        std::size_t total_allocs = 0;
        std::size_t total_frees = 0;
        bool corrupt = false;
        const char* corrupt_reason = nullptr;
    };

    struct BlockHeader {
        std::uint64_t magic;
        std::size_t size;
        void* base;
    };

    static inline std::size_t align_up(std::size_t x, std::size_t a) {
        return (x + (a - 1)) & ~(a - 1);
    }

    static void mark_corrupt(AllocStats* s, const char* reason) {
        if (!s) return;
        s->corrupt = true;
        s->corrupt_reason = reason;
    }

    static void* tt_malloc(std::size_t sz, void* userdata) {
        auto* st = reinterpret_cast<AllocStats*>(userdata);

        const std::size_t A = alignof(std::max_align_t);
        const std::size_t header_sz = sizeof(BlockHeader);
        const std::size_t tail_sz = sizeof(std::uint64_t);

        std::size_t total = header_sz + sz + tail_sz + A;
        void* base = std::malloc(total);
        if (!base) return nullptr;

        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(base);
        std::uintptr_t user = align_up(p + header_sz, A);
        auto* h = reinterpret_cast<BlockHeader*>(user - header_sz);

        h->magic = kMagic;
        h->size = sz;
        h->base = base;

        *reinterpret_cast<std::uint64_t*>(user + sz) = kTail;

        if (st) {
            st->live_blocks++;
            st->live_bytes += sz;
            st->total_allocs++;
        }

        // optional poison (can add overhead; keep off by default)
        // std::memset(reinterpret_cast<void*>(user), 0xCD, sz);

        return reinterpret_cast<void*>(user);
    }

    static void tt_free(void* ptr, void* userdata) {
        if (!ptr) return;
        auto* st = reinterpret_cast<AllocStats*>(userdata);

        std::uintptr_t user = reinterpret_cast<std::uintptr_t>(ptr);
        auto* h = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));

        if (h->magic != kMagic) {
            mark_corrupt(st, "Header magic mismatch.");
            return;
        }

        auto* tailp = reinterpret_cast<std::uint64_t*>(user + h->size);
        if (*tailp != kTail) {
            mark_corrupt(st, "Tail canary mismatch.");
        }

        if (st) {
            if (st->live_blocks) st->live_blocks--;
            if (st->live_bytes >= h->size) st->live_bytes -= h->size;
            st->total_frees++;
        }

        void* base = h->base;
        h->magic = 0;
        std::free(base);
    }

    static std::string getenv_str(const char* name) {
        const char* v = std::getenv(name);
        return v ? std::string(v) : std::string{};
    }

    static std::vector<std::string> split_paths(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == ';' || c == ':') {
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            }
            else cur.push_back(c);
        }
        if (!cur.empty()) out.push_back(cur);

        for (auto& p : out) {
            while (!p.empty() && (p.front() == ' ' || p.front() == '\t')) p.erase(p.begin());
            while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
        }
        out.erase(std::remove_if(out.begin(), out.end(), [](auto& x) {return x.empty(); }), out.end());
        return out;
    }

    static bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        f.seekg(0, std::ios::end);
        std::streamoff n = f.tellg();
        if (n <= 0) return false;
        f.seekg(0, std::ios::beg);
        out.resize((std::size_t)n);
        f.read((char*)out.data(), n);
        return f.good();
    }

    static std::vector<std::string> default_font_candidates() {
        return {
    #if defined(_WIN32)
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\calibri.ttf",
            "C:\\Windows\\Fonts\\times.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arialbd.ttf",
    #elif defined(__APPLE__)
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
            "/System/Library/Fonts/Supplemental/Courier New.ttf",
            "/System/Library/Fonts/SFNS.ttf",
    #else
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
    #endif
        };
    }

    static std::vector<std::string> collect_font_paths() {
        std::vector<std::string> paths;

        auto p1 = getenv_str("STBTT_TEST_FONT");
        if (!p1.empty()) paths.push_back(p1);

        auto pN = getenv_str("STBTT_TEST_FONTS");
        if (!pN.empty()) {
            auto more = split_paths(pN);
            paths.insert(paths.end(), more.begin(), more.end());
        }

        auto defs = default_font_candidates();
        paths.insert(paths.end(), defs.begin(), defs.end());

        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        return paths;
    }

    static std::uint64_t fnv1a64(const void* data, std::size_t n) {
        const auto* p = (const std::uint8_t*)data;
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    static int getenv_int(const char* name, int def) {
        auto s = getenv_str(name);
        if (s.empty()) return def;
        try { return std::max(1, std::stoi(s)); }
        catch (...) { return def; }
    }

    struct BenchResult {
        double ms_total = 0.0;
        std::size_t allocs = 0;
        std::size_t frees = 0;
        std::size_t bytes_live_peak = 0;
        std::uint64_t checksum = 0;
    };

} // namespace stbtt_bench

// Bind allocator hooks:
#define STBTT_malloc(sz,u) stbtt_bench::tt_malloc((sz),(u))
#define STBTT_free(p,u)    stbtt_bench::tt_free((p),(u))

// Fork
#include "stbtt/stb_truetype.hpp"

// Reference stb (optional)
#ifndef STBTT_BENCH_NO_REFERENCE
#   define STB_TRUETYPE_IMPLEMENTATION
#   define STBTT_STATIC
#   include "stbtt/stb_truetype.h"
#endif

// ---------------- Bench core ----------------
static stbtt_bench::BenchResult bench_port_glyph(std::vector<std::uint8_t>& font_bytes,
    int iters, stbtt_bench::AllocStats& st)
{
    stbtt::TrueType tt;
    tt.fi.userdata = &st;
    if (!tt.ReadBytes(font_bytes.data())) return {};

    // pick a stable glyph
    int gA = tt.FindGlyphIndex('A');
    if (gA < 0) gA = 0;

    float sc = tt.ScaleForPixelHeight(32.0f);
    auto bb = tt.GetGlyphBitmapBox(gA, sc, sc, 0.25f, 0.25f);
    int w = bb.x1 - bb.x0;
    int h = bb.y1 - bb.y0;
    if (w <= 0 || h <= 0) return {};

    std::vector<std::uint8_t> bmp((std::size_t)w * (std::size_t)h);

    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t chk = 0;
    for (int i = 0; i < iters; ++i) {
        std::memset(bmp.data(), 0, bmp.size());
        tt.MakeGlyphBitmap(bmp.data(), gA, w, h, w, sc, sc, 0.25f, 0.25f);
        chk ^= stbtt_bench::fnv1a64(bmp.data(), bmp.size());
    }
    auto t1 = std::chrono::steady_clock::now();

    stbtt_bench::BenchResult r{};
    r.ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.allocs = st.total_allocs;
    r.frees = st.total_frees;
    r.checksum = chk;
    return r;
}

static stbtt_bench::BenchResult bench_ref_glyph(const std::vector<std::uint8_t>& font_bytes,
    int iters, stbtt_bench::AllocStats& st)
{
    (void)st; // reference uses malloc/free, we don't track it

    stbtt_fontinfo ref{};
    int off = stbtt_GetFontOffsetForIndex(font_bytes.data(), 0);
    if (off < 0) return {};
    if (!stbtt_InitFont(&ref, font_bytes.data(), off)) return {};

    int gA = stbtt_FindGlyphIndex(&ref, 'A');
    float sc = stbtt_ScaleForPixelHeight(&ref, 32.0f);

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBoxSubpixel(&ref, gA, sc, sc, 0.25f, 0.25f, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return {};

    std::vector<std::uint8_t> bmp((std::size_t)w * (std::size_t)h);

    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t chk = 0;
    for (int i = 0; i < iters; ++i) {
        std::memset(bmp.data(), 0, bmp.size());
        stbtt_MakeGlyphBitmapSubpixel(&ref, bmp.data(), w, h, w, sc, sc, 0.25f, 0.25f, gA);
        chk ^= stbtt_bench::fnv1a64(bmp.data(), bmp.size());
    }
    auto t1 = std::chrono::steady_clock::now();

    stbtt_bench::BenchResult r{};
    r.ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.checksum = chk;
    return r;
}

int main() {
    set_high_perf_timer();

    const int iters = stbtt_bench::getenv_int("STBTT_BENCH_ITERS", 100000);

    auto paths = stbtt_bench::collect_font_paths();
    std::cout << "Fonts candidates: " << paths.size() << "\n";
    std::cout << "Iterations/font: " << iters << "\n\n";

    // header
    std::cout << "font\t\t\t\tbytes\tport_ms\tref_ms\tport_allocs\tport_frees\tchecksums_match\n";

    for (const auto& path : paths) {
        std::vector<std::uint8_t> bytes;
        if (!stbtt_bench::read_file(path, bytes)) continue;

        stbtt_bench::AllocStats st_port{};
        auto r_port = bench_port_glyph(bytes, iters, st_port);

        // reset alloc stats sanity
        bool ok_alloc = !st_port.corrupt && st_port.live_blocks == 0 && st_port.live_bytes == 0;

#ifndef STBTT_BENCH_NO_REFERENCE
        stbtt_bench::AllocStats st_ref{};
        auto r_ref = bench_ref_glyph(bytes, iters, st_ref);
        bool chk_ok = (r_ref.ms_total == 0.0) ? true : (r_port.checksum == r_ref.checksum);
#else
        double ref_ms = 0.0;
        bool chk_ok = true;
#endif

#ifndef STBTT_BENCH_NO_REFERENCE
        double ref_ms = r_ref.ms_total;
#else
        double ref_ms = 0.0;
#endif

        std::cout
            << path << "\t"
            << bytes.size() << "\t"
            << r_port.ms_total << "\t"
            << ref_ms << "\t"
            << r_port.allocs << "\t\t"
            << r_port.frees << "\t\t"
            << (chk_ok && ok_alloc ? "yes" : "no")
            << "\n";
    }

    return 0;
}
