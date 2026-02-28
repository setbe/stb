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
*/


// =======================================================================
//
//    NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT FILES
//
// This library does no range checking of the offsets found in the file,
// meaning an attacker can use it to read arbitrary memory.
//
// =======================================================================

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

#ifndef assert
#   define assert
#endif

#ifndef STBTT_STREAM_MAX_XS
#   define STBTT_STREAM_MAX_XS 256
#endif // STBTT_STREAM_MAX_XS

#ifndef STBTT_STREAM_VISIT_CAP
#   define STBTT_STREAM_VISIT_CAP 512
#endif // STBTT_STREAM_VISIT_CAP

namespace stbtt_stream {
enum class DfMode : uint8_t { SDF=1, MSDF=3, MTSDF=4 };
enum : uint8_t { EDGE_R, EDGE_G, EDGE_B };

// some of the values for the IDs are below; for more see the truetype spec:
// Apple (archive) https://web.archive.org/web/20090113004145/http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
// Microsoft (archive) https://web.archive.org/web/20090213110553/http://www.microsoft.com/typography/otspec/name.htm
enum class PlatformId {
    Unicode = 0,
    Mac = 1,
    Iso = 2,
    Microsoft = 3
};
enum class EncodingIdMicrosoft {
    Symbol = 0,
    Unicode_Bmp = 1,
    ShiftJis = 2,
    Unicode_Full = 10
};

struct SkylineNode {
    uint16_t x;
    uint16_t y;
    uint16_t w;
};
struct Skyline {
    SkylineNode* nodes;
    int node_count;
    int node_cap;
    uint16_t width;  // atlas side
};
static inline void skyline_init(Skyline& s, uint16_t width, SkylineNode* nodes, int cap) noexcept {
    s.nodes = nodes;
    s.node_cap = cap;
    s.width = width;
    s.node_count = 1;
    s.nodes[0] = SkylineNode{ 0, 0, width };
}
static inline void skyline_merge(Skyline& s) noexcept {
    for (int i = 0; i < s.node_count - 1; ) {
        if (s.nodes[i].y == s.nodes[i + 1].y) {
            s.nodes[i].w = (uint16_t)(s.nodes[i].w + s.nodes[i + 1].w);
            for (int j = i + 1; j < s.node_count - 1; ++j) s.nodes[j] = s.nodes[j + 1];
            --s.node_count;
        }
        else {
            ++i;
        }
    }
}
// returns y if fits, else 0xFFFF
static inline uint16_t skyline_fit(const Skyline& s, int idx, uint16_t rw, uint16_t rh) noexcept {
    uint16_t x = s.nodes[idx].x;
    if ((uint32_t)x + rw > s.width) return 0xFFFF;

    uint16_t y = s.nodes[idx].y;
    uint16_t width_left = rw;

    int i = idx;
    while (width_left > 0) {
        if (i >= s.node_count) return 0xFFFF;
        if (s.nodes[i].y > y) y = s.nodes[i].y;

        if ((uint32_t)y + rh > s.width) return 0xFFFF; // defensive

        if (s.nodes[i].w >= width_left) break;
        width_left = (uint16_t)(width_left - s.nodes[i].w);
        ++i;
    }
    return y;
}
static inline bool skyline_insert(Skyline& s, uint16_t rw, uint16_t rh, uint16_t& out_x, uint16_t& out_y) noexcept {
    int best_idx = -1;
    uint16_t best_y = 0xFFFF;
    uint16_t best_w = 0xFFFF;

    for (int i = 0; i < s.node_count; ++i) {
        uint16_t y = skyline_fit(s, i, rw, rh);
        if (y == 0xFFFF)
            continue;
        // heuristic: minimal y, then minimal width
        if (y < best_y || (y == best_y && s.nodes[i].w < best_w)) {
            best_y = y;
            best_idx = i;
            best_w = s.nodes[i].w;
        }
    }
    if (best_idx < 0) return false;
    if ((uint32_t)best_y + rh > s.width) return false;

    out_x = s.nodes[best_idx].x;
    out_y = best_y;

    // add new node
    if (s.node_count + 1 > s.node_cap) return false;

    SkylineNode newn;
    newn.x = out_x;
    newn.y = (uint16_t)(best_y + rh);
    newn.w = rw;

    // insert node at best_idx
    for (int i = s.node_count; i > best_idx; --i) s.nodes[i] = s.nodes[i - 1];
    s.nodes[best_idx] = newn;
    ++s.node_count;

    // shrink/erase covered nodes to the right
    for (int i = best_idx + 1; i < s.node_count; ) {
        SkylineNode& n = s.nodes[i];
        uint16_t prev_x = s.nodes[i - 1].x;
        uint16_t prev_w = s.nodes[i - 1].w;
        uint16_t end_x = (uint16_t)(prev_x + prev_w);

        if (n.x < end_x) {
            uint16_t shrink = (uint16_t)(end_x - n.x);
            if (shrink >= n.w) {
                // remove node i
                for (int j = i; j < s.node_count - 1; ++j) s.nodes[j] = s.nodes[j + 1];
                --s.node_count;
                continue;
            }
            else {
                n.x = end_x;
                n.w = (uint16_t)(n.w - shrink);
            }
        }
        break;
    }

    skyline_merge(s);
    return true;
}

struct PlanResult {
    bool ok;
    uint32_t planned;
    uint16_t atlas_side;
    uint16_t max_points;
    uint16_t max_w;
    uint16_t max_h;
    uint32_t max_area;
};
struct GlyphHorMetrics {
    int advance;
    int lsb; // left side bearing
};
struct GlyphRect {
    uint16_t x, y;   // in atlas pixels
    uint16_t w, h;   // in atlas pixels (no padding)
};
struct GlyphPlan {
    uint32_t codepoint;
    uint16_t glyph_index;

    // glyf bbox in font units (int16 from glyf header)
    int16_t x_min, y_min, x_max, y_max;

    GlyphRect rect;     // packed placement
    uint16_t num_points;// for scratch validation (simple glyph)
}; // struct GlyphPlan
struct GlyphPlanInfo {
    int16_t x_min, y_min, x_max, y_max;
    uint16_t max_points_in_tree; // max num_points among simple subglyphs
    bool is_empty;
}; // struct GlyphPlanInfo

// Intermediate per-glyph scratch buffer (memory provided by the caller).
struct GlyphScratch {
    // NOTE:
    // The constants below define *upper bounds* for temporary buffers.
    // They can be reduced if memory is extremely constrained.
    //
    // In practice, the default values consume ~100 KB of scratch memory,
    // which is perfectly acceptable on desktop-class systems.
    // Tuning these down only makes sense for embedded or very low-memory targets.

    // Maximum number of scanline intersections per glyph.
    // Must be large enough for complex outlines.
    static constexpr uint16_t MAX_XS = STBTT_STREAM_MAX_XS;

    // Maximum number of visited glyphs during composite glyph traversal.
    // Acts as a recursion / cycle guard.
    static constexpr uint16_t VISIT_CAP = STBTT_STREAM_VISIT_CAP;

    // Per-point data (sized by max_points)
    uint8_t* flags;   // On/off-curve flags
    int16_t* px;      // X coordinates (font units)
    int16_t* py;      // Y coordinates (font units)

    // Per-pixel data (sized by max_area)
    uint16_t* min_d2;  // Minimum squared distance accumulator
    uint8_t* inside; // Inside/outside classification mask
    float* xs;     // X-intersections for scanline tests

    // Composite glyph traversal guard
    uint16_t* visit;  // Stack / set of visited glyph indices
    uint16_t  visit_n;
};
static inline uint16_t* scratch_d2_r(const GlyphScratch& s) noexcept { return s.min_d2; }
static inline uint16_t* scratch_d2_g(const GlyphScratch& s, uint32_t max_area) noexcept { return s.min_d2 + max_area; }
static inline uint16_t* scratch_d2_b(const GlyphScratch& s, uint32_t max_area) noexcept { return s.min_d2 + max_area * 2; }
static inline uint16_t* scratch_d2_a(const GlyphScratch& s, uint32_t max_area) noexcept { return s.min_d2 + max_area * 3; }
// fill as form all the fields
struct PlanInput {
    DfMode   mode;
    uint16_t pixel_height;   // <=64
    float    spread_px;
    // codepoints source:
    const uint32_t* codepoints;
    uint32_t        codepoint_count;
};
struct Xform {
    // [ m00 m01 dx ]
    // [ m10 m11 dy ]
    float m00{1.f}, m01{0.f};
    float m10{0.f}, m11{1.f};
    float dx{0.f}, dy{0.f};

    static inline Xform identity() noexcept { return{}; }
};
static inline void xform_apply(const Xform& xf, float x, float y, float& ox, float& oy) noexcept {
    ox = xf.m00 * x + xf.m01 * y + xf.dx;
    oy = xf.m10 * x + xf.m11 * y + xf.dy;
}
// child = parent ∘ local  (apply local, then parent)
static inline Xform xform_compose(const Xform& parent, const Xform& local) noexcept {
    Xform r;
    r.m00 = parent.m00 * local.m00 + parent.m01 * local.m10;
    r.m01 = parent.m00 * local.m01 + parent.m01 * local.m11;
    r.m10 = parent.m10 * local.m00 + parent.m11 * local.m10;
    r.m11 = parent.m10 * local.m01 + parent.m11 * local.m11;

    r.dx = parent.m00 * local.dx + parent.m01 * local.dy + parent.dx;
    r.dy = parent.m10 * local.dx + parent.m11 * local.dy + parent.dy;
    return r;
}
// A "view" onto one user-provided memory block.
// User never allocates glyphs/nodes/scratch separately.
struct FontPlan {
    // user-facing parameters (filled by Plan)
    DfMode     mode{};
    uint16_t   pixel_height{};
    float      scale{};       // pixels per font unit
    float      spread_fu{};   // spread in font units

    // results (filled by Plan)
    uint16_t   atlas_side{};  // square side in pixels (no padding)
    uint32_t   glyph_count{};
    uint16_t   max_points{};
    uint32_t   max_area{};

