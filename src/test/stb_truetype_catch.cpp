// test_stb_truetype_catch2_single.hpp
// Compile as a single TU. Requires Catch2 single-header + your stb_truetype.hpp.
// Optional: add original stb_truetype.h (v1.26) for strict byte-to-byte comparison.
//
// ENV:
//  - STBTT_TEST_FONT   : path to a primary .ttf/.otf
//  - STBTT_TEST_FONTS  : additional paths (separator ';' or ':')
//  - STBTT_TEST_TTC    : optional .ttc path
//  - STBTT_TEST_CFF    : optional .otf/.ttf with CFF(Type2) outlines (to stress cubic path)
//
// Define this to enable reference comparison:
#define STBTT_TEST_WITH_REFERENCE 1
//
// Notes:
//  - These tests assume "trusted fonts" (same as stb). We avoid crafting malicious inputs.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>

// ---------------- Catch2 single header include ----------------
#if __has_include(<catch2/catch_all.hpp>)
#  define CATCH_CONFIG_MAIN
#  include <catch2/catch_all.hpp>
#elif __has_include(<catch.hpp>)
#  define CATCH_CONFIG_MAIN
#  include <catch.hpp>
#elif __has_include("catch.hpp")
#  define CATCH_CONFIG_MAIN
#  include "catch.hpp"
#else
#  error "Catch2 single-header not found. Provide <catch2/catch_all.hpp> or catch.hpp."
#endif

// ---------------- Canary allocator for STBTT_malloc/free ----------------
namespace stbtt_test {

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
        std::size_t   size;
        void* base;   // pointer returned by ::malloc
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

        // layout: [base ... padding ... header][user bytes][tail u64]
        const std::size_t A = alignof(std::max_align_t);
        const std::size_t header_sz = sizeof(BlockHeader);
        const std::size_t tail_sz = sizeof(std::uint64_t);

        std::size_t total = header_sz + sz + tail_sz + A; // A for alignment slack
        void* base = std::malloc(total);
        if (!base) return nullptr;

        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(base);
        std::uintptr_t user = align_up(p + header_sz, A);
        auto* h = reinterpret_cast<BlockHeader*>(user - header_sz);

        h->magic = kMagic;
        h->size = sz;
        h->base = base;

        // write tail
        auto* tailp = reinterpret_cast<std::uint64_t*>(user + sz);
        *tailp = kTail;

        if (st) {
            st->live_blocks++;
            st->live_bytes += sz;
            st->total_allocs++;
        }

        // Fill user memory with a pattern to help detect uninitialized reads in debugging
        std::memset(reinterpret_cast<void*>(user), 0xCD, sz);

        return reinterpret_cast<void*>(user);
    }

    static void tt_free(void* ptr, void* userdata) {
        if (!ptr) return;
        auto* st = reinterpret_cast<AllocStats*>(userdata);

        std::uintptr_t user = reinterpret_cast<std::uintptr_t>(ptr);
        auto* h = reinterpret_cast<BlockHeader*>(user - sizeof(BlockHeader));

        if (h->magic != kMagic) {
            mark_corrupt(st, "Header magic mismatch (double free / underrun / foreign pointer).");
            // Avoid UB: cannot trust base
            return;
        }

        auto* tailp = reinterpret_cast<std::uint64_t*>(user + h->size);
        if (*tailp != kTail) {
            mark_corrupt(st, "Tail canary mismatch (buffer overrun).");
            // Still attempt to free base to avoid leaks in tests
        }

        if (st) {
            if (st->live_blocks == 0) mark_corrupt(st, "live_blocks underflow (double free).");
            else st->live_blocks--;

            if (st->live_bytes < h->size) mark_corrupt(st, "live_bytes underflow.");
            else st->live_bytes -= h->size;

            st->total_frees++;
        }

        // poison memory
        std::memset(reinterpret_cast<void*>(user), 0xDD, h->size);

        void* base = h->base;
        h->magic = 0; // invalidate
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
            else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) out.push_back(cur);
        // trim spaces
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
        out.resize(static_cast<std::size_t>(n));
        f.read(reinterpret_cast<char*>(out.data()), n);
        return f.good();
    }

    static std::vector<std::string> default_font_candidates() {
        // Try to pick “normal” fonts first (avoid symbol fonts).
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

        // add defaults at end
        auto defs = default_font_candidates();
        paths.insert(paths.end(), defs.begin(), defs.end());

        // dedupe
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

        return paths;
    }

    static std::string first_existing_font_path() {
        std::vector<std::uint8_t> tmp;
        for (auto& p : collect_font_paths()) {
            if (read_file(p, tmp)) return p;
        }
        return {};
    }

    static std::uint64_t fnv1a64(const void* data, std::size_t n) {
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(data);
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
        return h;
    }

    static void require_no_leaks_and_no_corruption(const AllocStats& st) {
        REQUIRE(st.corrupt == false);
        if (st.corrupt) {
            INFO("Allocator corruption reason: " << (st.corrupt_reason ? st.corrupt_reason : "(unknown)"));
        }
        REQUIRE(st.live_blocks == 0);
        REQUIRE(st.live_bytes == 0);
    }

} // namespace stbtt_test

