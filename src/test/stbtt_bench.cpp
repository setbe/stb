// Honest bench: warmup + render full ASCII [32..126] per iteration.

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
#include <iomanip>

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
        std::size_t total_req_bytes = 0;
        std::size_t peak_live_bytes = 0;
        bool corrupt = false;
        const char* corrupt_reason = nullptr;
    };

    struct BlockHeader {
        std::uint64_t magic;
        std::size_t   size;
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
        void* base = malloc(total);
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
            st->total_req_bytes += sz;
            if (st->live_bytes > st->peak_live_bytes)
                st->peak_live_bytes = st->live_bytes;
        }
        return reinterpret_cast<void*>(user);
    }

    static void tt_free(void* ptr, void* userdata) {
        if (!ptr) return;
        auto* st = reinterpret_cast<AllocStats*>(userdata);

        std::uintptr_t user = reinterpret_cast<std::uintptr_t>(ptr);
        auto* h = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));

        if (h->magic != kMagic) { mark_corrupt(st, "Header magic mismatch."); return; }

        auto* tailp = reinterpret_cast<std::uint64_t*>(user + h->size);
        if (*tailp != kTail) { mark_corrupt(st, "Tail canary mismatch."); }

        if (st) {
            if (st->live_blocks) st->live_blocks--;
            if (st->live_bytes >= h->size) st->live_bytes -= h->size;
            st->total_frees++;
        }

        void* base = h->base;
        h->magic = 0;
        free(base);
    }

    static std::string getenv_str(const char* name) {
        const char* v = std::getenv(name);
        return v ? std::string(v) : std::string{};
    }

    static int getenv_int(const char* name, int def) {
        auto s = getenv_str(name);
        if (s.empty()) return def;
        try { return std::max(1, std::stoi(s)); }
        catch (...) { return def; }
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
        out.erase(std::remove_if(out.begin(), out.end(), [](auto& x) { return x.empty(); }), out.end());
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

    // Cheap checksum to prevent DCE, lighter than hashing the whole bitmap every time.
    static inline std::uint64_t mix64(std::uint64_t x) {
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33; return x;
    }

    struct GlyphJob {
        int cp = 0;
        int glyph = 0;
        int w = 0, h = 0;
        int x0 = 0, y0 = 0; // for debug if needed
        std::vector<std::uint8_t> buf;
    };

    struct BenchResult {
        double ms_total = 0.0;

        std::size_t allocs = 0;
        std::size_t frees = 0;
        std::size_t req_bytes = 0;
        std::size_t peak_live = 0;

        bool ok_alloc = true;
        std::uint64_t checksum = 0;
    };

} // namespace stbtt_bench

// Bind allocator hooks for your port:
#define STBTT_malloc(sz,u) stbtt_bench::tt_malloc((sz),(u))
#define STBTT_free(p,u)    stbtt_bench::tt_free((p),(u))

// Your fork
#include "stbtt/stb_truetype.hpp"

// Reference stb (optional)
#ifndef STBTT_BENCH_NO_REFERENCE
#   define STB_TRUETYPE_IMPLEMENTATION
#   define STBTT_STATIC
#   include "stbtt/stb_truetype.h"
#endif

// ---------------- Prep + Bench (PORT) ----------------
static bool prep_port_jobs(stbtt::Font& tt,
    float px, float sx, float sy,
    std::vector<stbtt_bench::GlyphJob>& jobs)
{
    const float sc = tt.ScaleForPixelHeight(px);
    jobs.clear();
    jobs.reserve(95);

    for (int cp = 32; cp <= 126; ++cp) {
        int g = tt.FindGlyphIndex(cp);
        // still include missing glyphs (g==0) for fairness
        auto bb = tt.GetGlyphBitmapBox(g, sc, sc, sx, sy);
        int w = bb.x1 - bb.x0;
        int h = bb.y1 - bb.y0;
        if (w <= 0 || h <= 0) {
            // keep a job with empty buffer (still counts cost of lookup in loop if you want)
            stbtt_bench::GlyphJob j{};
            j.cp = cp; j.glyph = g; j.w = 0; j.h = 0; j.x0 = bb.x0; j.y0 = bb.y0;
            jobs.push_back(std::move(j));
            continue;
        }
        stbtt_bench::GlyphJob j{};
        j.cp = cp;
        j.glyph = g;
        j.w = w; j.h = h;
        j.x0 = bb.x0; j.y0 = bb.y0;
        j.buf.resize((std::size_t)w * (std::size_t)h);
        jobs.push_back(std::move(j));
    }
    return true;
}