    // ---- internal pointers into the same plan memory block ----
    void*  _mem{};
    size_t _mem_bytes{};

    GlyphPlan*   _glyphs{};
    SkylineNode* _nodes{};
    uint32_t     _node_cap{};

    void* _scratch_mem{};
    size_t     _scratch_bytes{};
};
// Very small bump allocator for "plan_mem" block.
struct MemArena {
    uint8_t* base;
    size_t   cap;
    size_t   off; 
    inline void init(void* mem, size_t bytes) noexcept { base=(uint8_t*)mem; cap=bytes; off=0; }
    inline void* take(size_t bytes, size_t align) noexcept {
        size_t aligned = (off + (align-(size_t)1)) & ~(align-(size_t)1);
        if (aligned+bytes > cap) return nullptr;
        off = aligned+bytes;     return base + aligned;
    }
};
static constexpr size_t align_up(size_t v, size_t a) noexcept { return (v + (a - 1)) & ~(a - 1); }
static inline size_t glyph_scratch_bytes(uint16_t max_points,
                                         uint32_t max_area,
                                         DfMode mode) noexcept {
    size_t off = 0;

    off = align_up(off, 16); off += max_points * sizeof(uint8_t);  // flags
    off = align_up(off, 16); off += max_points * sizeof(int16_t);  // px
    off = align_up(off, 16); off += max_points * sizeof(int16_t);  // py
    
    if (mode == DfMode::SDF) {
        off = align_up(off, 16); off += (size_t)max_area * sizeof(uint16_t); // min_d2
    }
    else if (mode == DfMode::MSDF) {
        off = align_up(off, 16); off += (size_t)max_area * sizeof(uint16_t) * 3; // min_d2_r/g/b
    }
    else { // MTSDF
        off = align_up(off, 16); off += (size_t)max_area * sizeof(uint16_t) * 4; // min_d2_r/g/b + min_d2_a
    }
    off = align_up(off, 16); off += (size_t)max_area * sizeof(uint8_t); // inside
    off = align_up(off, 16); off += (size_t)GlyphScratch::MAX_XS * sizeof(float);
    off = align_up(off, 16); off += (size_t)GlyphScratch::VISIT_CAP * sizeof(uint16_t);

    return align_up(off, 16);
}

static inline GlyphScratch bind_glyph_scratch(void* mem, uint16_t max_points,
                                              uint32_t max_area, DfMode mode) noexcept {
    uint8_t* p = (uint8_t*)mem;
    size_t off = 0;
    auto take = [&](size_t bytes, size_t align) -> void* {
        off = align_up(off, align);
        void* r = p + off;
        off += bytes;
        return r;
    };
    GlyphScratch s{};
    s.flags = (uint8_t*)take((size_t)max_points * sizeof(uint8_t), 16);
    s.px    = (int16_t*)take((size_t)max_points * sizeof(int16_t), 16);
    s.py    = (int16_t*)take((size_t)max_points * sizeof(int16_t), 16);
    const size_t d2_mult = mode==DfMode::SDF ? 1u :
                           mode==DfMode::MSDF ? 3u : 4u;
    s.min_d2 = (uint16_t*)take((size_t)max_area * sizeof(uint16_t) * d2_mult,      16);
    s.inside = (uint8_t*) take((size_t)max_area * sizeof(uint8_t),                 16);
    s.xs     = (float*)   take((size_t)GlyphScratch::MAX_XS    * sizeof(float),    16);
    s.visit  = (uint16_t*)take((size_t)GlyphScratch::VISIT_CAP * sizeof(uint16_t), 16);
    s.visit_n = 0;
    return s;
}

// helper: bytes for plan block (glyphs + nodes + scratch)
static inline size_t plan_block_bytes(uint32_t glyph_count, uint32_t node_cap,
                                      uint16_t max_points,  uint32_t max_area, DfMode mode) noexcept {
    size_t off = 0;
    auto aup = [](size_t v, size_t a) noexcept { return (v + (a-1)) & ~(a-1); };
    off = aup(off, 16); off += (size_t)glyph_count * sizeof(GlyphPlan);
    off = aup(off, 16); off += (size_t)node_cap * sizeof(SkylineNode);
    const size_t scratch_bytes = glyph_scratch_bytes(max_points, max_area, mode);
    off = aup(off, 16); off += scratch_bytes;
    return aup(off, 16);
}
// Basic Newton-Raphson sqrt approximation
static inline float sqrt(float x) noexcept {
    if (x<=0) return 0;
    float r = x;
    for (int i=0; i<5; ++i)
        r = 0.5f*(r + x/r);
    return r;
}
static constexpr uint32_t isqrt_u32(uint32_t x) noexcept {
    // integer sqrt floor
    uint32_t r = 0;
    uint32_t bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        uint32_t t = r + bit;
        r >>= 1;
        if (x >= t) { x -= t; r += bit; }
        bit >>= 2;
    }
    return r;
}
static constexpr uint32_t ceil_sqrt_u32(uint32_t x) noexcept {
    uint32_t r = isqrt_u32(x);
    return (r * r < x) ? (r + 1) : r;
}
static inline int iceil(float v) noexcept {
    int i = (int)v;
    return (v > (float)i) ? (i + 1) : i;
}
static constexpr uint16_t ceil_to_u16(float v) noexcept {
    int iv = (int)v;
    if ((float)iv < v) ++iv;
    if (iv < 1) iv = 1;
    if (iv > 65535) iv = 65535;
    return (uint16_t)iv;
}
static constexpr uint16_t next_pow2_u16(uint32_t v) noexcept {
    if (v <= 1) return 1;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    ++v;
    if (v > 65535) v = 65535;
    return (uint16_t)v;
}
static constexpr uint16_t u16_max(uint16_t a, uint16_t b) noexcept { return a > b ? a : b; }
static constexpr uint16_t u16_min(uint16_t a, uint16_t b) noexcept { return a < b ? a : b; }
static inline float fminf(float a, float b) noexcept { return a < b ? a : b; }
static inline float fmaxf(float a, float b) noexcept { return a > b ? a : b; }
static inline float fabsf_i(float v) noexcept { return v < 0.f ? -v : v; }

static inline uint16_t pack_nd2_u16(float d2, float spread) noexcept {
    // nd2 = clamp(d2 / spread^2, 0..1) -> u16
    const float s2 = spread > 0.f ? (spread * spread) : 1.f;
    float nd2 = d2 / s2;
    if (nd2 < 0.f) nd2 = 0.f;
    if (nd2 > 1.f) nd2 = 1.f;
    return (uint16_t)(nd2 * 65535.f + 0.5f);
}
static inline float dist_line_sq(float px, float py,
                                 float ax, float ay,
                                 float bx, float by) noexcept {
    float vx = bx - ax;
    float vy = by - ay;
    float wx = px - ax;
    float wy = py - ay;

    float c1 = vx*wx + vy*wy;
    if (c1 <= 0.0f)
        return wx*wx + wy*wy;

    float c2 = vx*vx + vy*vy;
    if (c2 <= c1) {
        float dx = px - bx;
        float dy = py - by;
        return dx*dx + dy*dy;
    }

    float t = c1 / c2;
    float dx = ax + t*vx - px;
    float dy = ay + t*vy - py;
    return dx*dx + dy*dy;
}
static inline float dist_quad_sq(float px, float py,
                                 float x0, float y0,
                                 float x1, float y1,
                                 float x2, float y2) noexcept {
    // 4 segments is enough for UI DF
    float min_d = 1e30f;
    float ax = x0;
    float ay = y0;

    for (int i=1; i<=4; ++i) {
        float t = i * 0.25f;
        float mt = 1.0f - t;

        float bx = (mt*mt * x0) + (2.0f*mt * t*x1) + (t*t * x2);
        float by = (mt*mt * y0) + (2.0f*mt * t*y1) + (t*t * y2);

        float d = dist_line_sq(px,py, ax,ay, bx,by);
        if (d < min_d) min_d = d;

        ax = bx;
        ay = by;
    }
    return min_d;
}
static inline int ray_crosses(float px, float py,
                              float x0, float y0,
                              float x1, float y1) noexcept {
    if (y0 > y1) {
        float tx = x0;  x0 = x1;  x1 = tx;
        float ty = y0;  y0 = y1;  y1 = ty;
    }

    if (py <= y0 || py > y1) return 0;

    float t = (py - y0) / (y1 - y0);
    float ix = x0 + t * (x1 - x0);

    if (ix <= px) return 0;

    return (y1 > y0) ? 1 : -1;
}
    
struct DfGrid {
    signed char* df; // [w*h]
    int8_t* winding; // [w*h], signed accumulator

    float scale;
    float max_dist;

    float origin_x;
    float origin_y;

    int w, h;
};
struct DfGridFast {
    uint8_t* out;          // atlas pointer (full atlas)
    uint32_t out_stride;   // atlas width in pixels
    uint8_t  out_comp;     // 1 for SDF, 3 for MSDF
    int shift_x, shift_y;  // cell top-left in atlas

    int w, h;              // cell_size
    float scale;           // pixels per font unit
    float inv_scale;       // font units per pixel
    float spread;          // in font units
    float origin_x, origin_y;

    // per-cell scratch
    // SDF:
    uint16_t* d2;          // [w*h]
    // MSDF:
    uint16_t* d2r;         // [w*h]
    uint16_t* d2g;
    uint16_t* d2b;

    uint8_t* inside;       // [w*h]
};
static inline void pixel_center_to_font(float& fx, float& fy, const DfGridFast& g,
                                        int x, int y) noexcept {
    fx = g.origin_x + (x+.5f) * g.inv_scale;
    fy = g.origin_y + ((g.h-1 - y) + .5f) * g.inv_scale;
}

struct DfWindingPass {
    DfGrid& g;

    explicit DfWindingPass(DfGrid& grid) noexcept : g(grid) {}

    inline void begin() noexcept {
        for (int i = 0; i < g.w * g.h; ++i)
            g.winding[i] = 0;
    }

