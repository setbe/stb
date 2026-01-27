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

#include "codepoints/stbtt_codepoints_stream.hpp"

#if defined(_DEBUG) || !defined(NDEBUG)
#   include <assert.h>
#   define STBTT_assert(x) assert(x)
#else
#   define STBTT_assert
#endif

namespace stbtt_stream {
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
    uint16_t* min_d2;      // [w*h] distance^2 scaled
    uint8_t* inside;      // [w*h] 0/1 (optional; can be bitset)
};

static inline void pixel_center_to_font(float& fx, float& fy,
    const SdfGridFast& g,
    int x, int y) noexcept {
    fx = g.origin_x + (x + 0.5f) * g.inv_scale;
    fy = g.origin_y + ((g.h - 1 - y) + 0.5f) * g.inv_scale;
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

                // compare without sqrt; scale d2 into uint16
                // scale factor picks precision vs range; tune as needed
                float scaled = d2 * 1024.0f;
                if (scaled < 0) scaled = 0;
                if (scaled > 65535.f) scaled = 65535.f;

                uint16_t ud2 = (uint16_t)(scaled + 0.5f);
                int idx = y * g.w + x;
                if (ud2 < g.min_d2[idx]) g.min_d2[idx] = ud2;
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
                      float x0, float y0,
                      float x1, float y1,
                      float x2, float y2,
                      float flat) noexcept {
    float mx = (x0 + 2*x1 + x2) * 0.25f;
    float my = (y0 + 2*y1 + y2) * 0.25f;

    float lx = (x0 + x2) * 0.5f;
    float ly = (y0 + y2) * 0.5f;

    float dx = mx - lx;
    float dy = my - ly;

    if (dx*dx + dy*dy <= flat) {
        s.line(x0,y0, x2,y2);
        return;
    }

    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float x012 = (x01 + x12) * 0.5f;
    float y012 = (y01 + y12) * 0.5f;

    emit_quad(s, x0,y0,     x01,y01, x012,y012, flat);
    emit_quad(s, x012,y012, x12,y12, x2,y2,     flat);
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


template<class Pass>
struct SdfSink final : GlyphSink {
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


struct GlyphHorMetrics {
    int advance;
    int lsb; // left side bearing
};

struct GlyphInfo {
    uint16_t atlas_x;
    uint16_t atlas_y;
    GlyphHorMetrics metrics;
};


struct AtlasInfo {
    uint32_t glyph_count;

    uint32_t width;   // pixels

    uint32_t rows;

    uint16_t cells_per_row;
    uint8_t cell_size;   // size of one glyph cell in pixels (<=64)
    uint8_t padding;     // pixels between cells
};

struct BBox {
    float x_min, y_min;
    float x_max, y_max;
};

struct Font {
    explicit Font() noexcept = default;
    ~Font() noexcept = default;

    inline bool ReadBytes(uint8_t* font_buffer) noexcept;
    inline float ScaleForPixelHeight(float height) const noexcept;
    inline int FindGlyphIndex(int unicode_codepoint) const noexcept;
    inline GlyphHorMetrics GetGlyphHorMetrics(int glyph_index) const noexcept;


    inline bool MakeGlyphSDF(
        int glyph_index,
        unsigned char* atlas,     // full atlas bitmap
        uint32_t atlas_width_px,  // atlas stride
        int shift_x,              // top-left of cell
        int shift_y,
        int cell_size,
        float scale,
        float spread
    ) noexcept;
 
    template<typename... Scripts>
    inline AtlasInfo AtlasStream(uint8_t cell_size,   // max glyph size in pixels (<=64)
                                uint8_t padding, // pixels between glyphs
                                Scripts...) noexcept;
    
    template<typename... Scripts>
    inline void StreamSDF(unsigned char* out,
                          float spread,
                          uint32_t atlas_size_px,
                          uint8_t cell_size,
                          uint8_t padding,
                          void(*callback)(uint32_t codepoint, bool ok), // optional, may be nullptr
                          Scripts...) noexcept;

    template<class Sink>
    inline void Stream(uint32_t atlas_size_px,   // e.g. 2048
                       uint8_t cell_size,
                       uint8_t padding,
                       Sink& sink,
                       ::stbtt_codepoints::Script...) noexcept;

    template<class Sink>
    bool BuildGlyph(int glyph_index, Sink& sink, float spread) noexcept;

    static inline int GetFontOffsetForIndex(uint8_t* font_buffer, int index) noexcept;
    static inline int GetNumberOfFonts(const uint8_t* font_buffer) noexcept;

private:
    bool RunGlyfStream(int glyph_index, GlyphSink& sink, float spread) noexcept;

    // --- Parsing helpers ---
    inline int GetGlyfOffset(int glyph_index) const noexcept;
    inline uint32_t FindTable(const char* tag) const noexcept;

    // --- Static parsing helpers ---
    static uint8_t  Byte(const uint8_t* p) noexcept   { return *p; }
    static int8_t   Char(const uint8_t* p) noexcept   { return *(const int8_t*)p; }
    static uint16_t Ushort(const uint8_t* p) noexcept { return p[0] * 256 + p[1]; }
    static int16_t  Short(const uint8_t* p) noexcept  { return p[0] * 256 + p[1]; }
    static uint32_t Ulong(const uint8_t* p) noexcept  { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
    static int32_t  Long(const uint8_t* p) noexcept   { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
    // --- helper (RunGlyfStream local) ---
    static inline bool is_on_u8(uint8_t f) noexcept { return (f & 0x80) != 0; } // our reserved bit
    static inline void set_on_u8(uint8_t& f, bool on) noexcept {
        f = (uint8_t)((f & 0x7F) | (on ? 0x80 : 0x00));
    }

    static bool Tag4(const uint8_t* p, char c0, char c1, char c2, char c3) noexcept {
        return p[0]==c0 && p[1]==c1 && p[2]==c2 && p[3]==c3;
    }
    static bool Tag(const uint8_t* p, const char* str) noexcept { return Tag4(p, str[0], str[1], str[2], str[3]); }
    static int IsFont(const uint8_t* font) noexcept {
        // check the version number
        if (Tag4(font, '1', 0, 0, 0))  return 1; // TrueType 1
        if (Tag(font, "typ1"))   return 1; // TrueType with type 1 font -- we don't support this!
        if (Tag(font, "true"))   return 1; // Apple specification for TrueType fonts
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

inline bool Font::MakeGlyphSDF(int glyph_index,
    unsigned char* atlas,
    uint32_t atlas_width_px,
    int shift_x, int shift_y,
    int cell_size,
    float scale,
    float spread) noexcept {
    constexpr int cell_max = 64;
    STBTT_assert(cell_size > 0 && cell_size <= cell_max);

    // scratch (can be moved to user-provided scratch later)
    static uint16_t min_d2[cell_max * cell_max];
    static uint8_t  inside[cell_max * cell_max];
    static float    xs[256]; // per-row intersections (cap; tune)

    SdfGridFast gg{};
    gg.out = atlas;
    gg.out_stride = atlas_width_px;
    gg.shift_x = shift_x;
    gg.shift_y = shift_y;
    gg.w = cell_size;
    gg.h = cell_size;
    gg.scale = scale;
    gg.inv_scale = 1.0f / scale;
    gg.spread = spread;
    gg.origin_x = 0;
    gg.origin_y = 0;
    gg.min_d2 = min_d2;
    gg.inside = inside;

    // 1) distance pass (bbox)
    {
        SdfDistanceBBoxPass pass(gg);
        SdfSink<SdfDistanceBBoxPass> sink(pass);
        if (!BuildGlyph(glyph_index, sink, spread))
            return false;
    }

    // 2) sign pass (scanline parity)
    {
        SdfSignScanlinePass pass(gg, xs, (int)(sizeof(xs) / sizeof(xs[0])));
        SdfSink<SdfSignScanlinePass> sink(pass);

        // we need origin set the same way; BuildGlyph calls set_origin via bbox
        // but we must fill inside row-by-row; easiest: run glyph per row (expensive).
        // better: add row loop into sink by giving pass a current row and calling begin_row/finalize_row.
        //
        // Minimal approach (still OK for 64px): for each row, run glyph stream once.
        // This is O(h*edges) but h<=64 and edges moderate; still way faster than your old O(h*w*edges).

        for (int y = 0; y < gg.h; ++y) {
            pass.begin_row(y);
            if (!BuildGlyph(glyph_index, sink, spread))
                return false;
            pass.finalize_row(y);
        }
    }

    // 3) finalize: map min_d2 + inside -> 8-bit SDF in atlas
    // Convert spread (font units) to max_d2 scale used above: d2*1024
    float max_d = spread;
    float inv_max_d = (max_d > 0) ? (1.0f / max_d) : 0.0f;

    for (int y = 0; y < cell_size; ++y) {
        for (int x = 0; x < cell_size; ++x) {
            int idx = y * cell_size + x;

            // back to float distance
            float d2 = (float)min_d2[idx] / 1024.0f;
            float d = sqrt(d2);

            float nd = d * inv_max_d; // 0..1
            if (nd > 1) nd = 1;

            int sd = (int)(nd * 127.0f + 0.5f);
            if (inside[idx]) sd = -sd;

            gg.out[(shift_y + y) * gg.out_stride + (shift_x + x)] = (uint8_t)(128 + sd);
        }
    }

    return true;
}





template<typename... Scripts>
AtlasInfo Font::AtlasStream(uint8_t cell_size,
                            uint8_t padding,
                            Scripts... scripts) noexcept {
    AtlasInfo info{};
    info.cell_size = cell_size;
    info.padding = padding;

    // 1. count glyphs (C++11, no fold-expr)
    {
        uint32_t sum = 0;
        int dummy[] = {
            (sum += ::stbtt_codepoints::CountGlyphs<Font>(*this, scripts), 0)...
        };
        (void)dummy;
        info.glyph_count = sum;
    }

    const uint32_t cell_pitch = cell_size + padding;

    // 2. quadratic atlas size
    uint32_t cells_per_row = static_cast<uint32_t>(
                                sqrt(static_cast<float>(
                                    info.glyph_count)));
    if (cells_per_row * cells_per_row < info.glyph_count)
        ++cells_per_row;

    info.cells_per_row = static_cast<uint16_t>(cells_per_row);

    // 3. rows
    info.rows = (info.glyph_count + cells_per_row - 1) / cells_per_row;

    // 4. width
    info.width = cells_per_row * cell_pitch;

    return info;
} // CountStream

template<typename... Scripts>
void Font::StreamSDF(unsigned char* out,
                     float spread,
                     uint32_t atlas_size_px,
                     uint8_t cell_size,
                     uint8_t padding,
                     void(*callback)(uint32_t codepoint, bool ok),
                     Scripts... scripts) noexcept {
    const uint32_t cell_pitch = cell_size + padding;
    const uint32_t cells_per_row = atlas_size_px / cell_pitch;
    uint32_t glyph_index = 0;

    auto emit = [&](uint32_t cp, int glyph) {
        const uint32_t col = glyph_index % cells_per_row;
        const uint32_t row = glyph_index / cells_per_row;

        bool ok = MakeGlyphSDF(
            glyph,
            out,
            atlas_size_px,
            int(col * cell_pitch),
            int(row * cell_pitch),
            cell_size,
            ScaleForPixelHeight((float)cell_size),
            spread
        );

        if (callback)
            callback(cp, ok);

        ++glyph_index;
    };

    // --- helper: expand scripts recursively ---
    auto stream_one = [&](auto script) { stbtt_codepoints::StreamGlyphs(*this, script, emit); };

    // expand manually
    int dummy[] = { (stream_one(scripts), 0)... }; (void)dummy;
} // StreamSDF


// ============================================================================
//                         PRIVATE   METHODS
// ============================================================================

template<class Sink>
bool Font::BuildGlyph(int glyph_index, Sink& sink, float spread) noexcept {
    if (_glyf) // TrueType
        return RunGlyfStream(glyph_index, sink, spread);
    return false;
}

inline int Font::GetGlyfOffset(int glyph_index) const noexcept {
    int g1, g2;

    if (glyph_index >= _num_glyphs) return -1; // glyph index out of range
    if (_index_to_loc_format >= 2)  return -1; // unknown index->glyph map format

    if (_index_to_loc_format==0) {
        g1 = _glyf + 2 * Ushort(_data + _loca+glyph_index*2);
        g2 = _glyf + 2 * Ushort(_data + _loca+glyph_index*2 + 2);
    }
    else {
        g1 = _glyf + Ulong(_data + _loca+glyph_index*4);
        g2 = _glyf + Ulong(_data + _loca+glyph_index*4 + 4);
    }
    return g1 == g2 ? -1 : g1; // if length is 0, return -1
}


// TODO recursive composite glyphs
bool Font::RunGlyfStream(int glyph_index, GlyphSink& sink, float spread) noexcept {
    if (!_glyf || !_loca) return false;
    if ((unsigned)glyph_index >= (unsigned)_num_glyphs) return false;

    auto glyph_offset = [&](int g) -> uint32_t {
        return _index_to_loc_format==0 ?
              _glyf + 2*Ushort(_data + _loca+2 * g)
            : _glyf +    Ulong(_data + _loca+4 * g);
    };

    uint32_t g0 = glyph_offset(glyph_index);
    uint32_t g1 = glyph_offset(glyph_index + 1);
    if (g0 == g1) return false; // empty glyph

    const uint8_t* g = _data + g0;

    int16_t num_contours = Short(g);    

    // bbox
    {
        BBox bbox;
        bbox.x_min = static_cast<float>(Short(g + 2));
        bbox.y_min = static_cast<float>(Short(g + 4));
        bbox.x_max = static_cast<float>(Short(g + 6));
        bbox.y_max = static_cast<float>(Short(g + 8));
        sink.set_origin(bbox.x_min - spread * 0.5f,
                        bbox.y_min - spread * 0.5f);
    }
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

        const uint16_t num_points = Ushort(end_pts + 2 * (ncontours - 1)) + 1;

        // --- instructions ---
        const uint16_t instr_len = Ushort(g);
        g += 2 + instr_len;

        // --- read flags (expanded) ---
        uint8_t flags[512];
        int16_t px[512];
        int16_t py[512];

        // guard against overflow
        if (num_points > 512) return false;

        uint16_t fcount = 0;
        while (fcount < num_points) {
            uint8_t f = *g++;
            flags[fcount++] = f;
            if (f & 8) { // repeat
                uint8_t r = *g++;
                // r is repeat count, repeats the flag r times *in addition* to the first
                while (r-- && fcount < num_points) {
                    flags[fcount++] = f;
                }
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
                dx = Short(g);
                g += 2;
            } // else same as previous (dx=0)
            x += dx;
            px[i] = (int16_t)x;

            // cache on-curve into reserved bit (bit 7)
            set_on_u8(flags[i], (flags[i] & 1) != 0);
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
                dy = Short(g);
                g += 2;
            }
            y += dy;
            py[i] = (int16_t)y;
        }

        // --- emit contours ---
        uint16_t start = 0;
        for (int c = 0; c < ncontours; ++c) {
            const uint16_t end = Ushort(end_pts + 2 * c);

            auto idx_prev = [&](uint16_t i) noexcept -> uint16_t {
                return (i == start) ? end : (uint16_t)(i - 1);
            };
            auto idx_next = [&](uint16_t i) noexcept -> uint16_t {
                return (i == end) ? start : (uint16_t)(i + 1);
            };
            auto on = [&](uint16_t i) noexcept -> bool {
                return is_on_u8(flags[i]);
            };

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
                if (i == s) break;
            }

            sink.close();
            start = (uint16_t)(end + 1);
        }
    }



    // ------------------------------------------------------------
    // COMPOSITE GLYPH
    // ------------------------------------------------------------
    else {
        const uint8_t* p = g;
        uint16_t flags;

        do {
            flags        = Ushort(p); p+=2;
            uint16_t sub = Ushort(p); p+=2;

            float a=1.f, b=0.f, c=0.f, d=1.f, e=0.f, f=0.f;

            if (flags & 2) {
                if (flags & 1) {
                    e = Short(p); f = Short(p+2);
                    p += 4;
                }
                else {
                    e = static_cast<int8_t>(*p++);
                    f = static_cast<int8_t>(*p++);
                }
            }

            if (flags & 8) {
                a = d = Short(p) / 16384.f;
                p += 2;
            }
            else if (flags & 64) {
                a = Short(p+0) / 16384.f;
                d = Short(p+2) / 16384.f;
                p += 4;
            }
            else if (flags & 128) {
                a = Short(p+0) / 16384.f;
                b = Short(p+2) / 16384.f;
                c = Short(p+4) / 16384.f;
                d = Short(p+6) / 16384.f;
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

            RunGlyfStream(sub, xs, spread);

        } while (flags & 32);
    }

    return true;
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
        _num_glyphs = t ? Ushort(_data + t+4) : 0xffff;
    }

    // find a cmap encoding table we understand *now* to avoid searching
    // later. (todo: could make this installable)
    // the same regardless of glyph.
    num_tables = Ushort(_data + cmap+2);
    _index_map = 0;
    for (i=0; i<num_tables; ++i) {
        uint32_t encoding_record = cmap+4 + 8*i;
        PlatformId platform = static_cast<PlatformId>(Ushort(_data + encoding_record));

        // find an encoding we understand:
        switch (platform) {
        case PlatformId::Microsoft:
            switch (static_cast<EncodingIdMicrosoft>(
                Ushort(_data + encoding_record + 2))) {
            case EncodingIdMicrosoft::Unicode_Bmp:
            case EncodingIdMicrosoft::Unicode_Full: // MS/Unicode
                _index_map = cmap + Ulong(_data + encoding_record + 4);
                break;
            }
            break;
        case PlatformId::Unicode: // Mac/iOS has these
            // all the encodingIDs are unicode, so we don't bother to check it
            _index_map = cmap + Ulong(_data + encoding_record + 4);
            break;
        }
    }
    if (_index_map == 0)
        return false;

    _index_to_loc_format = Ushort(_data + _head + 50);
    return true;
}

inline float Font::ScaleForPixelHeight(float height) const noexcept {
    int h = Short(_data + _hhea+4) - Short(_data + _hhea+6);
    return height / static_cast<float>(h);
}

inline int Font::FindGlyphIndex(int unicode_codepoint) const noexcept {
    uint8_t* data = _data;
    uint32_t index_map = _index_map;

    uint16_t format = Ushort(data + index_map+0);
    if (format == 0) { // Apple byte encoding
        int32_t bytes = Ushort(data + index_map+2);
        if (unicode_codepoint < bytes - 6)
            return Byte(data + index_map+6 + unicode_codepoint);
        return 0;
    }
    else if (format == 6) {
        uint32_t first = Ushort(data + index_map+6);
        uint32_t count = Ushort(data + index_map+8);
        if (static_cast<uint32_t>(unicode_codepoint) >= first &&
            static_cast<uint32_t>(unicode_codepoint) < first+count)
            return Ushort(data + index_map+10 + 2*(unicode_codepoint-first));
        return 0;
    }
    else if (format == 2) {
        STBTT_assert(0); // @TODO: high-byte mapping for japanese/chinese/korean
        return 0;
    }
    // standard mapping for windows fonts: binary search collection of ranges
    else if (format == 4) {
        uint16_t seg_count      = Ushort(data + index_map+6) >> 1;
        uint16_t search_range   = Ushort(data + index_map+8) >> 1;
        uint16_t entry_selector = Ushort(data + index_map+10);
        uint16_t range_shift    = Ushort(data + index_map+12) >> 1;

        // do a binary search of the segments
        uint32_t end_count = index_map + 14;
        uint32_t search = end_count;

        if (unicode_codepoint > 0xFFFF)
            return 0;

        // they lie from end_count .. end_count + seg_count
        // but search_range is the nearest power of two, so...
        if (unicode_codepoint >= Ushort(data + search + range_shift * 2))
            search += range_shift * 2;

        // now decrement to bias correctly to find smallest
        search -= 2;
        while (entry_selector) {
            uint16_t end;
            search_range >>= 1;
            end = Ushort(data + search + search_range * 2);
            if (unicode_codepoint > end)
                search += search_range * 2;
            --entry_selector;
        }
        search += 2;

        {
            uint16_t offset, start, last;
            uint16_t item = static_cast<uint16_t>((search - end_count) >> 1);

            start = Ushort(data + index_map + 14 + seg_count*2 + 2 + 2*item);
            last  = Ushort(data + end_count + 2*item);
            if (unicode_codepoint < start || unicode_codepoint > last)
                return 0;

            offset = Ushort(data + index_map + 14 + seg_count*6 + 2 + 2*item);
            if (offset == 0)
                return static_cast<uint16_t>(
                    unicode_codepoint + Short(data + index_map + 14
                                              + seg_count*4 + 2 + 2*item));
            return Ushort(data + offset + (unicode_codepoint - start)*2
                          + index_map + 14 + seg_count*6 + 2 + 2 *item);
        }
    }
    else if (format==12 || format==13) {
        uint32_t n_groups = Ulong(data + index_map + 12);
        int32_t low, high;
        low = 0; high = static_cast<int32_t>(n_groups);
        // Binary search the right group.
        while (low < high) {
            int32_t mid = low + ((high-low) >> 1); // rounds down, so low <= mid < high
            uint32_t start_char = Ulong(data + index_map + 16 + mid*12);
            uint32_t end_char   = Ulong(data + index_map + 16 + mid*12 + 4);
            if (static_cast<uint32_t>(unicode_codepoint) < start_char)
                high = mid;
            else if (static_cast<uint32_t>(unicode_codepoint) > end_char)
                low = mid + 1;
            else {
                uint32_t start_glyph = Ulong(data + index_map + 16 + mid*12 + 8);
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
    uint16_t num = Ushort(_data + _hhea + 34);
    return glyph_index < num ?
        GlyphHorMetrics{ Short(_data+_hmtx +   4*glyph_index),
                         Short(_data+_hmtx +   4*glyph_index + 2) }
        : GlyphHorMetrics{ Short(_data+_hmtx + 4*(num-1)),
                           Short(_data+_hmtx + 4*num + 2*(glyph_index - num))};
}



// ============================================================================
//                         PRIVATE PARSING METHODS
// ============================================================================

inline uint32_t Font::FindTable(const char* tag) const noexcept {
    int32_t num_tables = Ushort(_data + 4);
    const uint32_t table_dir = 12;

    for (int i=0; i<num_tables; ++i) {
        uint32_t loc = table_dir + 16*i;
        if (Tag(_data + loc+0, tag))
            return Ulong(_data + loc+8);
    }
    return 0;
}

// ============================================================================
//                         STATIC   METHODS
// ============================================================================

inline int Font::GetFontOffsetForIndex(uint8_t* buff, int index) noexcept {
    if (IsFont(buff)) // if it's just a font, there's only one valid index
        return index==0 ? 0 : -1;
    if (Tag(buff, "ttcf")) { // check if it's a TTC
        // version 1?
        if (Ulong(buff+4) == 0x00010000 ||
            Ulong(buff+4) == 0x00020000) {
            int32_t n = Long(buff+8);
            if (index >= n) return -1;
            return Ulong(buff+12 + index*4);
        }
    }
    return -1;
}

inline int Font::GetNumberOfFonts(const uint8_t* buff) noexcept {
    // if it's just a font, there's only one valid font
    if (IsFont(buff)) return 1;
    if (Tag(buff, "ttcf")) { // check if it's a TTC
        // version 1?
        if (Ulong(buff+4) == 0x00010000 || Ulong(buff+4) == 0x00020000) {
            return Long(buff+8);
        }
    }
    return 0;
}



} // namespace stb_stream