// ---------------- Bind STBTT_malloc/free to our tracker ----------------
#define STBTT_malloc(sz, u) stbtt_test::tt_malloc((sz), (u))
#define STBTT_free(p, u)    stbtt_test::tt_free((p), (u))

// ---------------- C++ port ----------------
#include "stb_truetype.hpp"

// ---------------- reference stb_truetype.h ----------------
#if defined(STBTT_TEST_WITH_REFERENCE) && STBTT_TEST_WITH_REFERENCE
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#endif

// =====================================================================================
//                                       TESTS
// =====================================================================================

TEST_CASE("stb::TrueType - can locate a usable font file (or skip with help)", "[stbtt][setup]") {
    auto path = stbtt_test::first_existing_font_path();
    if (path.empty()) {
        FAIL("No font found. Set STBTT_TEST_FONT=/path/to/font.ttf (or STBTT_TEST_FONTS=...;...).");
    }
    SUCCEED("Found font: " + path);
}

TEST_CASE("stb::TrueType::GetNumberOfFonts/GetFontOffsetForIndex - TTC header parsing (synthetic)", "[stbtt][ttc]") {
    // Minimal TTC header:
    // 'ttcf' + version(0x00010000) + numFonts(2) + offsets[0]=0x20 offsets[1]=0x40
    std::uint8_t ttc[32]{};
    ttc[0] = 't'; ttc[1] = 't'; ttc[2] = 'c'; ttc[3] = 'f';
    ttc[4] = 0x00; ttc[5] = 0x01; ttc[6] = 0x00; ttc[7] = 0x00;
    ttc[8] = 0x00; ttc[9] = 0x00; ttc[10] = 0x00; ttc[11] = 0x02;
    ttc[12] = 0x00; ttc[13] = 0x00; ttc[14] = 0x00; ttc[15] = 0x20;
    ttc[16] = 0x00; ttc[17] = 0x00; ttc[18] = 0x00; ttc[19] = 0x40;

    REQUIRE(stb::TrueType::GetNumberOfFonts(ttc) == 2);
    REQUIRE(stb::TrueType::GetFontOffsetForIndex(ttc, 0) == 0x20);
    REQUIRE(stb::TrueType::GetFontOffsetForIndex(ttc, 1) == 0x40);
    REQUIRE(stb::TrueType::GetFontOffsetForIndex(ttc, 2) == -1);

#if defined(STBTT_TEST_WITH_REFERENCE) && STBTT_TEST_WITH_REFERENCE
    REQUIRE(stbtt_GetNumberOfFonts(ttc) == 2);
    REQUIRE(stbtt_GetFontOffsetForIndex(ttc, 0) == 0x20);
    REQUIRE(stbtt_GetFontOffsetForIndex(ttc, 1) == 0x40);
    REQUIRE(stbtt_GetFontOffsetForIndex(ttc, 2) == -1);
#endif
}