    inline void line(float x0, float y0,
                     float x1, float y1) noexcept {
        for (int y=0; y<g.h; ++y) {
            float py = g.origin_y + ((g.h-1 - y) + 0.5f) / g.scale;
            for (int x=0; x<g.w; ++x) {
                float px = g.origin_x + (x + 0.5f) / g.scale;
                int idx = y*g.w + x;
                g.winding[idx] += static_cast<int8_t>(ray_crosses(px,py, x0,y0, x1,y1));
            }
        }
    }

    inline void set_origin(float x, float y) noexcept {
        g.origin_x = x;
        g.origin_y = y;
    }
};
struct DfDistancePass {
    DfGrid& g;

    DfDistancePass() = delete;
    explicit DfDistancePass(DfGrid& grid) noexcept : g(grid) {}

    inline void begin() noexcept {
        for (int i = 0; i < g.w * g.h; ++i) g.df[i] = 127;
    }

    inline void line(float x0, float y0, float x1, float y1) noexcept {
        for (int y = 0; y < g.h; ++y) {
            float py = g.origin_y + ((g.h-1 - y) + 0.5f) / g.scale;
            for (int x = 0; x < g.w; ++x) {
                float px = g.origin_x + (x + 0.5f) / g.scale;
                int idx = y * g.w + x;
                float d2 = dist_line_sq(px, py, x0, y0, x1, y1);
                float d = sqrt(d2);
                float prev = static_cast <float> (g.df[idx]) / 127.0f * g.max_dist;
                if (d < prev) {
                    float nd = d / g.max_dist;
                    if (nd > 1) nd = 1;
                    g.df[idx] = static_cast <signed char> (nd * 127);
                }
            }
        }
    }
    inline void set_origin(float x, float y) noexcept {
        g.origin_x = x;
        g.origin_y = y;
    }
};
struct SdfDistanceBBoxPass {
    DfGridFast& g;

    explicit SdfDistanceBBoxPass(DfGridFast& gg) noexcept : g(gg) {}

    inline void begin() noexcept {
        const int n = g.w * g.h;
        for (int i = 0; i < n; ++i) g.d2[i] = 0xFFFF;
    }
    inline void set_origin(float x, float y) noexcept { g.origin_x = x; g.origin_y = y; }

    inline void line(float x0, float y0,
                     float x1, float y1, uint8_t /*color*/) noexcept {
        // font-space bbox expanded by spread
        float minx = (x0<x1 ? x0:x1) - g.spread;
        float maxx = (x0>x1 ? x0:x1) + g.spread;
        float miny = (y0<y1 ? y0:y1) - g.spread;
        float maxy = (y0>y1 ? y0:y1) + g.spread;

        int px0 = (int)((minx - g.origin_x) * g.scale);
        int px1 = (int)((maxx - g.origin_x) * g.scale);
        if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
        if (px0 < 0) px0 = 0;
        if (px1 >= g.w) px1 = g.w - 1;

        // y is flipped, so we clamp by scanning all y but skip rows outside miny/maxy
        for (int y=0; y<g.h; ++y) {
            float fx_dummy, fy;
            pixel_center_to_font(fx_dummy, fy, g, 0, y);
            if (fy < miny || fy > maxy) continue;

            for (int x=px0; x<=px1; ++x) {
                float fx, fy2;
                pixel_center_to_font(fx, fy2, g, x, y);

                const float d2 = dist_line_sq(fx, fy2, x0,y0, x1,y1);
                const uint16_t ud2 = pack_nd2_u16(d2, g.spread);

                const int idx = y*g.w + x;
                if (ud2 < g.d2[idx]) g.d2[idx] = ud2;
            }
        }
    }
};
struct MsdfDistanceBBoxPass {
    DfGridFast& g;

    explicit MsdfDistanceBBoxPass(DfGridFast& gg) noexcept : g(gg) {}

    inline void begin() noexcept {
        const int n = g.w * g.h;
        for (int i = 0; i < n; ++i) {
            g.d2r[i] = 0xFFFF;
            g.d2g[i] = 0xFFFF;
            g.d2b[i] = 0xFFFF;
        }
    }
    inline void set_origin(float x, float y) noexcept { g.origin_x = x; g.origin_y = y; }

    inline void line(float x0, float y0,
                     float x1, float y1, uint8_t color) noexcept {
        float minx = (x0 < x1 ? x0 : x1) - g.spread;
        float maxx = (x0 > x1 ? x0 : x1) + g.spread;
        float miny = (y0 < y1 ? y0 : y1) - g.spread;
        float maxy = (y0 > y1 ? y0 : y1) + g.spread;

        int px0 = (int)((minx - g.origin_x) * g.scale);
        int px1 = (int)((maxx - g.origin_x) * g.scale);
        if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
        if (px0 < 0) px0 = 0;
        if (px1 >= g.w) px1 = g.w - 1;

        for (int y = 0; y < g.h; ++y) {
            float fx_dummy, fy;
            pixel_center_to_font(fx_dummy, fy, g, 0, y);
            if (fy < miny || fy > maxy) continue;

            for (int x = px0; x <= px1; ++x) {
                float fx, fy2;
                pixel_center_to_font(fx, fy2, g, x, y);

                const float d2 = dist_line_sq(fx, fy2, x0, y0, x1, y1);
                const uint16_t ud2 = pack_nd2_u16(d2, g.spread);

                const int idx = y * g.w + x;
                if (color == EDGE_R) { if (ud2 < g.d2r[idx]) g.d2r[idx] = ud2; }
                else if (color == EDGE_G) { if (ud2 < g.d2g[idx]) g.d2g[idx] = ud2; }
                else { if (ud2 < g.d2b[idx]) g.d2b[idx] = ud2; }
            }
        }
    }
};

struct DfSignScanlinePass {
    DfGridFast& g;

    // scratch per row
    float* xs;      // [max_intersections]
    int   count;
    float scan_y;

    explicit DfSignScanlinePass(DfGridFast& gg, float* xs_buf) noexcept
        : g(gg), xs(xs_buf), count(0), scan_y(0.f) {}

    inline void begin() noexcept {
        // mark inside later per row
        // nothing here
    }
    inline void set_origin(float x, float y) noexcept { g.origin_x = x; g.origin_y = y; }

    inline void begin_row(int y) noexcept {
        float fx_dummy;
        pixel_center_to_font(fx_dummy, scan_y, g, 0, y);
        count = 0;
    }

    inline void line(float x0, float y0, float x1, float y1, uint8_t /*edge_color*/) noexcept {
        // ignore horizontal edges (critical for vertex double-count stability)
        if (y0 == y1) return;

        // standard half-open rule to avoid double counting vertices
        float ay=y0, by=y1, ax=x0, bx=x1;
        if (ay > by) { float tx=ax; ax=bx; bx=tx; float ty=ay; ay=by; by=ty; }

        // canonical half-open: [ay, by)
        if (!(scan_y >= ay && scan_y < by)) return;
        
        float t = (scan_y-ay) / (by-ay);
        float ix = ax + t*(bx-ax);
 
        if (count < GlyphScratch::MAX_XS) xs[count++] = ix;
    }

    static inline void sort_small(float* a, int n) noexcept {
        // insertion sort, n small
        for (int i=1; i<n; ++i) {
            float v = a[i];
            int j = i-1;
            while (j >= 0 && a[j] > v) { a[j+1] = a[j]; --j; }
            a[j+1] = v;
        }
    }

    inline void finalize_row(int y) noexcept {
        sort_small(xs, count);

        // tiny merge only (float noise)
        const float tol = 1e-4f * g.inv_scale; // ~0.0001 px
        int m=0;
        for (int i=0;i<count;++i){
            float v=xs[i];
            if (m && fabsf_i(v - xs[m-1]) < tol) continue;
            xs[m++] = v;
        }
        count = m;

        const int w = g.w;
        uint8_t* row = g.inside + y*w;
        for (int x=0; x<w; ++x) row[x] = 0;

        // pixel-center x in font space: fx = origin_x + (x+0.5)/scale
        // We want to mark pixels whose centers lie in [x0, x1) (half-open).
        for (int i=0; i+1 < count; i += 2) {
            float x0 = xs[i];
            float x1 = xs[i+1];
            if (x0 > x1) { float t=x0; x0=x1; x1=t; }

            // Convert to pixel index range using centers:
            // x_center = origin_x + (x+0.5)/scale
            // x_center >= x0  => x >= (x0-origin_x)*scale - 0.5
            // x_center <  x1  => x <  (x1-origin_x)*scale - 0.5
            float a = (x0 - g.origin_x) * g.scale - 0.5f;
            float b = (x1 - g.origin_x) * g.scale - 0.5f;

            int px0 = iceil(a);
            int px1 = iceil(b);   // exclusive end

            if (px0 < 0) px0 = 0;
            if (px1 > w) px1 = w;
            for (int x = px0; x < px1; ++x)
                row[x] = 1;
        }
    }
};

struct NullSink {
    inline void begin() noexcept {}
    inline void move(float, float) noexcept {}
    inline void line(float, float, float, float) noexcept {}
    inline void cubic(float cx1, float cy1, float cx2, float cy2, float nx, float ny) noexcept {}
    inline void quad(float, float, float, float, float, float) noexcept {}
    inline void end() noexcept {}
    inline void set_origin(float x, float y) noexcept {}
};
struct GlyphSink {
    virtual void begin() noexcept = 0;
    virtual void move(float, float) noexcept = 0;
    virtual void line(float, float) noexcept = 0;
    virtual void quad(float, float, float, float) noexcept = 0;
    virtual void cubic(float, float, float, float, float, float) noexcept = 0;
    virtual void close() noexcept = 0;
    virtual void set_origin(float x, float y) noexcept = 0;
    virtual void set_edge_color(uint8_t) noexcept {}
    virtual ~GlyphSink() noexcept = default;
};
template<class Pass>
struct DfSink {
    Pass& pass;
    float x{}, y{};
    float sx{}, sy{};
    bool open{ false };
    uint8_t edge_color{ EDGE_R }; // default, can be changed outside

    DfSink() = delete;
    explicit DfSink(Pass& p) noexcept : pass(p) {}

