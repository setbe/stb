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

#if defined(_DEBUG) || !defined(NDEBUG)
#   include <assert.h>
#   define STBTT_assert(x) assert(x)
#else
#   define STBTT_assert
#endif

namespace stbtt_stream {
enum class DfMode : uint8_t { SDF = 1, MSDF = 3 };
// some of the values for the IDs are below; for more see the truetype spec:
// Apple: http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
//   (archive) https://web.archive.org/web/20090113004145/http://developer.apple.com/textfonts/TTRefMan/RM06/Chap6name.html
// Microsoft: http://www.microsoft.com/typography/otspec/name.htm
//   (archive) https://web.archive.org/web/20090213110553/http://www.microsoft.com/typography/otspec/name.htm
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

struct BBox {
    float x_min, y_min;
    float x_max, y_max;
};
struct GlyphHorMetrics {
    int advance;
    int lsb; // left side bearing
};
struct GlyphInfo {
    uint16_t atlas_x;
    uint16_t atlas_y;
    GlyphHorMetrics metrics;
};
struct CodepointGlyph {
    uint32_t codepoint;
    uint16_t glyph_index;
};
struct GlyphMetrics {
    int advance;
    int lsb;
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
};
struct GlyphPlanInfo {
    int16_t x_min, y_min, x_max, y_max;
    uint16_t max_points_in_tree; // max num_points among simple subglyphs
    bool is_empty;
};
struct GlyphScratch {
    uint8_t* flags;  // [max_points]
    int16_t* px;     // [max_points]
    int16_t* py;     // [max_points]

    uint16_t* min_d2; // [max_area]
    uint8_t* inside; // [max_area]
    float* xs;     // [max_xs]
};
struct FontPlan {
    DfMode mode;
    uint8_t cell_size;     // <=64
    float spread;          // font units
    float scale;           // pixels per font unit (typically ScaleForPixelHeight(cell_size))

    uint32_t glyph_count;  // planned glyphs
    uint32_t max_area;     // max rect.w*rect.h across glyphs
    uint16_t max_points;   // for scratch sizing
    uint16_t max_xs;       // scanline intersection cap

    uint16_t atlas_side;   // square side in pixels (no padding)

    GlyphPlan* glyphs;     // user-allocated array [glyph_count]
};
struct AtlasInfo {
    uint32_t glyph_count;

    uint32_t width;   // pixels

    uint32_t rows;