TEST_CASE("stb::TrueType - ReadBytes + basic invariants", "[stbtt][basic]") {
    auto path = stbtt_test::first_existing_font_path();
    if (path.empty()) FAIL("No font found. Set STBTT_TEST_FONT.");

    stbtt_test::AllocStats st{};
    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(path, bytes));
    REQUIRE(bytes.size() > 16);

    stb::TrueType tt;
    tt.fi.userdata = &st;

    REQUIRE(tt.ReadBytes(bytes.data()) == true);

    // Scale should be positive and monotonic
    float s12 = tt.ScaleForPixelHeight(12.0f);
    float s24 = tt.ScaleForPixelHeight(24.0f);
    REQUIRE(s12 > 0.0f);
    REQUIRE(s24 > s12);

    // A few common codepoints (these should exist for “normal” fonts)
    const int cps[] = { 'A', 'B', 'a', 'b', '0', '1', '.', ',', ' ' };
    for (int cp : cps) {
        int g = tt.FindGlyphIndex(cp);
        // ' ' may map to glyph 0 in some fonts, so be slightly softer there
        if (cp != ' ') {
            REQUIRE(g > 0);
        }
        else {
            REQUIRE(g >= 0);
        }
        REQUIRE(g < tt.fi.num_glyphs);
    }

    // Metrics sanity for 'A'
    int gA = tt.FindGlyphIndex('A');
    auto hm = tt.GetGlyphHorMetrics(gA);
    REQUIRE(hm.advance > 0);
    // lsb can be negative; just sanity check for not-crazy values
    REQUIRE(hm.lsb > -10000);
    REQUIRE(hm.lsb < 10000);

    // Box sanity for 'A'
    stb::Box box{};
    REQUIRE(tt.GetGlyphBox(gA, box) == true);
    REQUIRE(box.x1 > box.x0);
    REQUIRE(box.y1 > box.y0);

    // Bitmap box sanity (scale 48px)
    float sc = tt.ScaleForPixelHeight(48.0f);
    auto bb = tt.GetGlyphBitmapBox(gA, sc, sc, 0.0f, 0.0f);
    REQUIRE(bb.x1 >= bb.x0);
    REQUIRE(bb.y1 >= bb.y0);

    // Rendering should produce non-empty bitmap for 'A' at 48px
    int w = bb.x1 - bb.x0;
    int h = bb.y1 - bb.y0;
    REQUIRE(w >= 0);
    REQUIRE(h >= 0);
    if (w == 0 || h == 0) {
        // extremely unlikely for 'A' in a normal font, but keep test stable
        FAIL("Glyph bitmap box produced empty bitmap for 'A' (font is unusual).");
    }

    std::vector<std::uint8_t> bmp(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    tt.MakeGlyphBitmap(bmp.data(), gA, w, h, w, sc, sc, 0.0f, 0.0f);

    std::uint64_t hsh = stbtt_test::fnv1a64(bmp.data(), bmp.size());
    REQUIRE(hsh != 0); // trivial guard (not a correctness oracle)
    // also ensure not all zeros
    std::size_t sum = 0;
    for (auto v : bmp) sum += v;
    REQUIRE(sum > 0);

    stbtt_test::require_no_leaks_and_no_corruption(st);
}

TEST_CASE("stb::TrueType - shifts/subpixel: bitmap dimensions stable-ish", "[stbtt][subpixel]") {
    auto path = stbtt_test::first_existing_font_path();
    if (path.empty()) FAIL("No font found. Set STBTT_TEST_FONT.");

    stbtt_test::AllocStats st{};
    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(path, bytes));

    stb::TrueType tt;
    tt.fi.userdata = &st;
    REQUIRE(tt.ReadBytes(bytes.data()));

    int g = tt.FindGlyphIndex('A');
    float sc = tt.ScaleForPixelHeight(32.0f);

    auto b0 = tt.GetGlyphBitmapBox(g, sc, sc, 0.00f, 0.00f);
    auto b1 = tt.GetGlyphBitmapBox(g, sc, sc, 0.25f, 0.00f);
    auto b2 = tt.GetGlyphBitmapBox(g, sc, sc, 0.50f, 0.00f);

    auto w0 = b0.x1 - b0.x0; auto h0 = b0.y1 - b0.y0;
    auto w1 = b1.x1 - b1.x0; auto h1 = b1.y1 - b1.y0;
    auto w2 = b2.x1 - b2.x0; auto h2 = b2.y1 - b2.y0;

    // shifts may change rounding by ~1 px, but should not explode
    REQUIRE(std::abs(w1 - w0) <= 2);
    REQUIRE(std::abs(h1 - h0) <= 2);
    REQUIRE(std::abs(w2 - w0) <= 2);
    REQUIRE(std::abs(h2 - h0) <= 2);

    stbtt_test::require_no_leaks_and_no_corruption(st);
}