    inline void begin() noexcept { pass.begin(); }
    inline void set_origin(float ox, float oy) noexcept { pass.set_origin(ox,oy); }
    inline void set_edge_color(uint8_t c) noexcept { edge_color = c; }

    inline void move(float nx, float ny) noexcept {
        x = sx = nx;
        y = sy = ny;
        open = true;
    }

    inline void line(float nx, float ny) noexcept {
        pass.line(x,y, nx,ny, edge_color);
        x = nx; y = ny;
    }

    inline void quad(float cx, float cy, float nx, float ny) noexcept {
        // fixed-step flatten (cheap, predictable)
        constexpr int STEPS = 8;
        float ax = x, ay = y;
        for (int i=1; i<=STEPS; ++i) {
            const float t  = (float)i * (1.0f / (float)STEPS);
            const float mt = 1.f - t;

            const float bx = mt*mt*x + 2.f*mt*t*cx + t*t*nx;
            const float by = mt*mt*y + 2.f*mt*t*cy + t*t*ny;

            pass.line(ax, ay, bx, by, edge_color);
            ax = bx; ay = by;
        }
        x = nx; y = ny;
    }

    inline void cubic(float cx1, float cy1, float cx2, float cy2, float nx, float ny) noexcept {
        // fixed-step cubic flatten
        constexpr int STEPS = 12;
        float ax = x, ay = y;
        for (int i=1; i<=STEPS; ++i) {
            const float t  = (float)i * (1.0f / (float)STEPS);
            const float mt = 1.f - t;
            const float bx = (mt*mt*mt)*x + 3.f*(mt*mt)*t*cx1
                                          + 3.f*mt*(t*t)*cx2 + (t*t*t)*nx;

            const float by = (mt*mt*mt)*y + 3.f*(mt*mt)*t*cy1
                                          + 3.f*mt*(t*t)*cy2 + (t*t*t)*ny;

            pass.line(ax,ay, bx,by, edge_color);
            ax=bx; ay=by;
        }
        x = nx; y = ny;
    }

    inline void close() noexcept {
        if (open && (x != sx || y != sy))
            pass.line(x, y, sx, sy, edge_color);
        open = false;
    }

    // ----------- RELATIVE METHODS ------------
    inline void rmove(float dx, float dy) noexcept { move(x+dx, y+dy); }
    inline void rline(float dx, float dy) noexcept { line(x+dx, y+dy, edge_color); }
    inline void rcubic(float cx1, float cy1,
                       float cx2, float cy2,
                       float nx, float ny) noexcept {
        cubic(x+cx1, y+cy1,  x+cx2, y+cy2,  x+nx, y+ny);
    }

}; // struct DfSink

struct Font {
    explicit Font() noexcept = default;
    ~Font() noexcept = default;

    inline bool ReadBytes(uint8_t* font_buffer) noexcept;
    inline float ScaleForPixelHeight(float height) const noexcept;
    inline int FindGlyphIndex(int unicode_codepoint) const noexcept;
    inline GlyphHorMetrics GetGlyphHorMetrics(int glyph_index) const noexcept;

    // INIT
    inline size_t PlanBytes(const PlanInput& in) const noexcept;

    // PASS 1
    inline bool Plan(const PlanInput& in,
                     void* plan_mem, size_t plan_bytes,
                     FontPlan& out_plan) noexcept;
    // PASS 2
    inline bool Build(const FontPlan& plan,
                      uint8_t* atlas,
                      uint32_t atlas_stride_bytes) noexcept;

    // 1 glyph, independent: unrelated to passes, streams glyph
    inline bool StreamDF(const GlyphPlan& gp,
        unsigned char* atlas,
        uint32_t atlas_stride_bytes, // atlas stride
        DfMode mode,          // SDF or MSDF
        float scale,          // pixels per font unit
        float spread,         // font units
        GlyphScratch& scratch,
        uint16_t max_points,  // from plan
        uint32_t max_area     // from plan
    ) noexcept;

    
    // public helper (tiny, no skyline, no passes)
    inline bool GetGlyphPlanInfo(int glyph_index, GlyphPlanInfo& out) const noexcept {
        return parse_glyph_plan_info_(_data, _loca, _glyf, _index_to_loc_format, _num_glyphs, glyph_index, out);
    }

public:
    static inline int GetFontOffsetForIndex(uint8_t* font_buffer, int index) noexcept;
    static inline int GetNumberOfFonts(const uint8_t* font_buffer) noexcept;

private:
    template<class SinkT>
    bool RunGlyfStream(int glyph_index, SinkT& sink, const Xform& xf, float spread,
                        GlyphScratch& scratch, uint16_t max_points) noexcept;

    // --- Parsing helpers ---
    inline int GetGlyfOffset(int glyph_index) const noexcept;
    inline uint32_t FindTable(const char* tag) const noexcept;