    uint16_t cells_per_row;
    uint8_t cell_size;   // size of one glyph cell in pixels (<=64)
    uint8_t padding;     // pixels between cells
};

static constexpr size_t align_up(size_t v, size_t a) noexcept { return (v + (a - 1)) & ~(a - 1); }
static inline size_t glyph_scratch_bytes(uint16_t max_points,
                                         uint32_t max_area,
                                         uint16_t max_xs,
                                         DfMode mode) noexcept {
    size_t off = 0;

    off = align_up(off, 16); off += max_points * sizeof(uint8_t);  // flags
    off = align_up(off, 16); off += max_points * sizeof(int16_t);  // px
    off = align_up(off, 16); off += max_points * sizeof(int16_t);  // py

    if (mode == DfMode::SDF) {
        off = align_up(off, 16); off += (size_t)max_area * sizeof(uint16_t); // min_d2
    }
    else {
        off = align_up(off, 16); off += (size_t)max_area * sizeof(uint16_t) * 3; // min_d2_r/g/b
    }
    off = align_up(off, 16); off += (size_t)max_area * sizeof(uint8_t); // inside
    off = align_up(off, 16); off += (size_t)max_xs * sizeof(float);     // xs

    return align_up(off, 16);
}
static inline GlyphScratch bind_glyph_scratch(void* mem,
                                              uint16_t max_points,
                                              uint32_t max_area,
                                              uint16_t max_xs,
                                              DfMode mode) noexcept {
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

    s.min_d2 = (uint16_t*)take((size_t)max_area * sizeof(uint16_t), 16);
    s.inside = (uint8_t*) take((size_t)max_area * sizeof(uint8_t), 16);
    s.xs     = (float*)   take((size_t)max_xs   * sizeof(float), 16);

    (void)mode; // @TODO MSDF
    return s;
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

// Basic Newton-Raphson sqrt approximation
static inline float sqrt(float x) noexcept {
    if (x<=0) return 0;
    float r = x;
    for (int i=0; i<5; ++i)
        r = 0.5f*(r + x/r);
    return r;
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
    // 4 segments is enough for UI SDF
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
    
struct SdfGrid {
    signed char* sdf;   // [w*h]
    int8_t* winding;    // [w*h], signed accumulator

    float scale;
    float max_dist;

    float origin_x;
    float origin_y;

    int w, h;
};
struct SdfGridFast {
    uint8_t* out;          // atlas pointer (full atlas)
    uint32_t out_stride;   // atlas width in pixels
    int shift_x, shift_y;  // cell top-left in atlas

    int w, h;              // cell_size
    float scale;           // pixels per font unit
    float inv_scale;       // font units per pixel
    float spread;          // in font units
    float origin_x, origin_y;

    // per-cell scratch
    uint16_t* min_d2;       // [w*h] (but backed by scratch max_area)
    uint8_t* inside;        // [w*h]
};

static inline void pixel_center_to_font(float& fx, float& fy, const SdfGridFast& g,
                                        int x, int y) noexcept {
    fx = g.origin_x + (x+.5f) * g.inv_scale;
    fy = g.origin_y + ((g.h-1 - y) + .5f) * g.inv_scale;
}



struct SdfWindingPass {
    SdfGrid& g;

    explicit SdfWindingPass(SdfGrid& grid) noexcept : g(grid) {}

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
struct SdfDistancePass {
    SdfGrid& g;

    SdfDistancePass() = delete;
    explicit SdfDistancePass(SdfGrid& grid) noexcept : g(grid) {}

    inline void begin() noexcept {
        for (int i = 0; i < g.w * g.h; ++i) g.sdf[i] = 127;
    }

    inline void line(float x0, float y0, float x1, float y1) noexcept {
        for (int y = 0; y < g.h; ++y) {
            float py = g.origin_y + ((g.h-1 - y) + 0.5f) / g.scale;
            for (int x = 0; x < g.w; ++x) {
                float px = g.origin_x + (x + 0.5f) / g.scale;
                int idx = y * g.w + x;
                float d2 = dist_line_sq(px, py, x0, y0, x1, y1);
                float d = sqrt(d2);
                float prev = static_cast <float> (g.sdf[idx]) / 127.0f * g.max_dist;
                if (d < prev) {
                    float nd = d / g.max_dist;
                    if (nd > 1) nd = 1;
                    g.sdf[idx] = static_cast <signed char> (nd * 127);
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
    SdfGridFast& g;

    explicit SdfDistanceBBoxPass(SdfGridFast& gg) noexcept : g(gg) {}

    inline void begin() noexcept {
        const int n = g.w * g.h;
        for (int i = 0; i < n; ++i) g.min_d2[i] = 0xFFFF;
    }
    inline void set_origin(float x, float y) noexcept { g.origin_x = x; g.origin_y = y; }

    inline void line(float x0, float y0, float x1, float y1) noexcept {
        // font-space bbox expanded by spread
        float minx = (x0 < x1 ? x0 : x1) - g.spread;
        float maxx = (x0 > x1 ? x0 : x1) + g.spread;
        float miny = (y0 < y1 ? y0 : y1) - g.spread;
        float maxy = (y0 > y1 ? y0 : y1) + g.spread;

        int px0 = (int)((minx - g.origin_x) * g.scale);
        int px1 = (int)((maxx - g.origin_x) * g.scale);
        if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
        if (px0 < 0) px0 = 0;
        if (px1 >= g.w) px1 = g.w - 1;

        // y is flipped, so we clamp by scanning all y but skip rows outside miny/maxy
        for (int y = 0; y < g.h; ++y) {
            float fx_dummy, fy;
            pixel_center_to_font(fx_dummy, fy, g, 0, y);
            if (fy < miny || fy > maxy) continue;

            for (int x = px0; x <= px1; ++x) {
                float fx, fy2;
                pixel_center_to_font(fx, fy2, g, x, y);

                float d2 = dist_line_sq(fx, fy2, x0, y0, x1, y1);

                // normalize by spread^2
                float inv_s2 = (g.spread > 0.f) ? (1.f / (g.spread * g.spread)) : 0.f;
                float nd2 = d2 * inv_s2;          // 0..inf
                if (nd2 > 1.f) nd2 = 1.f;         // clamp to spread
                if (nd2 < 0.f) nd2 = 0.f;

                uint16_t ud2 = (uint16_t)(nd2 * 65535.f + 0.5f);

                int idx = y * g.w + x;
                if (ud2 < g.min_d2[idx]) g.min_d2[idx] = ud2;;
            }
        }
    }
};
struct SdfSignScanlinePass {
    SdfGridFast& g;

    // scratch per row
    float* xs;      // [max_intersections]
    int   max_xs;
    int   count;
    float scan_y;

    explicit SdfSignScanlinePass(SdfGridFast& gg, float* xs_buf, int xs_cap) noexcept
        : g(gg), xs(xs_buf), max_xs(xs_cap), count(0), scan_y(0.f) {}

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

    inline void line(float x0, float y0, float x1, float y1) noexcept {
        // standard half-open rule to avoid double counting vertices
        float ay = y0, by = y1;
        float ax = x0, bx = x1;

        if (ay > by) { float tx = ax; ax = bx; bx = tx; float ty = ay; ay = by; by = ty; }

        if (!(scan_y > ay && scan_y <= by)) return; // (ay, by]
        float t = (scan_y - ay) / (by - ay);
        float ix = ax + t * (bx - ax);

        if (count < max_xs) xs[count++] = ix;
    }

    static inline void sort_small(float* a, int n) noexcept {
        // insertion sort, n small
        for (int i = 1; i < n; ++i) {
            float v = a[i];
            int j = i - 1;
            while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; --j; }
            a[j + 1] = v;
        }
    }

    inline void finalize_row(int y) noexcept {
        sort_small(xs, count);

        // parity fill: pairs [x0,x1], [x2,x3], ...
        // write into g.inside[y*w + x]
        int w = g.w;
        for (int x = 0; x < w; ++x) g.inside[y * w + x] = 0;

        for (int i = 0; i + 1 < count; i += 2) {
            float x0 = xs[i];
            float x1 = xs[i + 1];

            // convert font-space x to pixel indices
            int px0 = (int)((x0 - g.origin_x) * g.scale);
            int px1 = (int)((x1 - g.origin_x) * g.scale);

            if (px0 > px1) { int t = px0; px0 = px1; px1 = t; }
            if (px0 < 0) px0 = 0;
            if (px1 >= w) px1 = w - 1;

            for (int x = px0; x <= px1; ++x) g.inside[y * w + x] = 1;
        }
    }
};
template<class Sink>
static void emit_quad(Sink& s,
                      float x0, float y0, float x1, float y1,
                      float x2, float y2, float flat) noexcept {
    

    float dx, dy;
    {
        const float mx = (x0+2*x1+x2)*.25f;   const float lx = (x0+x2)*.5f;
        const float my = (y0+2*y1+y2)*.25f;   const float ly = (y0+y2)*.5f;
                    dx = mx-lx;                           dy = my-ly;
    }
    if (dx*dx + dy*dy <= flat) {
        s.line(x0,y0, x2,y2);
        return;
    }

    const float x01 = (x0+x1)*.5f;   const float x12 = (x1+x2)*.5f;
    const float y01 = (y0+y1)*.5f;   const float y12 = (y1+y2)*.5f;
    const float xo  = (x01+x12)*.5f; const float yo  = (y01+y12)*.5f;
    emit_quad(s, x0,y0, x01,y01, xo,yo, flat);
    emit_quad(s, xo,yo, x12,y12, x2,y2, flat);
}

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
    virtual ~GlyphSink() noexcept = default;
};
template<class Pass> struct SdfSink final : GlyphSink {
    Pass& pass;
    float x{}, y{};
    float sx{}, sy{};
    bool open{ false };

    SdfSink() = delete;
    explicit SdfSink(Pass& p) noexcept : pass(p) {}

    inline void begin() noexcept override { pass.begin(); }
    inline void set_origin(float x, float y) noexcept override { pass.set_origin(x,y); }

    inline void move(float nx, float ny) noexcept override {
        if (open && (x != sx || y != sy))
            pass.line(x,y, sx,sy);
        x = sx = nx;
        y = sy = ny;
        open = true;
    }

    inline void line(float nx, float ny) noexcept override {
        pass.line(x,y, nx,ny);
        x = nx; y = ny;
    }

    inline void quad(float cx, float cy,
                     float nx, float ny) noexcept override {
        emit_quad(pass, x,y, cx,cy, nx,ny, 0.25f);
        x = nx; y = ny;
    }

    inline void close() noexcept override {
        if (open && (x != sx || y != sy))
            pass.line(x,y, sx,sy);
        open = false;
    }

    inline void cubic(float cx1, float cy1,
                      float cx2, float cy2,
                      float nx, float ny) noexcept override {
        const int STEPS = 8;
        float ax = x;
        float ay = y;

        for (int i=1; i<=STEPS; ++i) {
            float t = static_cast<float>(i) / STEPS;
            float mt = 1.f - t;
            float bx = (mt*mt*mt) * x    +  3*(mt*mt)*t * cx1
                     + 3*mt * t*t * cx2  +  t*t*t * nx;

            float by = (mt*mt*mt) * y    +  3*(mt*mt)*t * cy1 +
                     + 3*mt * t*t * cy2  +  t*t*t * ny;

            pass.line(ax,ay, bx,by);
            ax=bx; ay=by;
        }

        x = nx; y = ny;
    }

    // ----------- RELATIVE METHODS ------------
    inline void rmove(float dx, float dy) noexcept { move(x+dx, y+dy); }
    inline void rline(float dx, float dy) noexcept { line(x+dx, y+dy); }
    inline void rcubic(float cx1, float cy1,
                       float cx2, float cy2,
                       float nx, float ny) noexcept {
        cubic(x+cx1, y+cy1, x+cx2, y+cy2, x+nx, y+ny);
    }

}; // struct SdfSink

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
struct PlanResult {
    bool ok;
    uint32_t planned;
    uint16_t atlas_side;
    uint16_t max_points;
    uint16_t max_w;
    uint16_t max_h;
    uint32_t max_area;
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
        if ((uint32_t)y + rh > 0xFFFF) return 0xFFFF; // defensive
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
        if (y == 0xFFFF) continue;

        // heuristic: minimal y, then minimal width
        if (y < best_y || (y == best_y && s.nodes[i].w < best_w)) {
            best_y = y;
            best_idx = i;
            best_w = s.nodes[i].w;
        }
    }

    if (best_idx < 0) return false;

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


struct Font {
    explicit Font() noexcept = default;
    ~Font() noexcept = default;

    inline bool ReadBytes(uint8_t* font_buffer) noexcept;
    inline float ScaleForPixelHeight(float height) const noexcept;
    inline int FindGlyphIndex(int unicode_codepoint) const noexcept;
    inline GlyphHorMetrics GetGlyphHorMetrics(int glyph_index) const noexcept;

    // 1 glyph, independent: unrelated to passes, streams glyph
    inline bool StreamSDF(const GlyphPlan& gp,
                          unsigned char* atlas,
                          uint32_t atlas_width_px, // atlas stride
                          float scale,          // pixels per font unit
                          float spread,         // font units
                          GlyphScratch& scratch,
                          uint16_t max_points,  // from plan
                          uint32_t max_area,    // from plan
                          uint16_t max_xs       // from plan
                         ) noexcept;

    // PASS 1
    inline PlanResult PlanSDF(FontPlan& plan,
                              const uint32_t* codepoints, uint32_t count,
                              GlyphPlan* glyphs,          uint32_t glyph_cap,
                              SkylineNode* skyline_nodes, int skyline_node_cap,
                              uint16_t max_xs) noexcept;
    // PASS 2
    inline bool BuildSDF(const FontPlan& plan,
                         uint8_t* atlas,    uint32_t atlas_stride,
                         void* scratch_mem, size_t scratch_bytes) noexcept;

    
    // public helper (tiny, no skyline, no passes)
    inline bool GetGlyphPlanInfo(int glyph_index, GlyphPlanInfo& out) const noexcept {
        return parse_glyph_plan_info_(_data, _loca, _glyf, _index_to_loc_format, _num_glyphs, glyph_index, out);
    }

public:
    static inline int GetFontOffsetForIndex(uint8_t* font_buffer, int index) noexcept;
    static inline int GetNumberOfFonts(const uint8_t* font_buffer) noexcept;

private:
    bool RunGlyfStream(int glyph_index, GlyphSink& sink, float spread,
                       GlyphScratch& scratchі, uint16_t max_points) noexcept;

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
    static inline bool is_on_u8_(uint8_t f) noexcept { return (f & 0x80) != 0; } // our reserved bit
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

inline bool Font::StreamSDF(const GlyphPlan& gp,
                            unsigned char* atlas,
                            uint32_t atlas_width_px,
                            float scale,          // pixels per font unit
                            float spread,         // font units
                            GlyphScratch& scratch,
                            uint16_t max_points,
                            uint32_t max_area,
                            uint16_t max_xs) noexcept {
    STBTT_assert(atlas);
    if (!atlas) return false;

    STBTT_assert(gp.rect.w != 0 && gp.rect.h != 0);
    if (gp.rect.w == 0 || gp.rect.h == 0) return false;

    const int w = (int)gp.rect.w;
    const int h = (int)gp.rect.h;
    const uint32_t area = (uint32_t)w * (uint32_t)h;

    STBTT_assert(area <= (uint32_t)max_area);
    if (area > (uint32_t)max_area) return false;

    STBTT_assert(max_xs != 0);
    if (max_xs == 0) return false;

    SdfGridFast gg{};
    gg.out = atlas;
    gg.out_stride = atlas_width_px;
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

    gg.min_d2 = scratch.min_d2;
    gg.inside = scratch.inside;

    // 1) distance pass (bbox)
    {
        SdfDistanceBBoxPass pass(gg);
        SdfSink<SdfDistanceBBoxPass> sink(pass);
        if (!RunGlyfStream(gp.glyph_index, sink, spread, scratch, max_points))
            return false;
    }

    // 2) sign pass (scanline parity) - baseline (h times)
    {
        SdfSignScanlinePass pass(gg, scratch.xs, (int)max_xs);
        SdfSink<SdfSignScanlinePass> sink(pass);

        for (int y=0; y<h; ++y) {
            pass.begin_row(y);
            if (!RunGlyfStream(gp.glyph_index, sink, spread, scratch, max_points))
                return false;
            pass.finalize_row(y);
        }
    }

    // 3) finalize to atlas (no padding)
    const float inv_max_d = spread>0.f ? 1.f/spread : 0.f;

    for (int y=0; y<h; ++y) {
        uint8_t* dst = gg.out + (gg.shift_y + y) * gg.out_stride + gg.shift_x;
        for (int x=0; x<w; ++x) {
            const int idx = y*w + x;
    
            float nd2 = (float)gg.min_d2[idx] * (1.f / 65535.f); // normalized d^2
            float nd  = sqrt(nd2);                               // normalized d
            if (nd > 1.f) nd = 1.f;
    
            int sd = (int)(nd * 127.f + 0.5f);
            if (gg.inside[idx]) sd = -sd;
    
            dst[x] = (uint8_t)(128 + sd);
        }
    }


    return true;
}

inline PlanResult Font::PlanSDF(FontPlan& plan,
                                const uint32_t* codepoints,
                                uint32_t codepoint_count,
                                GlyphPlan* out_glyphs,
                                uint32_t out_cap,
                                SkylineNode* skyline_nodes,
                                int skyline_node_cap,
                                uint16_t max_xs) noexcept {
    PlanResult r{};
    r.ok = false;

    plan.glyphs = out_glyphs;
    plan.glyph_count = 0;
    plan.max_points = 0;
    plan.max_xs = max_xs;
    plan.atlas_side = 0;

    uint32_t total_area = 0;
    uint32_t max_area = 0;
    uint16_t max_w = 0, max_h = 0;

    // 1) fill glyph list
    for (uint32_t i = 0; i < codepoint_count; ++i) {
        if (plan.glyph_count >= out_cap) break;

        uint32_t cp = codepoints[i];
        int gi = FindGlyphIndex((int)cp);
        if (gi <= 0) continue; // skip missing (and typically .notdef)

        GlyphPlanInfo gpi{};
        if (!parse_glyph_plan_info_(_data, _loca, _glyf, _index_to_loc_format,
                                    _num_glyphs, gi, gpi))
            continue;
        if (gpi.is_empty) continue;

        GlyphPlan& gp = out_glyphs[plan.glyph_count++];
        gp.codepoint = cp;
        gp.glyph_index = (uint16_t)gi;
        gp.x_min = gpi.x_min;
        gp.y_min = gpi.y_min;
        gp.x_max = gpi.x_max;
        gp.y_max = gpi.y_max;
        gp.num_points = gpi.max_points_in_tree; // max points among subglyphs; safe for scratch

        // rect size in pixels (bbox + 2*spread)
        float span_x = (float)(gp.x_max-gp.x_min) + 2.0f * plan.spread;
        float span_y = (float)(gp.y_max-gp.y_min) + 2.0f * plan.spread;

        uint16_t rw = ceil_to_u16(span_x * plan.scale);
        uint16_t rh = ceil_to_u16(span_y * plan.scale);

        gp.rect.w = rw;
        gp.rect.h = rh;
        gp.rect.x = 0;
        gp.rect.y = 0;

        plan.max_points = u16_max(plan.max_points, gp.num_points);

        uint32_t a = (uint32_t)rw * (uint32_t)rh;
        total_area += a;
        if (a > max_area) max_area = a;
        max_w = u16_max(max_w, rw);
        max_h = u16_max(max_h, rh);
    }

    if (plan.glyph_count == 0) return r;

    // ensure skyline node cap is ok (rough safe bound)
    // skyline tends to need <= 2*N + 1 nodes; add headroom
    if (skyline_node_cap < (int)(2 * plan.glyph_count + 16)) return r;

    // 2) choose atlas side and pack; try side *=2 until success
    uint16_t side = next_pow2_u16(ceil_sqrt_u32(total_area));
    if (side < max_w) side = next_pow2_u16(max_w);
    if (side < max_h) side = next_pow2_u16(max_h);
    if (side < 64) side = 64;

    for (int attempt = 0; attempt < 20; ++attempt) {
        Skyline sk;
        skyline_init(sk, side, skyline_nodes, skyline_node_cap);

        bool ok = true;
        for (uint32_t i = 0; i < plan.glyph_count; ++i) {
            GlyphPlan& gp = plan.glyphs[i];
            uint16_t x, y;
            if (!skyline_insert(sk, gp.rect.w, gp.rect.h, x, y)) { ok = false; break; }
            gp.rect.x = x;
            gp.rect.y = y;
        }

        if (ok) {
            plan.atlas_side = side;
            plan.max_area = max_area;
            plan.max_xs = max_xs;

            r.ok = true;
            r.planned = plan.glyph_count;
            r.atlas_side = side;
            r.max_points = plan.max_points;
            r.max_area = max_area;
            r.max_w = max_w;
            r.max_h = max_h;
            return r;
        }

        if (side >= 32768) break;
        side = (uint16_t)(side * 2);
    }

    return r;
}



inline bool Font::BuildSDF(const FontPlan& plan,
                           uint8_t* atlas,
                           uint32_t atlas_stride,
                           void* scratch_mem,
                           size_t scratch_bytes) noexcept {
    // verify scratch size
    if (!atlas || !scratch_mem) return false;
    if (atlas_stride < plan.atlas_side) return false;

    const size_t need = glyph_scratch_bytes(plan.max_points, plan.max_area, plan.max_xs, plan.mode);
    if (scratch_bytes < need) return false;

    GlyphScratch scratch = bind_glyph_scratch(scratch_mem, plan.max_points, plan.max_area, plan.max_xs, plan.mode);

    for (uint32_t i = 0; i < plan.glyph_count; ++i) {
        const GlyphPlan& gp = plan.glyphs[i];

        if ((uint32_t)gp.rect.x + gp.rect.w > plan.atlas_side) return false;
        if ((uint32_t)gp.rect.y + gp.rect.h > plan.atlas_side) return false;

        if (!StreamSDF(gp, atlas, atlas_stride,
                       plan.scale, plan.spread,
                       scratch,
                       plan.max_points, plan.max_area, plan.max_xs))
            return false;
    }
    return true;
}



// ============================================================================
//                         PRIVATE   METHODS
// ============================================================================


// TODO recursive composite glyphs
bool Font::RunGlyfStream(int glyph_index, GlyphSink& sink, float spread,
                         GlyphScratch& scratch, uint16_t max_points) noexcept {
    if (!_glyf || !_loca) return false;
    if ((unsigned)glyph_index >= (unsigned)_num_glyphs) return false;

    auto glyph_offset = [&](int g) -> uint32_t {
        return _index_to_loc_format==0 ?
              _glyf + 2*ushort_(_data + _loca+2 * g)
            : _glyf +    ulong_(_data + _loca+4 * g);
    };

    uint32_t g0 = glyph_offset(glyph_index);
    uint32_t g1 = glyph_offset(glyph_index + 1);
    if (g0 == g1) return false; // empty glyph

    const uint8_t* g = _data + g0;

    int16_t num_contours = short_(g);
    g += 10;

    sink.begin();

    // ------------------------------------------------------------
    // SIMPLE GLYPH
    // ------------------------------------------------------------
    if (num_contours >= 0) {
        const int ncontours = num_contours;

        // --- end points of contours ---
        const uint8_t* end_pts = g;
        g += 2 * ncontours;

        const uint16_t num_points = ushort_(end_pts + 2 * (ncontours - 1)) + 1;
        STBTT_assert(num_points <= max_points);
        if (num_points > max_points) return false;

        // --- instructions ---
        const uint16_t instr_len = ushort_(g);
        g += 2 + instr_len;

        // --- read flags ---
        uint8_t* flags = scratch.flags;
        int16_t* px = scratch.px;
        int16_t* py = scratch.py;

        uint16_t fcount = 0;
        while (fcount < num_points) {
            uint8_t f = *g++;
            flags[fcount++] = f;
            if (f & 8) { // repeat
                uint8_t r = *g++;
                while (r-- && fcount < num_points) flags[fcount++] = f;
            }
        }

        // --- decode X coordinates ---
        int x = 0;
        for (uint16_t i = 0; i < num_points; ++i) {
            int dx = 0;
            if (flags[i] & 2) { // x-short
                uint8_t v = *g++;
                dx = (flags[i] & 16) ? (int)v : -(int)v;
            }
            else if (!(flags[i] & 16)) { // x is int16
                dx = short_(g);
                g += 2;
            } // else same as previous (dx=0)
            x += dx;
            px[i] = (int16_t)x;

            // cache on-curve into reserved bit (bit 7)
            set_on_u8_(flags[i], (flags[i] & 1) != 0);
        }

        // --- decode Y coordinates ---
        int y = 0;
        for (uint16_t i = 0; i < num_points; ++i) {
            int dy = 0;
            if (flags[i] & 4) { // y-short
                uint8_t v = *g++;
                dy = (flags[i] & 32) ? (int)v : -(int)v;
            }
            else if (!(flags[i] & 32)) { // y is int16
                dy = short_(g);
                g += 2;
            }
            y += dy;
            py[i] = (int16_t)y;
        }

        // --- emit contours ---
        uint16_t start = 0;
        for (int c = 0; c < ncontours; ++c) {
            const uint16_t end = ushort_(end_pts + 2 * c);

            auto idx_prev = [&](uint16_t i) noexcept -> uint16_t { return i==start ? end : (uint16_t)(i-1); };
            auto idx_next = [&](uint16_t i) noexcept -> uint16_t { return i==end ? start : (uint16_t)(i+1); };
            auto on = [&](uint16_t i) noexcept -> bool { return is_on_u8_(flags[i]); };

            // --- choose start point (same logic as your original) ---
            uint16_t s = start;
            if (!on(s)) {
                uint16_t prev = idx_prev(s);
                if (!on(prev)) {
                    // start at midpoint of two off-curve points
                    sink.move(0.5f * (float(px[s]) + float(px[prev])),
                              0.5f * (float(py[s]) + float(py[prev])));
                }
                else {
                    sink.move((float)px[prev], (float)py[prev]);
                }
            }
            else {
                sink.move((float)px[s], (float)py[s]);
            }

            uint16_t i = s;
            while (true) {
                uint16_t ni = idx_next(i);

                if (on(i) && on(ni)) {
                    sink.line((float)px[ni], (float)py[ni]);
                }
                else if (on(i) && !on(ni)) {
                    uint16_t nn = idx_next(ni);
                    if (on(nn)) {
                        sink.quad((float)px[ni], (float)py[ni],
                                  (float)px[nn], (float)py[nn]);
                    }
                    else {
                        // off->off after on: implicit on at midpoint
                        float mx = 0.5f * (float(px[ni]) + float(px[nn]));
                        float my = 0.5f * (float(py[ni]) + float(py[nn]));
                        sink.quad((float)px[ni], (float)py[ni], mx, my);
                    }
                }
                else if (!on(i) && on(ni)) {
                    sink.quad((float)px[i], (float)py[i],
                              (float)px[ni], (float)py[ni]);
                }
                else { // off -> off
                    float mx = 0.5f * (float(px[i]) + float(px[ni]));
                    float my = 0.5f * (float(py[i]) + float(py[ni]));
                    sink.quad((float)px[i], (float)py[i], mx, my);
                }

                i = ni;
                if (i==s) break;
            }

            sink.close();
            start = (uint16_t)(end+1);
        }
    }



    // ------------------------------------------------------------
    // COMPOSITE GLYPH
    // ------------------------------------------------------------
    else {
        const uint8_t* p = g;
        uint16_t flags;

        do {
            flags        = ushort_(p); p+=2;
            uint16_t sub = ushort_(p); p+=2;

            float a=1.f, b=0.f, c=0.f, d=1.f, e=0.f, f=0.f;

            if (flags & 2) {
                if (flags & 1) {
                    e = short_(p); f = short_(p+2);
                    p += 4;
                }
                else {
                    e = static_cast<int8_t>(*p++);
                    f = static_cast<int8_t>(*p++);
                }
            }

            if (flags & 8) {
                a = d = short_(p) / 16384.f;
                p += 2;
            }
            else if (flags & 64) {
                a = short_(p+0) / 16384.f;
                d = short_(p+2) / 16384.f;
                p += 4;
            }
            else if (flags & 128) {
                a = short_(p+0) / 16384.f;
                b = short_(p+2) / 16384.f;
                c = short_(p+4) / 16384.f;
                d = short_(p+6) / 16384.f;
                p += 8;
            }

            struct XformSink final : GlyphSink {
                GlyphSink& out;

                // affine matrix:
                // [ m00 m01 dx ]
                // [ m10 m11 dy ]
                float m00, m01;
                float m10, m11;
                float dx, dy;

                XformSink(GlyphSink& out_,
                    float m00_, float m01_,
                    float m10_, float m11_,
                    float dx_, float dy_) noexcept
                    : out(out_)
                    , m00(m00_), m01(m01_)
                    , m10(m10_), m11(m11_)
                    , dx(dx_), dy(dy_)
                {}

                inline void begin() noexcept override { }
                inline void set_origin(float, float) noexcept override { }

                inline void close() noexcept override { }

                inline void move(float x, float y) noexcept override {
                    out.move(m00 * x + m01 * y + dx,
                             m10 * x + m11 * y + dy);
                }
                inline void line(float x, float y) noexcept override {
                    out.line(m00 * x + m01 * y + dx,
                             m10 * x + m11 * y + dy);
                }
                inline void quad(float cx, float cy,
                                 float x, float y) noexcept override {
                    out.quad(m00 * cx + m01 * cy + dx,
                             m10 * cx + m11 * cy + dy,
                             m00 * x  + m01 * y  + dx,
                             m10 * x  + m11 * y  + dy);
                }
                inline void cubic(float cx1, float cy1,
                                  float cx2, float cy2,
                                  float x,   float y) noexcept override {
                    out.cubic(m00 * cx1 + m01 * cy1 + dx,
                              m10 * cx1 + m11 * cy1 + dy,
                              m00 * cx2 + m01 * cy2 + dx,
                              m10 * cx2 + m11 * cy2 + dy,
                              m00 * x   + m01 * y   + dx,
                              m10 * x   + m11 * y   + dy);
                }
            } xs{ sink, a,b, c,d, e,f };

            if (!RunGlyfStream(sub, xs, spread, scratch, max_points))
                return false;

        } while (flags & 32);
    }

    return true;
}


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
        STBTT_assert(0); // @TODO: high-byte mapping for japanese/chinese/korean
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
    STBTT_assert(0);
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