#if defined(STBTT_TEST_WITH_REFERENCE) && STBTT_TEST_WITH_REFERENCE

static void require_same_box(const stb::Box& a, const stb::Box& b) {
    REQUIRE(a.x0 == b.x0);
    REQUIRE(a.y0 == b.y0);
    REQUIRE(a.x1 == b.x1);
    REQUIRE(a.y1 == b.y1);
}

TEST_CASE("Reference compare - Init/Scale/GlyphIndex/HMetrics/Boxes match stb_truetype.h", "[stbtt][ref][metrics]") {
    auto path = stbtt_test::first_existing_font_path();
    if (path.empty()) FAIL("No font found. Set STBTT_TEST_FONT.");

    stbtt_test::AllocStats st{};
    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(path, bytes));

    // Your port
    stb::TrueType tt;
    tt.fi.userdata = &st;
    REQUIRE(tt.ReadBytes(bytes.data()));

    // Reference
    stbtt_fontinfo ref{};
    int off = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    REQUIRE(off >= 0);
    REQUIRE(stbtt_InitFont(&ref, bytes.data(), off) == 1);

    // Compare a few sizes
    for (float px : { 12.0f, 24.0f, 48.0f }) {
        float sA = tt.ScaleForPixelHeight(px);
        float sB = stbtt_ScaleForPixelHeight(&ref, px);
        REQUIRE(sA == Approx(sB).epsilon(0.0f).margin(1e-7f)); // extremely strict
    }

    // Compare many codepoints (ASCII range)
    for (int cp = 32; cp < 128; ++cp) {
        int gA = tt.FindGlyphIndex(cp);
        int gB = stbtt_FindGlyphIndex(&ref, cp);
        REQUIRE(gA == gB);
    }

    // Compare hmetrics & glyph box for selected cps
    for (int cp : { 'A', 'B', 'C', 'a', 'b', 'c', '0', '1', '2', '!', '?', '@', '#' }) {
        int gA = tt.FindGlyphIndex(cp);
        int gB = stbtt_FindGlyphIndex(&ref, cp);
        REQUIRE(gA == gB);

        auto hm = tt.GetGlyphHorMetrics(gA);
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&ref, gB, &adv, &lsb);
        REQUIRE(hm.advance == adv);
        REQUIRE(hm.lsb == lsb);

        stb::Box boxA{};
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool okA = tt.GetGlyphBox(gA, boxA);
        int okB = stbtt_GetGlyphBox(&ref, gB, &x0, &y0, &x1, &y1);
        REQUIRE((okA ? 1 : 0) == okB);
        if (okA && okB) {
            REQUIRE(boxA.x0 == x0);
            REQUIRE(boxA.y0 == y0);
            REQUIRE(boxA.x1 == x1);
            REQUIRE(boxA.y1 == y1);
        }
    }

    stbtt_test::require_no_leaks_and_no_corruption(st);
}