static stbtt_bench::BenchResult bench_port_ascii(std::vector<std::uint8_t>& font_bytes,
    int warmup_iters, int iters,
    float px, float sx, float sy)
{
    stbtt_bench::AllocStats st{};
    stbtt::Font tt;
    tt.fi.userdata = &st;
    if (!tt.ReadBytes(font_bytes.data())) return {};

    std::vector<stbtt_bench::GlyphJob> jobs;
    prep_port_jobs(tt, px, sx, sy, jobs);

    // Warmup (not measured)
    for (int w = 0; w < warmup_iters; ++w) {
        for (auto& j : jobs) {
            if (j.w == 0) continue;
            std::memset(j.buf.data(), 0, j.buf.size());
            const float sc = tt.ScaleForPixelHeight(px);
            tt.MakeGlyphBitmap(j.buf.data(), j.glyph, j.w, j.h, j.w, sc, sc, sx, sy);
        }
    }

    // Reset allocator stats after warmup to measure steady-state allocations
    stbtt_bench::AllocStats st_meas{};
    tt.fi.userdata = &st_meas;

    // Measured
    volatile std::uint64_t sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; ++it) {
        const float sc = tt.ScaleForPixelHeight(px);
        for (auto& j : jobs) {
            if (j.w == 0) continue;
            std::memset(j.buf.data(), 0, j.buf.size());
            tt.MakeGlyphBitmap(j.buf.data(), j.glyph, j.w, j.h, j.w, sc, sc, sx, sy);

            // very light checksum: sample a few bytes
            std::uint64_t x = 0;
            const std::size_t n = j.buf.size();
            if (n) {
                x ^= j.buf[0];
                x ^= (std::uint64_t)j.buf[n >> 1] << 8;
                x ^= (std::uint64_t)j.buf[n - 1] << 16;
            }
            sink ^= stbtt_bench::mix64(x + (std::uint64_t)j.cp);
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    stbtt_bench::BenchResult r{};
    r.ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.allocs = st_meas.total_allocs;
    r.frees = st_meas.total_frees;
    r.req_bytes = st_meas.total_req_bytes;
    r.peak_live = st_meas.peak_live_bytes;
    r.ok_alloc = !st_meas.corrupt && st_meas.live_blocks == 0 && st_meas.live_bytes == 0;
    r.checksum = (std::uint64_t)sink;
    return r;
}

// ---------------- Prep + Bench (REF) ----------------
#ifndef STBTT_BENCH_NO_REFERENCE

static bool prep_ref_jobs(stbtt_fontinfo& ref, float px, float sx, float sy,
    std::vector<stbtt_bench::GlyphJob>& jobs)
{
    const float sc = stbtt_ScaleForPixelHeight(&ref, px);
    jobs.clear();
    jobs.reserve(95);

    for (int cp = 32; cp <= 126; ++cp) {
        int g = stbtt_FindGlyphIndex(&ref, cp);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&ref, g, sc, sc, sx, sy, &x0, &y0, &x1, &y1);
        int w = x1 - x0;
        int h = y1 - y0;

        stbtt_bench::GlyphJob j{};
        j.cp = cp; j.glyph = g; j.w = w; j.h = h; j.x0 = x0; j.y0 = y0;
        if (w > 0 && h > 0) j.buf.resize((std::size_t)w * (std::size_t)h);
        jobs.push_back(std::move(j));
    }
    return true;
}

static stbtt_bench::BenchResult bench_ref_ascii(std::vector<std::uint8_t>& font_bytes,
    int warmup_iters, int iters,
    float px, float sx, float sy)
{
    stbtt_bench::AllocStats st{};
    stbtt_fontinfo ref{};
    int off = stbtt_GetFontOffsetForIndex(font_bytes.data(), 0);
    if (off < 0) return {};
    if (!stbtt_InitFont(&ref, font_bytes.data(), off)) return {};

    std::vector<stbtt_bench::GlyphJob> jobs;
    prep_ref_jobs(ref, px, sx, sy, jobs);

    // Warmup (not measured)
    for (int w = 0; w < warmup_iters; ++w) {
        const float sc = stbtt_ScaleForPixelHeight(&ref, px);
        for (auto& j : jobs) {
            if (j.w <= 0 || j.h <= 0) continue;
            std::memset(j.buf.data(), 0, j.buf.size());
            stbtt_MakeGlyphBitmapSubpixel(&ref, j.buf.data(), j.w, j.h, j.w, sc, sc, sx, sy, j.glyph);
        }
    }

    // Reset allocator stats after warmup to measure steady-state allocations
    stbtt_bench::AllocStats st_meas{};
    ref.userdata = &st_meas;

    // Measured
    volatile std::uint64_t sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int it = 0; it < iters; ++it) {
        const float sc = stbtt_ScaleForPixelHeight(&ref, px);
        for (auto& j : jobs) {
            if (j.w <= 0 || j.h <= 0) continue;
            std::memset(j.buf.data(), 0, j.buf.size());
            stbtt_MakeGlyphBitmapSubpixel(&ref, j.buf.data(), j.w, j.h, j.w, sc, sc, sx, sy, j.glyph);

            std::uint64_t x = 0;
            const std::size_t n = j.buf.size();
            if (n) {
                x ^= j.buf[0];
                x ^= (std::uint64_t)j.buf[n >> 1] << 8;
                x ^= (std::uint64_t)j.buf[n - 1] << 16;
            }
            sink ^= stbtt_bench::mix64(x + (std::uint64_t)j.cp);
        }
    }
    auto t1 = std::chrono::steady_clock::now();

    stbtt_bench::BenchResult r{};
    r.ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.allocs = st_meas.total_allocs;
    r.frees = st_meas.total_frees;
    r.req_bytes = st_meas.total_req_bytes;
    r.peak_live = st_meas.peak_live_bytes;
    r.ok_alloc = !st_meas.corrupt && st_meas.live_blocks == 0 && st_meas.live_bytes == 0;
    r.checksum = (std::uint64_t)sink;
    return r;

}