    // --- Static parsing helpers ---
    static uint8_t  byte_(const uint8_t* p) noexcept   { return *p; }
    static int8_t   char_(const uint8_t* p) noexcept   { return (int8_t)(*p); }
    static uint16_t ushort_(const uint8_t* p) noexcept { return p[0] * 256 + p[1]; }
    static int16_t  short_(const uint8_t* p) noexcept  { return p[0] * 256 + p[1]; }
    static uint32_t ulong_(const uint8_t* p) noexcept  { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
    static int32_t  long_(const uint8_t* p) noexcept   { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
    // --- helpers: `RunGlyfStream` local ---
    static inline bool is_on_(uint8_t f) noexcept { return (f & 0x80) != 0; } // our reserved bit
    static inline void set_on_u8_(uint8_t& f, bool on) noexcept {
        f = (uint8_t)((f & 0x7F) | (on ? 0x80 : 0x00));
    }
    static inline uint32_t glyph_offset_for_index_(const uint8_t* data, int loca, int glyf, int index_to_loc_format, int g) noexcept {
        return index_to_loc_format == 0
            ? (uint32_t)(glyf + 2u * (uint32_t)(data[loca + 2*g] * 256 + data[loca + 2*g+1]))
            : (uint32_t)(glyf + (uint32_t)(
                (data[loca + 4*g+0] << 24) |
                (data[loca + 4*g+1] << 16) |
                (data[loca + 4*g+2] << 8 ) |
                 data[loca + 4*g+3]));
    }
    static inline uint16_t read_u16_be_(const uint8_t* p) noexcept { return (uint16_t)(p[0]*256 + p[1]); }
    static inline int16_t  read_s16_be_(const uint8_t* p) noexcept { return (int16_t)(p[0]*256 + p[1]); }
    static inline bool parse_glyph_plan_info_(const uint8_t* data,
                                          int loca, int glyf,
                                          int index_to_loc_format,
                                          int num_glyphs,
                                          int glyph_index,
                                          GlyphPlanInfo& out) noexcept;
    // --- helpers: parsing tags ---
    static bool tag4_(const uint8_t* p, char c0, char c1, char c2, char c3) noexcept {
        return p[0]==c0 && p[1]==c1 && p[2]==c2 && p[3]==c3;
    }
    static bool tag_(const uint8_t* p, const char* str) noexcept { return tag4_(p, str[0], str[1], str[2], str[3]); }
    static int is_font(const uint8_t* font) noexcept {
        // check the version number
        if (tag4_(font, '1', 0, 0, 0))  return 1; // TrueType 1
        if (tag_(font, "typ1"))   return 1; // TrueType with type 1 font -- we don't support this!
        if (tag_(font, "true"))   return 1; // Apple specification for TrueType fonts
        return 0;
    }

private:
    uint8_t* _data{};                 // pointer to .ttf file
    int _num_glyphs{};                // number of glyphs, needed for range checking

    // table locations as offset from start of .ttf
    int _loca{}, _head{}, _glyf{}, _hhea{}, _hmtx{};
    int _index_map{};                 // a cmap mapping for our chosen character encoding
    int _index_to_loc_format{};       // format needed to map from glyph index to glyph
}; // struct Font

// ============================================================================
//                         PUBLIC   METHODS
// ============================================================================

inline bool Font::StreamDF(const GlyphPlan& gp,
                            unsigned char* atlas, uint32_t atlas_stride_bytes,
                            DfMode mode,
                            float scale,          // pixels per font unit
                            float spread,         // font units
                            GlyphScratch& scratch,
                            uint16_t max_points, uint32_t max_area) noexcept {
    if (!atlas) return false;
    if (gp.rect.w == 0 || gp.rect.h == 0) return false;

    const int w = (int)gp.rect.w;
    const int h = (int)gp.rect.h;
    const uint32_t area = (uint32_t)w * (uint32_t)h;
    if (area > (uint32_t)max_area) return false;

    DfGridFast gg{};
    gg.out = (uint8_t*)atlas;
    gg.out_comp = (mode == DfMode::SDF) ? 1 : (mode == DfMode::MSDF) ? 3 : 4;
    gg.out_stride = atlas_stride_bytes;
    gg.shift_x = (int)gp.rect.x;
    gg.shift_y = (int)gp.rect.y;

    gg.w = w;
    gg.h = h;

    gg.scale = scale;
    gg.inv_scale = scale>0.f ? 1.f/scale : 0.f;
    gg.spread = spread;

    // rect origin in font space: bbox expanded by spread on both sides
    gg.origin_x = (float)gp.x_min - spread;
    gg.origin_y = (float)gp.y_min - spread;

    gg.inside = scratch.inside;

    // --------- bind distance buffers ----------
    if (mode == DfMode::SDF) {
        gg.d2 = scratch.min_d2;
        gg.d2r = gg.d2g = gg.d2b = nullptr;
    }
    else if (mode == DfMode::MSDF) {
        gg.d2 = nullptr;
        gg.d2r = scratch_d2_r(scratch);
        gg.d2g = scratch_d2_g(scratch, max_area);
        gg.d2b = scratch_d2_b(scratch, max_area);
    }
    else { // MTSDF
        gg.d2  = scratch_d2_a(scratch, max_area);
        gg.d2r = scratch_d2_r(scratch);
        gg.d2g = scratch_d2_g(scratch, max_area);
        gg.d2b = scratch_d2_b(scratch, max_area);
    }

    // =====================================================================
    // 1) distance pass
    // =====================================================================
    if (mode == DfMode::SDF) {
        SdfDistanceBBoxPass pass(gg);
        DfSink<SdfDistanceBBoxPass> sink(pass);
        const Xform id = Xform::identity();

        if (!RunGlyfStream(gp.glyph_index, sink, id, spread, scratch, max_points))
            return false;
    }
    else if (mode == DfMode::MSDF) {
        MsdfDistanceBBoxPass pass(gg);
        DfSink<MsdfDistanceBBoxPass> sink(pass);
        const Xform id = Xform::identity();

        if (!RunGlyfStream(gp.glyph_index, sink, id, spread, scratch, max_points))
            return false;
    }
    else { // MTSDF: RGB from MSDF + A from true SDF
        {
            MsdfDistanceBBoxPass pass(gg);
            DfSink<MsdfDistanceBBoxPass> sink(pass);
            const Xform id = Xform::identity();

            if (!RunGlyfStream(gp.glyph_index, sink, id, spread, scratch, max_points))
                return false;
        }
        {
            SdfDistanceBBoxPass pass(gg);
            DfSink<SdfDistanceBBoxPass> sink(pass);
            const Xform id = Xform::identity();

            if (!RunGlyfStream(gp.glyph_index, sink, id, spread, scratch, max_points))
                return false;
        }
    }

    // =====================================================================
    // 2) sign pass (same for both)
    // =====================================================================
    {
        DfSignScanlinePass pass(gg, scratch.xs);
        DfSink<DfSignScanlinePass> sink(pass);
        const Xform id = Xform::identity();

        for (int y=0; y<h; ++y) {
            pass.begin_row(y);
            if (!RunGlyfStream(gp.glyph_index, sink, id, spread, scratch, max_points))
                return false;
            pass.finalize_row(y);
        }
    }

    // 3) finalize to atlas
    if (mode == DfMode::MSDF) {
        for (int y=0; y<h; ++y) {
            uint8_t* row = gg.out + (uint32_t)(gg.shift_y + y) * gg.out_stride
                         + (uint32_t)gg.shift_x * 3u;

            for (int x=0; x<w; ++x) {
                const int idx = y*w + x;

                const float nr = sqrt((float)gg.d2r[idx] * (1.f / 65535.f));
                const float ng = sqrt((float)gg.d2g[idx] * (1.f / 65535.f));
                const float nb = sqrt((float)gg.d2b[idx] * (1.f / 65535.f));

                int sr = (int)(nr * 127.f + .5f);
                int sg = (int)(ng * 127.f + .5f);
                int sb = (int)(nb * 127.f + .5f);

                if (gg.inside[idx]) {
                    sr = -sr;
                    sg = -sg;
                    sb = -sb;
                }

                uint8_t* p = row + (uint32_t)x * 3u;
                p[0] = (uint8_t)(128 + sr);
                p[1] = (uint8_t)(128 + sg);
                p[2] = (uint8_t)(128 + sb);
            }
        }
    }
    else if (mode == DfMode::MTSDF) {
        for (int y=0; y<h; ++y) {
            uint8_t* row = gg.out + (uint32_t)(gg.shift_y + y) * gg.out_stride
                                  + (uint32_t)gg.shift_x * 4u;

            for (int x=0; x<w; ++x) {
                const int idx = y*w + x;

                const float nr = sqrt((float)gg.d2r[idx] * (1.f / 65535.f));
                const float ng = sqrt((float)gg.d2g[idx] * (1.f / 65535.f));
                const float nb = sqrt((float)gg.d2b[idx] * (1.f / 65535.f));

                float na = sqrt((float)gg.d2[idx] * (1.f / 65535.f));
                if (na > 1.f) na = 1.f;

                int sr = (int)(nr * 127.f + .5f);
                int sg = (int)(ng * 127.f + .5f);
                int sb = (int)(nb * 127.f + .5f);
                int sa = (int)(na * 127.f + .5f);

                if (gg.inside[idx]) {
                    sr = -sr;
                    sg = -sg;
                    sb = -sb;
                    sa = -sa;
                }

                uint8_t* p = row + (uint32_t)x * 4u;
                p[0] = (uint8_t)(128 + sr);
                p[1] = (uint8_t)(128 + sg);
                p[2] = (uint8_t)(128 + sb);
                p[3] = (uint8_t)(128 + sa);
            }
        }
    }
    else /* SDF */ {
        for (int y=0; y<h; ++y) {
            uint8_t* row = gg.out + (uint32_t)(gg.shift_y + y) * gg.out_stride
                         + (uint32_t)gg.shift_x;

            for (int x=0; x<w; ++x) {
                const int idx = y*w + x;

                float nd = sqrt((float)gg.d2[idx] * (1.f / 65535.f));
                if (nd > 1.f) nd = 1.f;

                int sd = (int)(nd * 127.f + 0.5f);
                if (gg.inside[idx]) sd = -sd;

                row[x] = (uint8_t)(128 + sd);
            }
        }
    }
return true;
}

inline size_t Font::PlanBytes(const PlanInput& in) const noexcept {
    if (!in.codepoints || in.codepoint_count == 0) return 0;

    // compute scale/spread in font units
    const float scale = ScaleForPixelHeight((float)in.pixel_height);
    const float spread_fu = (scale > 0.f) ? (in.spread_px / scale) : 0.f;

    uint32_t glyph_count = 0;
    uint16_t max_points = 0;
    uint32_t max_area = 0;

    for (uint32_t i = 0; i < in.codepoint_count; ++i) {
        const uint32_t cp = in.codepoints[i];
        const int gi = FindGlyphIndex((int)cp);
        if (gi <= 0) continue;

        GlyphPlanInfo gpi{};
        if (!GetGlyphPlanInfo(gi, gpi)) continue;
        if (gpi.is_empty) continue;

        // bbox+spread -> pixel rect
        const float span_x = (float)(gpi.x_max - gpi.x_min) + 2.f * spread_fu;
        const float span_y = (float)(gpi.y_max - gpi.y_min) + 2.f * spread_fu;

        const uint16_t rw = ceil_to_u16(span_x * scale);
        const uint16_t rh = ceil_to_u16(span_y * scale);

        const uint32_t area = (uint32_t)rw * (uint32_t)rh;
        if (area > max_area) max_area = area;

        if (gpi.max_points_in_tree > max_points) max_points = gpi.max_points_in_tree;

        ++glyph_count;
    }

    if (!glyph_count) return 0;

    // skyline needs ~2*N+16 nodes
    const uint32_t node_cap = 2u * glyph_count + 16u;

    // final bytes for one plan block
    return plan_block_bytes(glyph_count, node_cap, max_points, max_area, in.mode);
}

inline bool Font::Plan(const PlanInput& in,
                       void* plan_mem, size_t plan_bytes,
                       FontPlan& out_plan) noexcept {
    if (!plan_mem || plan_bytes == 0) return false;
    if (!in.codepoints || in.codepoint_count == 0) return false;

    // compute scale/spread in font units
    const float scale = ScaleForPixelHeight((float)in.pixel_height);
    if (scale <= 0.f) return false;

    const float spread_fu = in.spread_px / scale;

    // --------- First: count again (must match plan_bytes logic) ----------
    uint32_t glyph_count = 0;
    uint16_t max_points = 0;
    uint32_t max_area = 0;
    uint32_t total_area = 0;
    uint16_t max_w = 0, max_h = 0;

    for (uint32_t i = 0; i < in.codepoint_count; ++i) {
        const int gi = FindGlyphIndex((int)in.codepoints[i]);
        if (gi <= 0) continue;

        GlyphPlanInfo gpi{};
        if (!GetGlyphPlanInfo(gi, gpi)) continue;
        if (gpi.is_empty) continue;

        const float span_x = (float)(gpi.x_max - gpi.x_min) + 2.f * spread_fu;
        const float span_y = (float)(gpi.y_max - gpi.y_min) + 2.f * spread_fu;

        const uint16_t rw = ceil_to_u16(span_x * scale);
        const uint16_t rh = ceil_to_u16(span_y * scale);

        const uint32_t area = (uint32_t)rw * (uint32_t)rh;
        total_area += area;
        if (area > max_area) max_area = area;

        if (rw > max_w) max_w = rw;
        if (rh > max_h) max_h = rh;

        if (gpi.max_points_in_tree > max_points) max_points = gpi.max_points_in_tree;

        ++glyph_count;
    }

    if (!glyph_count) return false;

    const uint32_t node_cap = 2u * glyph_count + 16u;

    // verify plan_bytes big enough
    const size_t need_bytes = plan_block_bytes(glyph_count, node_cap, max_points, max_area, in.mode);
    if (plan_bytes < need_bytes) return false;

    // --------- Bind plan block ----------
    MemArena a{};
    a.init(plan_mem, plan_bytes);

    GlyphPlan* glyphs = (GlyphPlan*)a.take((size_t)glyph_count * sizeof(GlyphPlan), 16);
    SkylineNode* nodes = (SkylineNode*)a.take((size_t)node_cap * sizeof(SkylineNode), 16);

    const size_t scratch_bytes = glyph_scratch_bytes(max_points, max_area, in.mode);
    void* scratch_mem = a.take(scratch_bytes, 16);

    if (!glyphs || !nodes || !scratch_mem) return false;

    // --------- Fill glyph array (second pass) ----------
    uint32_t at = 0;
    for (uint32_t i = 0; i < in.codepoint_count; ++i) {
        const uint32_t cp = in.codepoints[i];
        const int gi = FindGlyphIndex((int)cp);
        if (gi <= 0) continue;

        GlyphPlanInfo gpi{};
        if (!GetGlyphPlanInfo(gi, gpi)) continue;
        if (gpi.is_empty) continue;

        GlyphPlan& gp = glyphs[at++];
        gp.codepoint = cp;
        gp.glyph_index = (uint16_t)gi;
        gp.x_min = gpi.x_min;
        gp.y_min = gpi.y_min;
        gp.x_max = gpi.x_max;
        gp.y_max = gpi.y_max;
        gp.num_points = gpi.max_points_in_tree;

        const float span_x = (float)(gp.x_max - gp.x_min) + 2.f * spread_fu;
        const float span_y = (float)(gp.y_max - gp.y_min) + 2.f * spread_fu;

        gp.rect.w = ceil_to_u16(span_x * scale);
        gp.rect.h = ceil_to_u16(span_y * scale);
        gp.rect.x = 0;
        gp.rect.y = 0;
    }

    // defensive: should match glyph_count
    if (at != glyph_count) return false;

    // --------- Choose atlas side and skyline-pack ----------
    uint16_t side = next_pow2_u16(ceil_sqrt_u32(total_area));
    if (side < max_w) side = next_pow2_u16(max_w);
    if (side < max_h) side = next_pow2_u16(max_h);
    if (side < 64) side = 64;

    bool packed = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        Skyline sk{};
        skyline_init(sk, side, nodes, (int)node_cap);

        bool ok = true;
        for (uint32_t i = 0; i < glyph_count; ++i) {
            uint16_t x, y;
            if (!skyline_insert(sk, glyphs[i].rect.w, glyphs[i].rect.h, x, y)) {
                if ((uint32_t)x + glyphs[i].rect.w > side) return false;
                if ((uint32_t)y + glyphs[i].rect.h > side) return false;
                ok = false;
                break;
            }
            glyphs[i].rect.x = x;
            glyphs[i].rect.y = y;
        }

        if (ok) { packed = true; break; }
        if (side >= 32768) break;
        side = (uint16_t)(side * 2);
    }
    if (!packed) return false;

    // --------- Fill out_plan ----------
    out_plan.mode = in.mode;
    out_plan.pixel_height = in.pixel_height;
    out_plan.scale = scale;
    out_plan.spread_fu = spread_fu;

    out_plan.atlas_side = side;
    out_plan.glyph_count = glyph_count;
    out_plan.max_points = max_points;
    out_plan.max_area = max_area;

    out_plan._mem = plan_mem;
    out_plan._mem_bytes = plan_bytes;

    out_plan._glyphs = glyphs;
    out_plan._nodes = nodes;
    out_plan._node_cap = node_cap;

    out_plan._scratch_mem = scratch_mem;
    out_plan._scratch_bytes = scratch_bytes;

    return true;
}



inline bool Font::Build(const FontPlan& plan,
                        uint8_t* atlas,
                        uint32_t atlas_stride_bytes) noexcept {
    if (!atlas) return false;
    if (!plan._glyphs || !plan._scratch_mem) return false;
    if (!plan.atlas_side || !plan.glyph_count) return false;

    const uint32_t comp = plan.mode==DfMode::SDF ? 1u :
                          plan.mode==DfMode::MSDF ? 3u : 4u;
    if (atlas_stride_bytes < (uint32_t)plan.atlas_side * comp)
        return false;

    // bind scratch views (also sets visit_n=0, etc.)
    GlyphScratch scratch = bind_glyph_scratch(plan._scratch_mem,
        plan.max_points,
        plan.max_area,
        plan.mode);

    for (uint32_t i = 0; i < plan.glyph_count; ++i) {
        const GlyphPlan& gp = plan._glyphs[i];

        // bounds check (atlas is square side x side)
        if ((uint32_t)gp.rect.x + gp.rect.w > plan.atlas_side)
            return false;
        if ((uint32_t)gp.rect.y + gp.rect.h > plan.atlas_side)
            return false;

        // IMPORTANT: reset recursion guard per glyph
        scratch.visit_n = 0;

        if (!StreamDF(gp,
            (unsigned char*)atlas,
            atlas_stride_bytes,   // NOTE: stride is BYTES, not width in pixels
            plan.mode,
            plan.scale,
            plan.spread_fu,
            scratch,
            plan.max_points,
            plan.max_area))
            return false;
    }
    return true;
}

// ============================================================================
//                         PRIVATE   METHODS
// ============================================================================

static inline bool glyf_visit_push(GlyphScratch& scratch, uint16_t g) noexcept {
    for (uint16_t i = 0; i < scratch.visit_n; ++i) if (scratch.visit[i] == g) return false; // cycle
    if (scratch.visit_n >= GlyphScratch::VISIT_CAP) return false;
    scratch.visit[scratch.visit_n++] = g;
    return true;
};
static inline void glyf_visit_pop(GlyphScratch& scratch) noexcept {
    if (scratch.visit_n) --scratch.visit_n;
};

struct VisitGuard {
    GlyphScratch& s;
    bool pushed;
    VisitGuard(GlyphScratch& ss, uint16_t g) noexcept : s(ss), pushed(false) {
        pushed = glyf_visit_push(s, g);
    }
    ~VisitGuard() noexcept {
        if (pushed) glyf_visit_pop(s);
    }
    inline bool ok() const noexcept { return pushed; }
};

template<class SinkT>
bool Font::RunGlyfStream(int glyph_index,
                         SinkT& sink,
                         const Xform& xf,
                         float /*spread*/,
                         GlyphScratch& scratch,
                         uint16_t max_points) noexcept {
    if (!_glyf || !_loca) return false;
    if ((unsigned)glyph_index >= (unsigned)_num_glyphs) return false;

    VisitGuard root(scratch, (uint16_t)glyph_index);
    if (!root.ok()) return false;

    // call begin() only for the top-level glyph in this stream
    if (scratch.visit_n == 1)
        sink.begin();

    auto glyph_offset = [&](int g) -> uint32_t {
        return _index_to_loc_format == 0 ?
              _glyf + 2u * (uint32_t)ushort_(_data+_loca + 2*g)
            : _glyf +      (uint32_t)ulong_ (_data+_loca + 4*g);
    };

    const uint8_t* g = _data + glyph_offset(glyph_index);
    {
        const uint32_t g0 = g - _data;
        const uint32_t g1 = glyph_offset(glyph_index + 1);
        if (g0 == g1)
            return false; // empty glyph
    } 
    const int16_t num_contours = short_(g);
    g += 10;

    // ------------------------------------------------------------
    // SIMPLE GLYPH
    // ------------------------------------------------------------
    if (num_contours >= 0) {
        const int ncontours = num_contours;

        // --- end points of contours ---
        const uint8_t* end_pts = g;
        g += 2*ncontours;

        const uint16_t num_points = ushort_(end_pts + 2*(ncontours-1)) + 1;
        if (num_points > max_points) return false;

        // --- instructions ---
        const uint16_t instr_len = ushort_(g);
        g += 2 + instr_len;

        // --- read flags ---
        uint8_t* flags = scratch.flags;
        int16_t* px = scratch.px;
        int16_t* py = scratch.py;

        { // flags (with repeats)
            uint16_t fcount = 0;
            while (fcount < num_points) {
                uint8_t f = *g++;
                flags[fcount++] = f;
                if (f & 8) { // repeat
                    uint8_t r = *g++;
                    while (r-- && fcount < num_points) flags[fcount++] = f;
                }
            }
        }

        // --- decode X coordinates ---
        int x = 0;
        for (int i=0; i<num_points; ++i) {
            int dx = 0;
            const uint8_t f = flags[i];
            if (f & 2) { // x-short
                uint8_t v = *g++;
                dx = (f & 16) ? (int)v : -(int)v;
            }
            else if (!(f & 16)) { // x is int16
                dx = short_(g);
                g+=2;
            } // else same as previous (dx=0)
            x += dx;
            px[i] = (int16_t)x;

            // cache on-curve into reserved bit (bit 7)
            set_on_u8_(flags[i], (f&1) != 0);
        }

        // --- decode Y coordinates ---
        int y = 0;
        for (int i=0; i<num_points; ++i) {
            int dy = 0;
            const uint8_t f = flags[i];
            if (f & 4) { // y-short
                uint8_t v = *g++;
                dy = (f & 32) ? (int)v : -(int)v;
            }
            else if (!(f & 32)) { // y is int16
                dy = short_(g);
                g+=2;
            }
            y += dy;
            py[i] = (int16_t)y;
        }

        // small helper to emit transformed points
        auto emit_move = [&](float x0, float y0) noexcept {
            float tx, ty; xform_apply(xf, x0, y0, tx, ty);
            sink.move(tx, ty);
        };
        auto emit_line = [&](float x0, float y0) noexcept {
            float tx, ty; xform_apply(xf, x0, y0, tx, ty);
            sink.line(tx, ty);
        };
        auto emit_quad = [&](float cx, float cy, float x1, float y1) noexcept {
            float tcx, tcy, tx, ty;
            xform_apply(xf, cx, cy, tcx, tcy);
            xform_apply(xf, x1, y1, tx, ty);
            sink.quad(tcx, tcy, tx, ty);
        };
        auto emit_cubic = [&](float cx1, float cy1, float cx2, float cy2, float x1, float y1) noexcept {
            float tcx1,tcy1, tcx2,tcy2, tx,ty;
            xform_apply(xf, cx1, cy1, tcx1, tcy1);
            xform_apply(xf, cx2, cy2, tcx2, tcy2);
            xform_apply(xf, x1,  y1,  tx,  ty);
            sink.cubic(tcx1, tcy1, tcx2, tcy2, tx, ty);
        };

        // --- emit contours ---
        uint16_t start = 0;
        uint8_t col = 0;
        auto next_col = [&]{ col = (uint8_t)((col+1) % 3); };
        auto emit_contour = [&](uint16_t s, uint16_t end) noexcept {
            auto at = [&](uint16_t idx)->uint16_t { return (idx==end) ? s : (uint16_t)(idx+1); };
                
            auto X = [&](uint16_t idx)->float { return (float)px[idx]; };
            auto Y = [&](uint16_t idx)->float { return (float)py[idx]; };
            auto ON = [&](uint16_t idx)->bool { return is_on_(flags[idx]); };
                
            // ---- choose start point P0 (must be on-curve in param space) ----
            uint16_t first = s;
            uint16_t last  = end;
                
            float P0x, P0y;        // current pen position in font units
            float Cx=0, Cy=0;      // pending control point
            bool  hasC=false;
                
            if (ON(first)) {
                P0x = X(first); P0y = Y(first);
            } else {
                // start is off-curve: start point is either last on-curve, or midpoint(lastOff, firstOff)
                if (ON(last)) {
                    P0x = X(last); P0y = Y(last);
                } else {
                    P0x = 0.5f*(X(last)+X(first));
                    P0y = 0.5f*(Y(last)+Y(first));
                }
                // also treat first off-curve as pending control for the next segment
                Cx = X(first); Cy = Y(first);
                hasC = true;
            }
        
            emit_move(P0x, P0y);
        
            // iterate points starting AFTER first
            uint16_t i = first;
            while (true) {
                uint16_t j = at(i);
                if (j == first) break; // completed loop back to start index
            
                float jx = X(j), jy = Y(j);
                bool  jon = ON(j);
            
                if (jon) {
                    sink.set_edge_color(col);
                    if (hasC) {
                        emit_quad(Cx, Cy, jx, jy);
                        hasC = false;
                    } else {
                        emit_line(jx, jy);
                    }
                    next_col();
                    P0x = jx; P0y = jy;
                } else {
                    if (hasC) {
                        // off-off: insert implicit on at midpoint M between C and j
                        float mx = 0.5f*(Cx + jx);
                        float my = 0.5f*(Cy + jy);
                        sink.set_edge_color(col);
                        emit_quad(Cx, Cy, mx, my);
                        next_col();
                        P0x = mx; P0y = my;
                        // keep j as next pending control
                        Cx = jx; Cy = jy;
                        hasC = true;
                    } else {
                        // first off after on: just store control
                        Cx = jx; Cy = jy;
                        hasC = true;
                    }
                }
            
                i = j;
            }
        
            // close contour to start point (Pstart = P0 at move time)
            // We need the geometric start point (the pen at emit_move). Store it:
        };
        
        // ---- call site per contour ----
        for (int c=0; c<ncontours; ++c) {
            sink.set_edge_color((uint8_t)(c % 3));
            const uint16_t end = ushort_(end_pts + 2*c);
            const uint16_t s   = start;
        
            // store start position to close correctly
            float startx, starty;
            {
                uint16_t first = s, last = end;
                if (is_on_(flags[first])) { startx=(float)px[first]; starty=(float)py[first]; }
                else if (is_on_(flags[last])) { startx=(float)px[last]; starty=(float)py[last]; }
                else { startx=0.5f*((float)px[last]+(float)px[first]); starty=0.5f*((float)py[last]+(float)py[first]); }
            }
        
            // emit contour with streaming normalisation
            {
                auto at = [&](uint16_t idx)->uint16_t { return (idx==end) ? s : (uint16_t)(idx+1); };
                auto X  = [&](uint16_t idx)->float { return (float)px[idx]; };
                auto Y  = [&](uint16_t idx)->float { return (float)py[idx]; };
                auto ON = [&](uint16_t idx)->bool  { return is_on_(flags[idx]); };
            
                float Cx=0, Cy=0; bool hasC=false;
            
                // init pen
                if (ON(s)) {
                    emit_move(X(s), Y(s));
                } else {
                    uint16_t last = end;
                    float sx = ON(last) ? X(last) : 0.5f*(X(last)+X(s));
                    float sy = ON(last) ? Y(last) : 0.5f*(Y(last)+Y(s));
                    emit_move(sx, sy);
                    Cx = X(s); Cy = Y(s); hasC=true;
                }
            
                uint16_t i = s;
                while (true) {
                    uint16_t j = at(i);
                    if (j == s) break;
                
                    float jx=X(j), jy=Y(j);
                    if (ON(j)) {
                        sink.set_edge_color(col);
                        if (hasC) {  emit_quad(Cx,Cy, jx,jy); hasC=false; }
                        else      { emit_line(jx,jy); }
                        next_col();
                    } else {
                        if (hasC) {
                            float mx=0.5f*(Cx+jx), my=0.5f*(Cy+jy);
                            sink.set_edge_color(col);
                            emit_quad(Cx,Cy, mx,my);
                            next_col();
                            Cx=jx; Cy=jy; hasC=true;
                        } else {
                            Cx=jx; Cy=jy; hasC=true;
                        }
                    }
                    i = j;
                }
            
                // finish back to start point
                sink.set_edge_color(col);
                if (hasC) emit_quad(Cx,Cy, startx,starty);
                else      emit_line(startx,starty);
                next_col();
            }
        
            // do not call sink.close() if you already explicitly returned to start
            // (otherwise you may double-close)
            start = (uint16_t)(end + 1);
        }
    }
    // ------------------------------------------------------------
    // COMPOSITE GLYPH
    // ------------------------------------------------------------
    else {
        const uint8_t* p = g;
        uint16_t flags = 0;
        do {
            flags = ushort_(p);
            p+=2;
            const uint16_t sub_glyf = ushort_(p);
            p+=2;

            // ---- parse args correctly (MUST advance p always) ----
            // TrueType flags:
            // 0x0001 ARGS_ARE_WORDS
            // 0x0002 ARGS_ARE_XY_VALUES
            // 0x0020 MORE_COMPONENTS
            // 0x0100 WE_HAVE_INSTRUCTIONS
            // 0x0008 WE_HAVE_A_SCALE
            // 0x0040 WE_HAVE_AN_X_AND_Y_SCALE
            // 0x0080 WE_HAVE_A_TWO_BY_TWO

            // args
            int16_t arg1=0, arg2=0;
            if (flags & 0x0001) {
                arg1 = short_(p);
                arg2 = short_(p+2);
                p+=4;
            }
            else {
                arg1 = (int8_t)p[0];
                arg2 = (int8_t)p[1];
                p+=2;
            }

            float e=0.f, f=0.f;
            if (flags & 0x0002) { e = (float)arg1; f = (float)arg2; } // XY values
            // else: point-to-point not supported (but parsed correctly)

            float a=1.f, b=0.f, c=0.f, d=1.f;
            if (flags & 0x0008) {
                a=d= short_(p + 0) / 16384.f;
                p+=2;
            }
            else if (flags & 0x0040) {
                a = short_(p + 0) / 16384.f;
                d = short_(p + 2) / 16384.f;
                p+=4;
            }
            else if (flags & 0x0080) {
                a = short_(p + 0) / 16384.f;
                b = short_(p + 2) / 16384.f;
                c = short_(p + 4) / 16384.f;
                d = short_(p + 6) / 16384.f;
                p+=8;
            }

            const Xform local{ a,b, c,d, e,f };
            const Xform child = xform_compose(xf, local);

            if (!RunGlyfStream((int)sub_glyf, sink, child, /*spread*/0.f, scratch, max_points))
                return false;

        } while (flags & 0x0020); // MORE_COMPONENTS
        // skip instructions if present
        if (flags & 0x0100) { // WE_HAVE_INSTRUCTIONS
            const uint16_t ilen = ushort_(p);
            p+=2;
            p+=ilen;
        }
    }
    return true;
} // RunGlyfStream


inline int Font::GetGlyfOffset(int glyph_index) const noexcept {
    int g1, g2;

    if (glyph_index >= _num_glyphs) return -1; // glyph index out of range
    if (_index_to_loc_format >= 2)  return -1; // unknown index->glyph map format

    if (_index_to_loc_format == 0) {
        g1 = _glyf + 2 * ushort_(_data + _loca + glyph_index * 2);
        g2 = _glyf + 2 * ushort_(_data + _loca + glyph_index * 2 + 2);
    }
    else {
        g1 = _glyf + ulong_(_data + _loca + glyph_index * 4);
        g2 = _glyf + ulong_(_data + _loca + glyph_index * 4 + 4);
    }
    return g1 == g2 ? -1 : g1; // if length is 0, return -1
}


// ============================================================================
//                         PUBLIC PARSING METHODS
// ============================================================================

inline bool Font::ReadBytes(uint8_t* font_buffer) noexcept {
    uint32_t cmap;
    int32_t i, num_tables;

    _data = font_buffer;

    cmap = FindTable("cmap");  // required
    _loca = FindTable("loca"); // required
    _head = FindTable("head"); // required
    _glyf = FindTable("glyf"); // required
    _hhea = FindTable("hhea"); // required
    _hmtx = FindTable("hmtx"); // required

    if (!cmap || !_head || !_hhea || !_hmtx)
        return 0;
    if (_glyf) {
        if (!_loca) return false; // required for truetype
    }

    {
        uint32_t t = FindTable("maxp");
        _num_glyphs = t ? ushort_(_data + t+4) : 0xffff;
    }

    // find a cmap encoding table we understand *now* to avoid searching
    // later. (todo: could make this installable)
    // the same regardless of glyph.
    num_tables = ushort_(_data + cmap+2);
    _index_map = 0;
    for (i=0; i<num_tables; ++i) {
        uint32_t encoding_record = cmap+4 + 8*i;
        PlatformId platform = static_cast<PlatformId>(ushort_(_data + encoding_record));

        // find an encoding we understand:
        switch (platform) {
        case PlatformId::Microsoft:
            switch (static_cast<EncodingIdMicrosoft>(
                ushort_(_data + encoding_record + 2))) {
            case EncodingIdMicrosoft::Unicode_Bmp:
            case EncodingIdMicrosoft::Unicode_Full: // MS/Unicode
                _index_map = cmap + ulong_(_data + encoding_record + 4);
                break;
            }
            break;
        case PlatformId::Unicode: // Mac/iOS has these
            // all the encodingIDs are unicode, so we don't bother to check it
            _index_map = cmap + ulong_(_data + encoding_record + 4);
            break;
        }
    }
    if (_index_map == 0)
        return false;

    _index_to_loc_format = ushort_(_data + _head + 50);
    return true;
}

inline float Font::ScaleForPixelHeight(float height) const noexcept {
    int h = short_(_data + _hhea+4) - short_(_data + _hhea+6);
    return height / static_cast<float>(h);
}

inline int Font::FindGlyphIndex(int unicode_codepoint) const noexcept {
    uint8_t* data = _data;
    uint32_t index_map = _index_map;

    uint16_t format = ushort_(data + index_map+0);
    if (format == 0) { // Apple byte encoding
        int32_t bytes = ushort_(data + index_map+2);
        if (unicode_codepoint < bytes - 6)
            return byte_(data + index_map+6 + unicode_codepoint);
        return 0;
    }
    else if (format == 6) {
        uint32_t first = ushort_(data + index_map+6);
        uint32_t count = ushort_(data + index_map+8);
        if (static_cast<uint32_t>(unicode_codepoint) >= first &&
            static_cast<uint32_t>(unicode_codepoint) < first+count)
            return ushort_(data + index_map+10 + 2*(unicode_codepoint-first));
        return 0;
    }
    else if (format == 2) {
        assert(0); // @TODO: high-byte mapping for japanese/chinese/korean
        return 0;
    }
    // standard mapping for windows fonts: binary search collection of ranges
    else if (format == 4) {
        uint16_t seg_count      = ushort_(data + index_map+6) >> 1;
        uint16_t search_range   = ushort_(data + index_map+8) >> 1;
        uint16_t entry_selector = ushort_(data + index_map+10);
        uint16_t range_shift    = ushort_(data + index_map+12) >> 1;

        // do a binary search of the segments
        uint32_t end_count = index_map + 14;
        uint32_t search = end_count;

        if (unicode_codepoint > 0xFFFF)
            return 0;

        // they lie from end_count .. end_count + seg_count
        // but search_range is the nearest power of two, so...
        if (unicode_codepoint >= ushort_(data + search + range_shift * 2))
            search += range_shift * 2;

        // now decrement to bias correctly to find smallest
        search -= 2;
        while (entry_selector) {
            uint16_t end;
            search_range >>= 1;
            end = ushort_(data + search + search_range * 2);
            if (unicode_codepoint > end)
                search += search_range * 2;
            --entry_selector;
        }
        search += 2;

        {
            uint16_t offset, start, last;
            uint16_t item = static_cast<uint16_t>((search - end_count) >> 1);

            start = ushort_(data + index_map + 14 + seg_count*2 + 2 + 2*item);
            last  = ushort_(data + end_count + 2*item);
            if (unicode_codepoint < start || unicode_codepoint > last)
                return 0;

            offset = ushort_(data + index_map + 14 + seg_count*6 + 2 + 2*item);
            if (offset == 0)
                return static_cast<uint16_t>(
                    unicode_codepoint + short_(data + index_map + 14
                                              + seg_count*4 + 2 + 2*item));
            return ushort_(data + offset + (unicode_codepoint - start)*2
                          + index_map + 14 + seg_count*6 + 2 + 2 *item);
        }
    }
    else if (format==12 || format==13) {
        uint32_t n_groups = ulong_(data + index_map + 12);
        int32_t low, high;
        low = 0; high = static_cast<int32_t>(n_groups);
        // Binary search the right group.
        while (low < high) {
            int32_t mid = low + ((high-low) >> 1); // rounds down, so low <= mid < high
            uint32_t start_char = ulong_(data + index_map + 16 + mid*12);
            uint32_t end_char   = ulong_(data + index_map + 16 + mid*12 + 4);
            if (static_cast<uint32_t>(unicode_codepoint) < start_char)
                high = mid;
            else if (static_cast<uint32_t>(unicode_codepoint) > end_char)
                low = mid + 1;
            else {
                uint32_t start_glyph = ulong_(data + index_map + 16 + mid*12 + 8);
                if (format==12)
                    return start_glyph + unicode_codepoint - start_char;
                else // format == 13
                    return start_glyph;
            }
        }
        return 0; // not found
    }
    // @TODO
    assert(0);
    return 0;
}

inline GlyphHorMetrics Font::GetGlyphHorMetrics(int glyph_index) const noexcept {
    // num of long hor metrics
    uint16_t num = ushort_(_data + _hhea + 34);
    return glyph_index < num ?
        GlyphHorMetrics{ short_(_data+_hmtx +   4*glyph_index),
                         short_(_data+_hmtx +   4*glyph_index + 2) }
        : GlyphHorMetrics{ short_(_data+_hmtx + 4*(num-1)),
                           short_(_data+_hmtx + 4*num + 2*(glyph_index - num))};
}



// ============================================================================
//                         STATIC  METHODS
// ============================================================================

inline int Font::GetFontOffsetForIndex(uint8_t* buff, int index) noexcept {
    if (is_font(buff)) // if it's just a font, there's only one valid index
        return index==0 ? 0 : -1;
    if (tag_(buff, "ttcf")) { // check if it's a TTC
        // version 1?
        if (ulong_(buff+4) == 0x00010000 ||
            ulong_(buff+4) == 0x00020000) {
            int32_t n = long_(buff+8);
            if (index >= n) return -1;
            return ulong_(buff+12 + index*4);
        }
    }
    return -1;
}


inline int Font::GetNumberOfFonts(const uint8_t* buff) noexcept {
    // if it's just a font, there's only one valid font
    if (is_font(buff)) return 1;
    if (tag_(buff, "ttcf")) { // check if it's a TTC
        // version 1?
        if (ulong_(buff+4) == 0x00010000 || ulong_(buff+4) == 0x00020000) {
            return long_(buff+8);
        }
    }
    return 0;
}

// ============================================================================
//                         PRIVATE PARSING METHODS
// ============================================================================

inline uint32_t Font::FindTable(const char* tag) const noexcept {
    int32_t num_tables = ushort_(_data + 4);
    const uint32_t table_dir = 12;

    for (int i = 0; i < num_tables; ++i) {
        uint32_t loc = table_dir + 16 * i;
        if (tag_(_data + loc+0, tag))
            return ulong_(_data + loc+8);
    }
    return 0;
}


inline bool Font::parse_glyph_plan_info_(const uint8_t* data,
                                         int loca, int glyf,
                                         int index_to_loc_format,
                                         int num_glyphs,
                                         int glyph_index,
                                         GlyphPlanInfo& out) noexcept {
    out.is_empty = true;
    out.max_points_in_tree = 0;

    if (glyph_index < 0 || glyph_index >= num_glyphs) return false;

    uint32_t g0 = glyph_offset_for_index_(data, loca, glyf, index_to_loc_format, glyph_index);
    uint32_t g1 = glyph_offset_for_index_(data, loca, glyf, index_to_loc_format, glyph_index + 1);
    if (g0 == g1) { // empty
        out.x_min = out.y_min = out.x_max = out.y_max = 0;
        return true;
    }

    const uint8_t* g = data + g0;
    int16_t num_contours = read_s16_be_(g+0);
    out.x_min = read_s16_be_(g+2);
    out.y_min = read_s16_be_(g+4);
    out.x_max = read_s16_be_(g+6);
    out.y_max = read_s16_be_(g+8);
    out.is_empty = false;

    // SIMPLE: max_points_in_tree = num_points
    if (num_contours >= 0) {
        const uint8_t* p = g + 10;
        const uint8_t* end_pts = p;
        uint16_t ncontours = (uint16_t)num_contours;
        if (ncontours == 0) { out.max_points_in_tree = 0; return true; }
        uint16_t last_end = read_u16_be_(end_pts + 2*(ncontours-1));
        out.max_points_in_tree = (uint16_t)(last_end + 1);
        return true;
    }

    // COMPOSITE: traverse components and take max_points among subglyphs
    // Minimal depth-limited DFS without heap.
    // TrueType composite flags:
    // 0x0020 MORE_COMPONENTS
    // 0x0001 ARG_1_AND_2_ARE_WORDS
    // 0x0002 ARGS_ARE_XY_VALUES
    // 0x0008 WE_HAVE_A_SCALE
    // 0x0040 WE_HAVE_AN_X_AND_Y_SCALE
    // 0x0080 WE_HAVE_A_TWO_BY_TWO
    // 0x0100 WE_HAVE_INSTRUCTIONS
    // We only need to skip argument/transform bytes and read sub glyph indices.

    uint16_t stack[32];
    uint8_t sp = 0;
    stack[sp++] = (uint16_t)glyph_index;

    uint16_t maxp = 0;

    while (sp) {
        uint16_t gi = stack[--sp];

        uint32_t sg0 = glyph_offset_for_index_(data, loca, glyf, index_to_loc_format, gi);
        uint32_t sg1 = glyph_offset_for_index_(data, loca, glyf, index_to_loc_format, gi+1);
        if (sg0 == sg1) continue;

        const uint8_t* sg = data + sg0;
        int16_t sc = read_s16_be_(sg+0);

        if (sc >= 0) {
            const uint8_t* p = sg+10;
            const uint8_t* end_pts = p;
            uint16_t ncontours = (uint16_t)sc;
            if (ncontours) {
                uint16_t last_end = read_u16_be_(end_pts + 2 * (ncontours - 1));
                uint16_t np = (uint16_t)(last_end + 1);
                if (np > maxp) maxp = np;
            }
            continue;
        }

        // composite: parse components
        const uint8_t* p = sg + 10;
        for (;;) {
            uint16_t flags = read_u16_be_(p); p += 2;
            uint16_t sub = read_u16_be_(p); p += 2;

            if (sub < (uint16_t)num_glyphs && sp < 32) stack[sp++] = sub;

            // skip args
            if (flags & 0x0001) p += 4; // words
            else                p += 2; // bytes

            // skip transform
            if (flags & 0x0008) p += 2;
            else if (flags & 0x0040) p += 4;
            else if (flags & 0x0080) p += 8;

            if (!(flags & 0x0020)) {
                if (flags & 0x0100) {
                    uint16_t ilen = read_u16_be_(p); // actually instructions len is at end; but in glyf composite, instructions follow after all components:
                    // Spec: if WE_HAVE_INSTRUCTIONS, then after components: uint16 instr_len; instr[instr_len]
                    // We don't need to parse further for plan.
                    (void)ilen;
                }
                break;
            }
        }
    }

    out.max_points_in_tree = maxp;
    return true;
}
} // namespace stb_stream