TEST_CASE("Reference compare - GetGlyphBitmapBox + MakeGlyphBitmap are byte-identical", "[stbtt][ref][raster]") {
    auto path = stbtt_test::first_existing_font_path();
    if (path.empty()) FAIL("No font found. Set STBTT_TEST_FONT.");

    stbtt_test::AllocStats st{};
    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(path, bytes));

    stb::TrueType tt;
    tt.fi.userdata = &st;
    REQUIRE(tt.ReadBytes(bytes.data()));

    stbtt_fontinfo ref{};
    int off = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    REQUIRE(off >= 0);
    REQUIRE(stbtt_InitFont(&ref, bytes.data(), off) == 1);

    struct Case { int cp; float px; float sx; float sy; };
    std::vector<Case> cases = {
        { 'A', 16.f, 0.00f, 0.00f },
        { 'A', 16.f, 0.25f, 0.00f },
        { 'A', 32.f, 0.00f, 0.00f },
        { 'B', 32.f, 0.50f, 0.25f },
        { 'a', 48.f, 0.00f, 0.00f },
        { 'g', 48.f, 0.25f, 0.50f },
        { '0', 64.f, 0.00f, 0.00f },
        { '@', 64.f, 0.50f, 0.00f },
    };

    for (auto c : cases) {
        int g = tt.FindGlyphIndex(c.cp);
        REQUIRE(g >= 0);

        float scaleA = tt.ScaleForPixelHeight(c.px);
        float scaleB = stbtt_ScaleForPixelHeight(&ref, c.px);

        // Bitmap box (subpixel)
        stb::Box ba = tt.GetGlyphBitmapBox(g, scaleA, scaleA, c.sx, c.sy);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&ref, g, scaleB, scaleB, c.sx, c.sy, &x0, &y0, &x1, &y1);

        stb::Box bb{}; bb.x0 = x0; bb.y0 = y0; bb.x1 = x1; bb.y1 = y1;
        require_same_box(ba, bb);

        int w = ba.x1 - ba.x0;
        int h = ba.y1 - ba.y0;
        if (w <= 0 || h <= 0) continue;

        std::vector<std::uint8_t> outA(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        std::vector<std::uint8_t> outB(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);

        tt.MakeGlyphBitmap(outA.data(), g, w, h, w, scaleA, scaleA, c.sx, c.sy);
        stbtt_MakeGlyphBitmapSubpixel(&ref, outB.data(), w, h, w, scaleB, scaleB, c.sx, c.sy, g);

        // Strict byte compare
        REQUIRE(outA == outB);
    }

    stbtt_test::require_no_leaks_and_no_corruption(st);
}

TEST_CASE("Reference compare - randomized ASCII corpus stays identical", "[stbtt][ref][fuzzlite]") {
    auto path = stbtt_test::first_existing_font_path();
    if (path.empty()) FAIL("No font found. Set STBTT_TEST_FONT.");

    stbtt_test::AllocStats st{};
    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(path, bytes));

    stb::TrueType tt;
    tt.fi.userdata = &st;
    REQUIRE(tt.ReadBytes(bytes.data()));

    stbtt_fontinfo ref{};
    int off = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
    REQUIRE(off >= 0);
    REQUIRE(stbtt_InitFont(&ref, bytes.data(), off) == 1);

    // deterministic pseudo-random
    std::uint32_t seed = 0x12345678u;
    auto rnd = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };

    for (int i = 0; i < 64; ++i) {
        int cp = 32 + (rnd() % 96); // [32..127)
        float px = 10.f + float((rnd() % 60)); // [10..69]
        float shx = (rnd() % 4) * 0.25f;
        float shy = (rnd() % 4) * 0.25f;

        int gA = tt.FindGlyphIndex(cp);
        int gB = stbtt_FindGlyphIndex(&ref, cp);
        REQUIRE(gA == gB);

        float sA = tt.ScaleForPixelHeight(px);
        float sB = stbtt_ScaleForPixelHeight(&ref, px);

        stb::Box ba = tt.GetGlyphBitmapBox(gA, sA, sA, shx, shy);

        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&ref, gB, sB, sB, shx, shy, &x0, &y0, &x1, &y1);

        REQUIRE(ba.x0 == x0); REQUIRE(ba.y0 == y0);
        REQUIRE(ba.x1 == x1); REQUIRE(ba.y1 == y1);

        int w = ba.x1 - ba.x0;
        int h = ba.y1 - ba.y0;
        if (w <= 0 || h <= 0) continue;

        std::vector<std::uint8_t> outA(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        std::vector<std::uint8_t> outB(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);

        tt.MakeGlyphBitmap(outA.data(), gA, w, h, w, sA, sA, shx, shy);
        stbtt_MakeGlyphBitmapSubpixel(&ref, outB.data(), w, h, w, sB, sB, shx, shy, gB);

        REQUIRE(outA == outB);
    }

    stbtt_test::require_no_leaks_and_no_corruption(st);
}