#endif // ref

static void print_header() {
    using std::setw;
    using std::left;
    std::cout
        << left << setw(48) << "font"
        << left << setw(10) << "bytes"
        << left << setw(12) << "port_ms"
        << left << setw(12) << "ref_ms"

        << left << setw(12) << "p_alloc"
        << left << setw(12) << "p_free"
        << left << setw(14) << "p_req_bytes"
        << left << setw(14) << "p_peak_live"

        << left << setw(12) << "r_alloc"
        << left << setw(12) << "r_free"
        << left << setw(14) << "r_req_bytes"
        << left << setw(14) << "r_peak_live"

        << left << setw(10) << "match"
        << "\n";
}

static void print_requested_bytes(double glyphs,
            stbtt_bench::BenchResult port,
            stbtt_bench::BenchResult ref) {
    using std::setw;
    using std::left;

    const double p_alloc_per_glyph = port.allocs / glyphs;
    const double p_avg = port.allocs ? double(port.req_bytes) / double(port.allocs) : 0.0;

#ifndef STBTT_BENCH_NO_REFERENCE
    const double r_alloc_per_glyph = ref.allocs / glyphs;
    const double r_avg = ref.allocs ? double(ref.req_bytes) / double(ref.allocs) : 0.0;
#endif

    std::cout
        << left << setw(14) << "p_per_glyph"
        << left << setw(14) << "p_avg"
#ifndef STBTT_BENCH_NO_REFERENCE
        << left << setw(14) << "r_per_glyph"
        << left << setw(14) << "r_avg"
#endif
        << "\n";

    std::cout
        << left << setw(14) << p_alloc_per_glyph
        << left << setw(14) << p_avg
#ifndef STBTT_BENCH_NO_REFERENCE
        << left << setw(14) << r_alloc_per_glyph
        << left << setw(14) << r_avg
#endif
        << "\n";
}


int main() {
    set_high_perf_timer();

    const int iters = stbtt_bench::getenv_int("STBTT_BENCH_ITERS", 10000);
    const int warmup = stbtt_bench::getenv_int("STBTT_BENCH_WARMUP", std::max(10, iters / 20));
    const float px = 32.0f;
    const float sx = 0.25f;
    const float sy = 0.25f;
    const double glyphs = double(iters) * 95.0;

    auto paths = stbtt_bench::collect_font_paths();
    std::cout << "Fonts candidates: " << paths.size() << "\n";
    std::cout << "Warmup passes:    " << warmup << " (each pass renders ASCII 32..126)\n";
    std::cout << "Measured passes:  " << iters << " (each pass renders ASCII 32..126)\n\n";

    print_header();

    for (const auto& path : paths) {
        std::vector<std::uint8_t> bytes;
        if (!stbtt_bench::read_file(path, bytes)) continue;

        auto port = bench_port_ascii(bytes, warmup, iters, px, sx, sy);   

#ifndef STBTT_BENCH_NO_REFERENCE
        auto ref = bench_ref_ascii(bytes, warmup, iters, px, sx, sy);
        const bool match = (ref.ms_total == 0.0) ? true : (port.checksum == ref.checksum);
        const bool ok = match && port.ok_alloc && ref.ok_alloc;   
#else
        const bool ok = port.ok_alloc;
#endif

        // fixed-width rows (truncate very long path for neatness)
        std::string shown = path;
        if (shown.size() > 47) {
            shown = "..." + shown.substr(shown.size() - 44);
        }

        print_requested_bytes(glyphs, port, ref);

        std::cout
            << std::left << std::setw(48) << shown
            << std::left << std::setw(10) << bytes.size()
            << std::left << std::setw(12) << port.ms_total
#ifndef STBTT_BENCH_NO_REFERENCE
            << std::left << std::setw(12) << ref.ms_total
#else
            << std::left << std::setw(12) << 0.0
#endif

            << std::left << std::setw(12) << port.allocs
            << std::left << std::setw(12) << port.frees
            << std::left << std::setw(14) << port.req_bytes
            << std::left << std::setw(14) << port.peak_live

#ifndef STBTT_BENCH_NO_REFERENCE
            << std::left << std::setw(12) << ref.allocs
            << std::left << std::setw(12) << ref.frees
            << std::left << std::setw(14) << ref.req_bytes
            << std::left << std::setw(14) << ref.peak_live
#else
            << std::left << std::setw(12) << 0
            << std::left << std::setw(12) << 0
            << std::left << std::setw(14) << 0
            << std::left << std::setw(14) << 0
#endif

            << std::left << std::setw(10) << (ok ? "yes" : "no")
            << "\n";

    }

    return 0;
}