#endif // STBTT_TEST_WITH_REFERENCE

TEST_CASE("Optional - TTC real file sanity (if provided)", "[stbtt][ttc][optional]") {
    auto ttc_path = stbtt_test::getenv_str("STBTT_TEST_TTC");
    if (ttc_path.empty()) FAIL("Set STBTT_TEST_TTC=/path/to/font.ttc to enable.");

    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(ttc_path, bytes));
    REQUIRE(bytes.size() > 16);

    int n = stb::TrueType::GetNumberOfFonts(bytes.data());
    REQUIRE(n >= 1);

    for (int i = 0; i < std::min(n, 4); ++i) {
        int off = stb::TrueType::GetFontOffsetForIndex(bytes.data(), i);
        REQUIRE(off >= 0);
        REQUIRE(static_cast<std::size_t>(off) < bytes.size());
    }

#if defined(STBTT_TEST_WITH_REFERENCE) && STBTT_TEST_WITH_REFERENCE
    REQUIRE(stbtt_GetNumberOfFonts(bytes.data()) == n);
    for (int i = 0; i < std::min(n, 4); ++i) {
        REQUIRE(stbtt_GetFontOffsetForIndex(bytes.data(), i) ==
            stb::TrueType::GetFontOffsetForIndex(bytes.data(), i));
    }
#endif
}

TEST_CASE("Optional - CFF(Type2)/cubic stress (if provided)", "[stbtt][cff][optional]") {
    auto cff_path = stbtt_test::getenv_str("STBTT_TEST_CFF");
    if (cff_path.empty()) FAIL("Set STBTT_TEST_CFF=/path/to/cff-font.otf to enable cubic tests.");

    stbtt_test::AllocStats st{};
    std::vector<std::uint8_t> bytes;
    REQUIRE(stbtt_test::read_file(cff_path, bytes));
    REQUIRE(bytes.size() > 16);

    stb::TrueType tt;
    tt.fi.userdata = &st;
    REQUIRE(tt.ReadBytes(bytes.data()));

    // Stress a bunch of codepoints; CFF fonts often have Latin coverage
    std::vector<int> cps = { 'A','B','C','a','b','c','g','@','&','?','1','2','3' };
    float sc = tt.ScaleForPixelHeight(48.f);

    for (int cp : cps) {
        int g = tt.FindGlyphIndex(cp);
        REQUIRE(g >= 0);

        auto bb = tt.GetGlyphBitmapBox(g, sc, sc, 0.25f, 0.25f);
        int w = bb.x1 - bb.x0;
        int h = bb.y1 - bb.y0;
        if (w <= 0 || h <= 0) continue;

        std::vector<std::uint8_t> bmp(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        tt.MakeGlyphBitmap(bmp.data(), g, w, h, w, sc, sc, 0.25f, 0.25f);

        // Ensure not all zeros for a “real” glyph
        std::size_t sum = 0;
        for (auto v : bmp) sum += v;
        if (cp != ' ') REQUIRE(sum > 0);
    }

    stbtt_test::require_no_leaks_and_no_corruption(st);
}

TEST_CASE("GetNumberOfFonts fuzz-lite (safe: only reads header bytes)", "[stbtt][fuzzlite]") {
    // This is intentionally safe: only calls functions that read a small header.
    std::uint32_t seed = 0xA5A5A5A5u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    std::vector<std::uint8_t> buf(64);

    for (int i = 0; i < 2000; ++i) {
        for (auto& b : buf) b = static_cast<std::uint8_t>(rnd() & 0xFF);

        (void)stb::TrueType::GetNumberOfFonts(buf.data());
        (void)stb::TrueType::GetFontOffsetForIndex(buf.data(), int(rnd() % 4));

#if defined(STBTT_TEST_WITH_REFERENCE) && STBTT_TEST_WITH_REFERENCE
        (void)stbtt_GetNumberOfFonts(buf.data());
        (void)stbtt_GetFontOffsetForIndex(buf.data(), int(rnd() % 4));
#endif
    }

    SUCCEED("No crash in fuzz-lite.");
}
