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
// 
//    ABOUT FORK
// 
// This fork based on stb_truetype.h - v1.26
// Goal: rewrite the lib in neat freestanding C++.

#ifdef _MSC_VER
#   define STBTT__NOTUSED(v)  (void)(v)
#else
#   define STBTT__NOTUSED(v)  (void)sizeof(v)
#endif

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

#if defined(_DEBUG) || !defined(NDEBUG)
#   include <assert.h>
#   define STBTT_assert(x) assert(x)
#else
#   define STBTT_assert
#endif

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
////
////   INTEGRATION WITH YOUR CODEBASE
////
////   The following sections allow you to supply alternate definitions
////   of C library functions used by stb_truetype, e.g. if you don't
////   link with the C runtime library.

// If freestanding: you must provide own math macros.
//      STBTT_ifloor(x)
//      STBTT_iceil(x)
//      STBTT_sqrt(x)
//      STBTT_pow(x, y)
//      STBTT_fmod(x, y)
//      STBTT_cos(x)
//      STBTT_acos(x)
//      STBTT_fabs(x)
// 
// and from lib C:
//      STBTT_malloc(x,u)
//      STBTT_free(x,u)
//      STBTT_strlen(x)
//      STBTT_memcpy
//      STBTT_memset

#ifdef STBTT_FREESTANDING
#   include "stbtt/detail/math_integration.hpp"
#else // !STBTT_FREESTANDING
    // ---------- math functions -----------
#   ifndef STBTT_ifloor             
#       include <math.h>
#       define STBTT_ifloor(x)   ((int)floor(x))
#       define STBTT_iceil(x)    ((int)ceil(x))
#   endif
#   ifndef STBTT_sqrt
#       include <math.h>
#       define STBTT_sqrt(x)     sqrt(x)
#       define STBTT_pow(x,y)    pow(x,y)
#   endif
#   ifndef STBTT_fmod
#       include <math.h>
#       define STBTT_fmod(x,y)   fmod(x,y)
#   endif
#   ifndef STBTT_cos
#       include <math.h>
#       define STBTT_cos(x)      cos(x)
#       define STBTT_acos(x)     acos(x)
#   endif
#   ifndef STBTT_fabs
#       include <math.h>
#       define STBTT_fabs(x)     fabs(x)
#   endif
#endif // !STBTT_FREESTANDING

#ifdef STBTT_FREESTANDING
#   include "stbtt/detail/libc_integration.hpp"
#else
    // ---------- libC functions -----------
#   include <stdlib.h>
#   include <string.h>

    // Default to malloc/free
#   ifndef STBTT_malloc
#      define STBTT_malloc(x,u)  ((void)(u), malloc(x))
#      define STBTT_free(x,u)    ((void)(u), free(x))
#   endif

#   ifndef STBTT_strlen
#      define STBTT_strlen(x)    strlen(x)
#   endif

#   ifndef STBTT_memcpy
#      define STBTT_memcpy       memcpy
#      define STBTT_memset       memset
#   endif
#endif // ifndef STBTT_FREESTANDING


#include "stbtt/detail/enums.hpp"
#include "stbtt/detail/buf.hpp"
#include "stbtt/detail/edges.hpp"
#include "stbtt/detail/raster_scratch.hpp"


namespace stbtt {
// The following structure is defined publicly so you can declare one on
// the stack or as a global or etc, but you should treat it as opaque.
struct FontInfo {
    void    * userdata;
    uint8_t * data;              // pointer to .ttf file
    int       fontstart;         // offset of start of font

    int num_glyphs;                    // number of glyphs, needed for range checking

    int loca, head, glyf, hhea, hmtx, kern, gpos, svg; // table locations as offset from start of .ttf
    int index_map;              // a cmap mapping for our chosen character encoding
    int index_to_loc_format;    // format needed to map from glyph index to glyph

    detail::Buf cff;                    // cff font data
    detail::Buf charstrings;            // the charstring index
    detail::Buf g_subrs;                // global charstring subroutines index
    detail::Buf subrs;                  // private charstring subroutines index
    detail::Buf fontdicts;              // array of font dicts
    detail::Buf fdselect;               // map from glyph to fontdict
};

struct Vertex {
    enum class Kind {
        Move = 1, // move-to
        Line = 2, // line-to
        Curve = 3, // quadratic Bezier curve-to
        Cubic = 4  // cubic Bezier curve-to
    };
    Kind kind{};

    // CFF/Type2 theoretically could contain wider numbers than 16-bit.
    using value_t = int16_t;
    value_t x{}, y{};
    value_t cx{}, cy{};
    value_t cx1{}, cy1{};

    void Update(Kind kind_, int32_t x_,  int32_t y_,
                            int32_t cx_, int32_t cy_) noexcept {
        kind = kind_;

        // clamp from i32 to i16
        x = static_cast<value_t>(x_);
        y = static_cast<value_t>(y_);
        cx = static_cast<value_t>(cx_);
        cy = static_cast<value_t>(cy_);
    }
};

struct CurveShape {
    int bounds;
    int started;
    float first_x, first_y;
    float x, y;
    int32_t min_x, max_x, min_y, max_y;

    Vertex* p_vertices;
    int num_vertices;

    inline CurveShape(int bounds_) noexcept : bounds{ bounds_ } {
        started = 0;
        first_x = first_y = x = y = 0.f;
        min_x = max_x = min_y = max_y = 0;
        p_vertices = nullptr;
        num_vertices = 0;
    }

    inline void TrackVertex(int32_t x_, int32_t y_) noexcept {
        if (x_ > max_x || !started) max_x = x_;
        if (y_ > max_y || !started) max_y = y_;
        if (x_ < min_x || !started) min_x = x_;
        if (y_ < min_y || !started) min_y = y_;
        started = 1;
    }

    inline void V(Vertex::Kind type_,
           int32_t x_,   int32_t y_,
           int32_t cx_,  int32_t cy_,
           int32_t cx1_, int32_t cy1_) noexcept {
        if (bounds) {
            TrackVertex(x_, y_);
            if (type_ == Vertex::Kind::Cubic) {
                TrackVertex(cx_, cy_);
                TrackVertex(cx1_, cy1_);
            }
        } else {
            p_vertices[num_vertices].Update(type_, x_, y_, cx_, cy_);
            p_vertices[num_vertices].cx1 = static_cast<Vertex::value_t>(cx1_);
            p_vertices[num_vertices].cy1 = static_cast<Vertex::value_t>(cy1_);
        }
        ++num_vertices;
    }

    inline void CloseShape() noexcept {
        if (first_x != x || first_y != y)
            V(Vertex::Kind::Line, static_cast<int32_t>(first_x),
                                static_cast<int32_t>(first_y), 0, 0, 0, 0);
    }

    inline void RMoveTo(float dx, float dy) noexcept {
        CloseShape();
        first_x = x = x+dx;
        first_y = y = y+dy;
        V(Vertex::Kind::Move, static_cast<int32_t>(x),
                            static_cast<int32_t>(y), 0, 0, 0, 0);
    }

    inline void RLineTo(float dx, float dy) noexcept {
        x += dx;
        y += dy;
        V(Vertex::Kind::Line, static_cast<int32_t>(x),
                            static_cast<int32_t>(y), 0, 0, 0, 0);
    }

    inline void RcCurveTo(float dx1, float dy1,
                          float dx2, float dy2,
                          float dx3, float dy3) noexcept {
        float cx1 = x + dx1;
        float cy1 = y + dy1;
        float cx2 = cx1 + dx2;
        float cy2 = cy1 + dx2;
        x = cx2 + dx3;
        y = cy2 + dy3;
        V(Vertex::Kind::Cubic,
            static_cast<int32_t>(x),   static_cast<int32_t>(y),
            static_cast<int32_t>(cx1), static_cast<int32_t>(cy1),
            static_cast<int32_t>(cx2), static_cast<int32_t>(cy2));
    }
};

struct GlyphHorMetrics {
    int advance;
    int lsb; // left side bearing
};

struct Box {
    int x0, y0, x1, y1;
    inline Box() noexcept { x0 = y0 = x1 = y1 = 0; }
};

struct Bitmap {
    int w, h, stride;
    uint8_t* pixels;
};

struct Point { float x, y; };

struct TrueType {
    FontInfo fi{};

    TrueType() = default;
    inline bool ReadBytes(uint8_t* font_buffer) noexcept;
    inline float ScaleForPixelHeight(float height) const noexcept;
    inline int FindGlyphIndex(int unicode_codepoint) const noexcept;
    inline GlyphHorMetrics GetGlyphHorMetrics(int glyph_index) const noexcept;

    inline int GetGlyphInfoT2(int glyph_index, Box& out) noexcept;
    inline bool GetGlyphBox(int glyph_index, Box& out) noexcept;
    inline Box GetGlyphBitmapBox(int glyph_index,
                          float scale_x,     float scale_y,
                          float shift_x = 0, float shift_y = 0) noexcept;

    inline void MakeGlyphBitmap(unsigned char* output, int glyph_index,
                            int out_w, int out_h, int out_stride,
                            float scale_x,       float scale_y,
                            float shift_x = 0.f, float shift_y = 0.f) noexcept;

    Point* FlattenCurves(Vertex* vertices, int num_verts,
            float objspace_flatness, int** contour_lengths,
             int* num_contours,      void* userdata) noexcept;

    inline void Rasterize(Bitmap& out,    float flatness_in_pixels,
                   Vertex* vertices, int num_verts,
                   float scale_x,  float scale_y,
                   float shift_x,  float shift_y,
                     int x_off,      int y_off,
                 uint8_t invert,   void* userdata) noexcept;
    
    // since most people won't use this, find this table the first time it's needed
    inline int GetSvg() noexcept;

    static inline int GetFontOffsetForIndex(uint8_t* font_buffer, int index) noexcept;
    static inline int GetNumberOfFonts(const uint8_t* font_buffer) noexcept;

private:
    inline int GetGlyfOffset(int glyph_index) const noexcept;
    inline uint32_t FindTable(const char* tag) const noexcept;
    inline detail::Buf GetCidGlyphSubrs(int glyph_index) noexcept;
    inline int RunCharString(int glyph_index, CurveShape&) noexcept;

    inline int CloseShape(Vertex* vertices, int num_vertices, bool was_off, bool start_off,
        int32_t sx, int32_t sy, int32_t scx, int32_t scy, int32_t cx, int32_t cy) noexcept;

    inline int GetGlyphShape(int glyph_index, Vertex** pvertices) noexcept;
    inline int GetGlyphShapeTT(int glyph_index, Vertex** pvertices) noexcept;
    inline int GetGlyphShapeT2(int glyph_index, Vertex** pvertices) noexcept;

    inline void AddPoint(Point* points, int n, float x, float y) noexcept;
    inline int TesselateCurve(Point* points, int* num_points,
            float x0, float y0, float x1, float y1,
            float x2, float y2, float objspace_flatness_squared, int n) noexcept;
    inline void TesselateCubic(Point* points, int* num_points,
            float x0, float y0, float x1, float y1, float x2, float y2,
            float x3, float y3, float objspace_flatness_squared, int n) noexcept;

    void RasterizeProcess(Bitmap& out, Point* points, int* wcount, int windings,
            float scale_x, float scale_y, float shift_x, float shift_y,
            int off_x, int off_y, uint8_t invert, void* userdata) noexcept;

    // --- Edging ---
    void RasterizeSortedEdges(Bitmap& out, detail::Edge* e, int n_edges,
            int off_x, int off_y, void* userdata) noexcept;
    inline void SortEdges(detail::Edge* p, int n_edges) noexcept { _SortEdgesQuicksort(p, n_edges); _SortEdgesInsSort(p, n_edges); }
    inline void _SortEdgesQuicksort(detail::Edge* p, int n_edges) noexcept;
    inline void _SortEdgesInsSort(detail::Edge* p, int n_edges) noexcept;


    // --- Parsing helpers ---
    static uint8_t  Byte(const uint8_t* p) noexcept   { return *p; }
    static int8_t   Char(const uint8_t* p) noexcept   { return *(const int8_t*)p; }
    static uint16_t Ushort(const uint8_t* p) noexcept { return p[0] * 256 + p[1]; }
    static int16_t  Short(const uint8_t* p) noexcept  { return p[0] * 256 + p[1]; }
    static uint32_t Ulong(const uint8_t* p) noexcept  { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
    static int32_t  Long(const uint8_t* p) noexcept   { return (p[0]<<24) + (p[1]<<16) + (p[2]<<8) + p[3]; }
    static bool Tag4(const uint8_t* p, char c0, char c1, char c2, char c3) noexcept {
        return ((p)[0] == (c0) && (p)[1] == (c1) && (p)[2] == (c2) && (p)[3] == (c3));
    }
    static bool Tag(const uint8_t* p, const char* str) noexcept { return Tag4(p, str[0], str[1], str[2], str[3]); }
    static int IsFont(const uint8_t* font) noexcept {
        // check the version number
        if (Tag4(font, '1', 0, 0, 0))  return 1; // TrueType 1
        if (Tag(font, "typ1"))   return 1; // TrueType with type 1 font -- we don't support this!
        if (Tag(font, "OTTO"))   return 1; // OpenType with CFF
        if (Tag4(font, 0, 1, 0, 0)) return 1; // OpenType 1.0
        if (Tag(font, "true"))   return 1; // Apple specification for TrueType fonts
        return 0;
    }
}; // struct TrueType

// ============================================================================
//                         PUBLIC   METHODS
// ============================================================================

inline bool TrueType::ReadBytes(uint8_t* font_buffer) noexcept {
    uint32_t cmap, t;
    int32_t i, num_tables;

    fi.data = font_buffer;
    fi.fontstart = 0;

    cmap = FindTable("cmap");    // required
    fi.loca = FindTable("loca"); // required
    fi.head = FindTable("head"); // required
    fi.glyf = FindTable("glyf"); // required
    fi.hhea = FindTable("hhea"); // required
    fi.hmtx = FindTable("hmtx"); // required
    fi.kern = FindTable("kern"); // not required
    fi.gpos = FindTable("GPOS"); // not required

    if (!cmap || !fi.head || !fi.hhea || !fi.hmtx)
        return 0;
    if (fi.glyf) {
        if (!fi.loca) return false; // required for truetype
    } else {
        detail::Buf b, topdict, topdictidx;
        uint32_t cstype = 2, charstrings = 0, fdarrayoff = 0, fdselectoff = 0;
        uint32_t cff;

        cff = FindTable("CFF ");
        if (!cff) return false;

        // @TODO this should use size from table (not 512MB)
        fi.cff.data = font_buffer + cff;
        fi.cff.size = 512 * 1024 * 1024;
        b = fi.cff;

        // read the header
        b.Skip(2);
        b.Seek(b.Get8()); // hdrsize

        // @TODO the name INDEX could list multiple fonts,
        // but we just use the first one.
        b.CffGetIndex(); // name INDEX
        topdictidx = b.CffGetIndex();
        topdict = topdictidx.CffGetIndex(0);
        b.CffGetIndex();  // string INDEX
        fi.g_subrs = b.CffGetIndex();

        topdict.DictGetInts(17, 1, &charstrings);
        topdict.DictGetInts(0x100 | 6, 1, &cstype);
        topdict.DictGetInts(0x100 | 36, 1, &fdarrayoff);
        topdict.DictGetInts(0x100 | 37, 1, &fdselectoff);
        fi.subrs = detail::Buf::GetSubrs(b, topdict);

        // we only support Type 2 charstrings
        if (cstype != 2) return false;
        if (charstrings == 0) return false;

        if (fdarrayoff) {
            // looks like a CID font
            if (!fdselectoff) return false;
            b.Seek(fdarrayoff);
            fi.fontdicts = b.CffGetIndex();
            fi.fdselect = b.Range(fdselectoff, b.size - fdselectoff);
        }
        b.Seek(charstrings);
        fi.charstrings = b.CffGetIndex();
    }

    t = FindTable("maxp");
    if (t) fi.num_glyphs = Ushort(fi.data + t + 4);
    else   fi.num_glyphs = 0xffff;

    fi.svg = -1;

    // find a cmap encoding table we understand *now* to avoid searching
    // later. (todo: could make this installable)
    // the same regardless of glyph.
    using detail::PlatformId;
    using detail::EncodingIdMicrosoft;
    num_tables = Ushort(fi.data + cmap + 2);
    fi.index_map = 0;
    for (i = 0; i < num_tables; ++i) {
        uint32_t encoding_record = cmap + 4 + 8 * i;
        PlatformId platform =
            static_cast<PlatformId>(Ushort(fi.data + encoding_record));

        // find an encoding we understand:
        switch (platform) {
        case PlatformId::Microsoft:
            switch (static_cast<EncodingIdMicrosoft>(
                Ushort(fi.data + encoding_record + 2))) {
            case EncodingIdMicrosoft::Unicode_Bmp:
            case EncodingIdMicrosoft::Unicode_Full: // MS/Unicode
                fi.index_map = cmap + Ulong(fi.data + encoding_record + 4);
                break;
            }
            break;
        case PlatformId::Unicode: // Mac/iOS has these
            // all the encodingIDs are unicode, so we don't bother to check it
            fi.index_map = cmap + Ulong(fi.data + encoding_record + 4);
            break;
        }
    }
    if (fi.index_map == 0)
        return false;

    fi.index_to_loc_format = Ushort(fi.data + fi.head + 50);
    return true;
}

inline float TrueType::ScaleForPixelHeight(float height) const noexcept {
    int h = Short(fi.data + fi.hhea + 4) - Short(fi.data + fi.hhea + 6);
    return height / static_cast<float>(h);
}

inline int TrueType::FindGlyphIndex(int unicode_codepoint) const noexcept {
    uint8_t* data = fi.data;
    uint32_t index_map = fi.index_map;

    uint16_t format = Ushort(data + index_map+0);
    if (format == 0) { // Apple byte encoding
        int32_t bytes = Ushort(data + index_map+2);
        if (unicode_codepoint < bytes-6)
            return Byte(data + index_map+6 + unicode_codepoint);
        return 0;
    } else if (format == 6) {
        uint32_t first = Ushort(data + index_map+6);
        uint32_t count = Ushort(data + index_map+8);
        if (static_cast<uint32_t>(unicode_codepoint) >= first &&
            static_cast<uint32_t>(unicode_codepoint) < first+count)
            return Ushort(data + index_map+10 + (unicode_codepoint - first)*2);
        return 0;
    } else if ( format == 2) {
        STBTT_assert(0); // @TODO: high-byte mapping for japanese/chinese/korean
        return 0;
    }
    // standard mapping for windows fonts: binary search collection of ranges
    else if (format == 4) {
        uint16_t seg_count      = Ushort(data + index_map+6) >> 1;
        uint16_t search_range   = Ushort(data + index_map+8) >> 1;
        uint16_t entry_selector = Ushort(data + index_map+10);
        uint16_t range_shift    = Ushort(data + index_map+12) >> 1;

        // do a binary searc of the segments
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
            last = Ushort(data + end_count + 2*item);
            if (unicode_codepoint < start || unicode_codepoint > last)
                return 0;

            offset = Ushort(data + index_map + 14 + seg_count*6 + 2 + 2*item);
            if (offset == 0)
                return static_cast<uint16_t>(
                    unicode_codepoint + Short(data + index_map+14
                                                  + seg_count*4 + 2 + 2*item));
            return Ushort(data + offset + (unicode_codepoint - start)*2
                                    + index_map+14 + seg_count*6 + 2 + 2*item);
        }
    }
    else if (format == 12 || format == 13) {
        uint32_t n_groups = Ulong(data + index_map+12);
        int32_t low, high;
        low = 0; high = static_cast<int32_t>(n_groups);
        // Binary search the right group.
        while (low < high) {
            int32_t mid = low + ((high-low)>>1); // rounds down, so low <= mid < high
            uint32_t start_char = Ulong(data + index_map+16+mid*12);
            uint32_t end_char   = Ulong(data + index_map+16+mid*12 + 4);
            if (static_cast<uint32_t>(unicode_codepoint) < start_char)
                high = mid;
            else if (static_cast<uint32_t>(unicode_codepoint) > end_char)
                low = mid+1;
            else {
                uint32_t start_glyph = Ulong(data + index_map+16+mid*12 + 8);
                if (format == 12)
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

inline GlyphHorMetrics TrueType::GetGlyphHorMetrics(int glyph_index) const noexcept {
    // num of long hor metrics
    uint16_t num = Ushort(fi.data + fi.hhea + 34);

    return glyph_index < num ?
        GlyphHorMetrics{
        Short(fi.data + fi.hmtx + 4 * glyph_index),
        Short(fi.data + fi.hmtx + 4 * glyph_index + 2)}
        : GlyphHorMetrics{
        Short(fi.data + fi.hmtx + 4 * (num - 1)),
        Short(fi.data + fi.hmtx + 4 * (num - 1))};
}


inline int TrueType::GetGlyfOffset(int glyph_index) const noexcept {
    int g1, g2;
    STBTT_assert(!fi.cff.size);

    if (glyph_index >= fi.num_glyphs) return -1; // glyph index out of range
    if (fi.index_to_loc_format >= 2)  return -1; // unknown index->glyph map format

    if (fi.index_to_loc_format == 0) {
        g1 = fi.glyf + Ushort(fi.data + fi.loca + glyph_index*2) * 2;
        g2 = fi.glyf + Ushort(fi.data + fi.loca + glyph_index*2 + 2) * 2;
    } else {
        g1 = fi.glyf + Ulong(fi.data + fi.loca + glyph_index*4);
        g2 = fi.glyf + Ulong(fi.data + fi.loca + glyph_index*4 + 4);
    }
    return g1==g2 ? -1 : g1; // if length is 0, return -1
}

inline int TrueType::GetGlyphInfoT2(int glyph_index, Box& out) noexcept {
    CurveShape c(1);
    int r = RunCharString(glyph_index, c);
    out.x0 = r ? c.min_x : 0;
    out.y0 = r ? c.min_y : 0;
    out.x1 = r ? c.max_x : 0;
    out.y1 = r ? c.max_y : 0;
    return r ? c.num_vertices : 0;
}

inline bool TrueType::GetGlyphBox(int glyph_index, Box& box) noexcept {
    if (fi.cff.size) {
        GetGlyphInfoT2(glyph_index, box);
    } else {
        int g = GetGlyfOffset(glyph_index);
        if (g < 0) return false;

        box.x0 = Short(fi.data + g + 2);
        box.y0 = Short(fi.data + g + 4);
        box.x1 = Short(fi.data + g + 6);
        box.y1 = Short(fi.data + g + 8);
    }
    return true;
}

inline Box TrueType::GetGlyphBitmapBox(int glyph_index,
                                 float scale_x, float scale_y,
                                 float shift_x, float shift_y) noexcept {
    Box b{};
    if (GetGlyphBox(glyph_index, b)) {
        // move to integral bboxes (treating pixels as little squares, what pixels get touched)?
        const int x0 = b.x0;
        const int y0 = b.y0;
        const int x1 = b.x1;
        const int y1 = b.y1;

        b.x0 = STBTT_ifloor(x0 * scale_x + shift_x);
        b.y0 = STBTT_ifloor(-y1 * scale_y + shift_y);
        b.x1 = STBTT_iceil(x1 * scale_x + shift_x);
        b.y1 = STBTT_iceil(-y0 * scale_y + shift_y);
    }
    else {
        // e.g. space character
    }

    return b;
    
}

inline int TrueType::GetSvg() noexcept {
    if (fi.svg >= 0) return fi.svg;
    uint32_t t;
    t = FindTable("SVG ");
    if (!t) {
        fi.svg = 0;
    }
    else {
        uint32_t offset = Ulong(fi.data + t + 2);
        fi.svg = t + offset;
    }
    return fi.svg;
}


inline uint32_t TrueType::FindTable(const char* tag) const noexcept {
    int32_t num_tables = Ushort(fi.data + fi.fontstart+4);
    uint32_t table_dir = fi.fontstart + 12;
    for (int32_t i = 0; i < num_tables; ++i) {
        uint32_t loc = table_dir + 16 * i;
        if (Tag(fi.data+loc+0, tag))
            return Ulong(fi.data+loc+8);
    }
    return 0;
}

inline detail::Buf TrueType::GetCidGlyphSubrs(int glyph_index) noexcept {
    detail::Buf fd_select = fi.fdselect; // copy
    int nranges, start, end, v, fmt;
    int fdselector = -1;

    fd_select.Seek(0);
    fmt = fd_select.Get8();
    if (fmt == 0) {
        // untested
        fd_select.Skip(glyph_index);
        fdselector = fd_select.Get8();
    }
    else if (fmt == 3) {
        nranges = fd_select.Get16();
        start   = fd_select.Get16();
        for (int i = 0; i < nranges; ++i) {
            v   = fd_select.Get8();
            end = fd_select.Get16();
            if (glyph_index >= start && glyph_index < end) {
                fdselector = v;
                break;
            }
            start = end;
        }
    }
    if (fdselector == -1) return detail::Buf{};

    detail::Buf fontdict = fi.fontdicts.CffGetIndex(fdselector);
    return detail::Buf::GetSubrs(fi.cff, fontdict);
}

inline int TrueType::RunCharString(int glyph_index, CurveShape& c) noexcept {
    int in_header = 1;
    int sp, maskbits, subr_stack_height, has_subrs;
    has_subrs = subr_stack_height = maskbits = sp = 0;
    
    int v, i, b0, clear_stack;
    float s[48]{};
    detail::Buf subr_stack[10];
    detail::Buf subrs = fi.subrs;
    
    detail::Buf b;
    float f;

#define STBTT__CSERR(s) (0)
    // this currently ignores the initial width value, which isn't needed if we have hmtx
    b = fi.charstrings.CffGetIndex(glyph_index);
    while (b.cursor < b.size) {
        i = 0;
        clear_stack = 1;
        b0 = b.Get8();
        switch (b0) {
            // @TODO implement hinting
        case 0x13: // hintmask
        case 0x14: // cntrmask
            if (in_header)
                maskbits += (sp / 2); // implicit "vstem"
            in_header = 0;
            b.Skip((maskbits + 7) / 8);
            break;

        case 0x01: // hstem
        case 0x03: // vstem
        case 0x12: // hstemhm
        case 0x17: // vstemhm
            maskbits += (sp / 2);
            break;

        case 0x15: // rmoveto
            in_header = 0;
            if (sp < 2) return STBTT__CSERR("rmoveto stack");
            c.RMoveTo(s[sp - 2], s[sp - 1]);
            break;
        case 0x04: // vmoveto
            in_header = 0;
            if (sp < 1) return STBTT__CSERR("vmoveto stack");
            c.RMoveTo(0, s[sp - 1]);
            break;
        case 0x16: // hmoveto
            in_header = 0;
            if (sp < 1) return STBTT__CSERR("hmoveto stack");
            c.RMoveTo(s[sp - 1], 0);
            break;

        case 0x05: // rlineto
            if (sp < 2) return STBTT__CSERR("rlineto stack");
            for (; i + 1 < sp; i += 2)
                c.RLineTo(s[i], s[i + 1]);
            break;

        // hlineto/vlineto and vhcurveto/hvcurveto alternate horizontal and
        // vertical starting from a different place.

        case 0x07: // vlineto
            if (sp < 1) return STBTT__CSERR("vlineto stack");
            goto vlineto;
        case 0x06: // hlineto
            if (sp < 1) return STBTT__CSERR("hlineto stack");
            for (;;) {
                if (i >= sp) break;
                c.RLineTo(s[i], 0);
                i++;
            vlineto:
                if (i >= sp) break;
                c.RLineTo(0, s[i]);
                i++;
            }
            break;

        case 0x1F: // hvcurveto
            if (sp < 4) return STBTT__CSERR("hvcurveto stack");
            goto hvcurveto;
        case 0x1E: // vhcurveto
            if (sp < 4) return STBTT__CSERR("vhcurveto stack");
            for (;;) {
                if (i + 3 >= sp) break;
                c.RcCurveTo(0, s[i], s[i + 1], s[i + 2], s[i + 3], (sp - i == 5) ? s[i + 4] : 0.0f);
                i += 4;
            hvcurveto:
                if (i + 3 >= sp) break;
                c.RcCurveTo(s[i], 0, s[i + 1], s[i + 2], (sp - i == 5) ? s[i + 4] : 0.0f, s[i + 3]);
                i += 4;
            }
            break;

        case 0x08: // rrcurveto
            if (sp < 6) return STBTT__CSERR("rcurveline stack");
            for (; i + 5 < sp; i += 6)
                c.RcCurveTo(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
            break;

        case 0x18: // rcurveline
            if (sp < 8) return STBTT__CSERR("rcurveline stack");
            for (; i + 5 < sp - 2; i += 6)
                c.RcCurveTo(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
            if (i + 1 >= sp) return STBTT__CSERR("rcurveline stack");
            c.RLineTo(s[i], s[i + 1]);
            break;

        case 0x19: // rlinecurve
            if (sp < 8) return STBTT__CSERR("rlinecurve stack");
            for (; i + 1 < sp - 6; i += 2)
                c.RLineTo(s[i], s[i + 1]);
            if (i + 5 >= sp) return STBTT__CSERR("rlinecurve stack");
            c.RcCurveTo(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
            break;

        case 0x1A: // vvcurveto
        case 0x1B: // hhcurveto
            if (sp < 4) return STBTT__CSERR("(vv|hh)curveto stack");
            f = 0.0;
            if (sp & 1) { f = s[i]; i++; }
            for (; i + 3 < sp; i += 4) {
                if (b0 == 0x1B)
                    c.RcCurveTo(s[i], f, s[i + 1], s[i + 2], s[i + 3], 0.0);
                else
                    c.RcCurveTo(f, s[i], s[i + 1], s[i + 2], 0.0, s[i + 3]);
                f = 0.0;
            }
            break;

        case 0x0A: // callsubr
            if (!has_subrs) {
                if (fi.fdselect.size)
                    subrs = GetCidGlyphSubrs(glyph_index);
                has_subrs = 1;
            }
            // FALLTHROUGH
        case 0x1D: // callgsubr
            if (sp < 1) return STBTT__CSERR("call(g|)subr stack");
            v = (int)s[--sp];
            if (subr_stack_height >= 10) return STBTT__CSERR("recursion limit");
            subr_stack[subr_stack_height++] = b;
            b = detail::Buf::GetSubr(b0 == 0x0A ? subrs : fi.g_subrs, v);
            if (b.size == 0) return STBTT__CSERR("subr not found");
            b.cursor = 0;
            clear_stack = 0;
            break;

        case 0x0B: // return
            if (subr_stack_height <= 0) return STBTT__CSERR("return outside subr");
            b = subr_stack[--subr_stack_height];
            clear_stack = 0;
            break;

        case 0x0E: // endchar
            c.CloseShape();
            return 1;

        case 0x0C: { // two-byte escape
            float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
            float dx, dy;
            int b1 = b.Get8();
            switch (b1) {
                // @TODO These "flex" implementations ignore the flex-depth and resolution,
                // and always draw beziers.
            case 0x22: // hflex
                if (sp < 7) return STBTT__CSERR("hflex stack");
                dx1 = s[0];
                dx2 = s[1];
                dy2 = s[2];
                dx3 = s[3];
                dx4 = s[4];
                dx5 = s[5];
                dx6 = s[6];
                c.RcCurveTo(dx1, 0, dx2, dy2, dx3, 0);
                c.RcCurveTo(dx4, 0, dx5, -dy2, dx6, 0);
                break;

            case 0x23: // flex
                if (sp < 13) return STBTT__CSERR("flex stack");
                dx1 = s[0];
                dy1 = s[1];
                dx2 = s[2];
                dy2 = s[3];
                dx3 = s[4];
                dy3 = s[5];
                dx4 = s[6];
                dy4 = s[7];
                dx5 = s[8];
                dy5 = s[9];
                dx6 = s[10];
                dy6 = s[11];
                //fd is s[12]
                c.RcCurveTo(dx1, dy1, dx2, dy2, dx3, dy3);
                c.RcCurveTo(dx4, dy4, dx5, dy5, dx6, dy6);
                break;

            case 0x24: // hflex1
                if (sp < 9) return STBTT__CSERR("hflex1 stack");
                dx1 = s[0];
                dy1 = s[1];
                dx2 = s[2];
                dy2 = s[3];
                dx3 = s[4];
                dx4 = s[5];
                dx5 = s[6];
                dy5 = s[7];
                dx6 = s[8];
                c.RcCurveTo(dx1, dy1, dx2, dy2, dx3, 0);
                c.RcCurveTo(dx4, 0, dx5, dy5, dx6, -(dy1 + dy2 + dy5));
                break;

            case 0x25: // flex1
                if (sp < 11) return STBTT__CSERR("flex1 stack");
                dx1 = s[0];
                dy1 = s[1];
                dx2 = s[2];
                dy2 = s[3];
                dx3 = s[4];
                dy3 = s[5];
                dx4 = s[6];
                dy4 = s[7];
                dx5 = s[8];
                dy5 = s[9];
                dx6 = dy6 = s[10];
                dx = dx1 + dx2 + dx3 + dx4 + dx5;
                dy = dy1 + dy2 + dy3 + dy4 + dy5;
                if (STBTT_fabs(dx) > STBTT_fabs(dy))
                    dy6 = -dy;
                else
                    dx6 = -dx;
                c.RcCurveTo(dx1, dy1, dx2, dy2, dx3, dy3);
                c.RcCurveTo(dx4, dy4, dx5, dy5, dx6, dy6);
                break;

            default:
                return STBTT__CSERR("unimplemented");
            }
        } break;

        default:
            if (b0 != 255 && b0 != 28 && b0 < 32)
                return STBTT__CSERR("reserved operator");

            // push immediate
            if (b0 == 255) {
                f = static_cast<float>(
                    static_cast<int32_t>(b.Get32())) / 0x10000f;
            }
            else {
                b.Skip(-1);
                f = static_cast<float>(static_cast<int16_t>(b.CffInt()));
            }
            if (sp >= 48) return STBTT__CSERR("push stack overflow");
            s[sp++] = f;
            clear_stack = 0;
            break;
        }
        if (clear_stack) sp = 0;
    }
    return STBTT__CSERR("no endchar");

#undef STBTT__CSERR
}


inline int TrueType::CloseShape(Vertex* vertices, int num_vertices, bool was_off, bool start_off,
    int32_t sx, int32_t sy, int32_t scx, int32_t scy, int32_t cx, int32_t cy) noexcept {
    STBTT_assert(vertices);

    if (start_off) {
        if (was_off)
            vertices[num_vertices++].Update(Vertex::Kind::Curve, (cx + scx) >> 1, (cy + scy) >> 1, cx, cy);
        vertices[num_vertices++].Update(Vertex::Kind::Curve, sx, sy, scx, scy);
    } else {
        if (was_off)
            vertices[num_vertices++].Update(Vertex::Kind::Curve, sx, sy, cx, cy);
        vertices[num_vertices++].Update(Vertex::Kind::Line, sx, sy, 0, 0);
    }
    return num_vertices;
}

inline int TrueType::GetGlyphShape(int glyph_index, Vertex** pvertices) noexcept {
    return fi.cff.size ?
        GetGlyphShapeT2(glyph_index, pvertices)
        : GetGlyphShapeTT(glyph_index, pvertices);
}

inline int TrueType::GetGlyphShapeTT(int glyph_index, Vertex** pvertices) noexcept {
    int16_t num_contours;
    uint8_t* end_pts_contours;
    uint8_t* data = fi.data;
    Vertex* vertices = nullptr;
    int num_vertices = 0;
    int g = GetGlyfOffset(glyph_index);

    *pvertices = nullptr;

    if (g < 0) return 0;

    num_contours = Short(data + g);

    if (num_contours > 0) {
        uint8_t flags = 0, flagcount;
        bool was_off = false, start_off = false;

        int32_t ins, i,j=0, m,n, next_move, off;
        int32_t x,y, cx,cy,sx,sy, scx, scy;
        uint8_t* points;
        end_pts_contours = data + g+10;
        ins = Ushort(data + g+10 + num_contours*2);
        points = data + g+10 + num_contours*2 + 2 + ins;

        n = 1 + Ushort(end_pts_contours + num_contours*2 - 2);

        m = n + 2*num_contours; // a loose bound on how many vertices we might need
        vertices = reinterpret_cast<Vertex*>(
            STBTT_malloc(m * sizeof(vertices[0]), fi.userdata));
        if (vertices == 0)
            return 0;

        next_move = 0;
        flagcount = 0;

        // in first pass, we load uninterpreted data into the allocated array
        // above, shifted to the end of the array so we won't overwrite it when
        // we create our final data starting from the front

        off = m - n; // starting offset for uninterpreted data, regardless of how m ends up being calculated

        // first load flags
        for (i = 0; i < n; ++i) {
            if (flagcount == 0) {
                flags = *points++;
                if (flags & 8) flagcount = *points++;
            } else {
                --flagcount;
            }
            vertices[off+i].kind = static_cast<Vertex::Kind>(flags);
        }

        // now load x coordinates
        x=0;
        for (i=0; i < n; ++i) {
            flags = static_cast<uint8_t>(vertices[off+i].kind);
            if (flags & 2) {
                int16_t dx = *points++;
                x += (flags & 16) ? dx : -dx; // ???
            }
            else {
                if (!(flags & 16)) {
                    x = x + static_cast<int16_t>(points[0]*256 + points[1]);
                    points += 2;
                }
            }
            vertices[off+i].x = static_cast<int16_t>(x);
        }

        // now load y coordinates
        y = 0;
        for (i = 0; i < n; ++i) {
            flags = static_cast<uint8_t>(vertices[off+i].kind);
            if (flags & 4) {
                int16_t dy = *points++;
                y += (flags & 32) ? dy : -dy; // ???
            } else {
                if (!(flags & 32)) {
                    y = y + static_cast<int16_t>(points[0]*256 + points[1]);
                    points += 2;
                }
            }
            vertices[off+i].y = static_cast<int16_t>(y);
        }

        // now convert them to our format
        num_vertices = 0;
        sx = sy = cx = cy = scx = scy = 0;

        for (i = 0; i < n; ++i) {
            flags = static_cast<uint8_t>(vertices[off+i].kind);
            x = static_cast<int16_t>(vertices[off+i].x);
            y = static_cast<int16_t>(vertices[off+i].y);

            if (next_move == i) {
                if (i != 0)
                    num_vertices = CloseShape(vertices, num_vertices, was_off, start_off, sx,sy, scx,scy, cx,cy);
                // now start the new one
                start_off = !static_cast<bool>(flags & 1);
                if (start_off) {
                    // if we start off with an off-curve point, then when we need to find a point on the curve
                    // where we can start, and we need to save some state for when we wraparound.
                    scx = x;
                    scy = y;
                    if (!( static_cast<uint8_t>(vertices[off+i+1].kind) & 1)) {
                        // next point is also a curve point, so interpolate an on-point curve
                        sx = (x + static_cast<int32_t>(vertices[off+i+1].x)) >> 1;
                        sy = (y + static_cast<int32_t>(vertices[off+i+1].y)) >> 1;
                    }
                    else {
                        // otherwise just use the next point as our start point
                        sx = static_cast<int32_t>(vertices[off+i+1].x);
                        sy = static_cast<int32_t>(vertices[off+i+1].y);
                        ++i; // we're using point i+1 as the starting point, so skip it
                    }
                }
                else {
                    sx = x;
                    sy = y;
                }
                vertices[num_vertices++].Update(Vertex::Kind::Move, sx, sy, 0, 0);
                was_off = false;
                next_move = 1 + Ushort(end_pts_contours + j * 2);
                ++j;
            }
            else {
                if (!(flags & 1)) { // if it's a curve
                    if (was_off) {
                        // two off-curve control points in a row means interpolate an on-curve midpoint
                        vertices[num_vertices++].Update(Vertex::Kind::Curve, (cx+x)>>1, (cy+y)>>1, cx, cy);
                    }
                    cx = x;
                    cy = y;
                    was_off = true;
                }
                else {
                    if (was_off) vertices[num_vertices++].Update(Vertex::Kind::Curve, x, y, cx, cy);
                    else         vertices[num_vertices++].Update(Vertex::Kind::Line,  x, y, 0, 0);
                    was_off = false;
                }
            }
        }
        num_vertices = CloseShape(vertices, num_vertices, was_off, start_off,
                                  sx, sy, scx, scy, cx, cy);
    }
    else if (num_contours < 0) {
        // Compound shapes.
        int more = 1;
        uint8_t* comp = data + g + 10;
        num_vertices = 0;
        vertices = 0;
        while (more) {
            uint16_t flags, gidx;
            int comp_num_verts = 0;
            Vertex* comp_verts = 0;
            Vertex* tmp = 0;
            float mtx[6] = { 1,0,0,1,0,0 }, m, n;

            flags = Short(comp); comp += 2;
            gidx = Short(comp); comp += 2;

            if (flags & 2) { // XY values
                if (flags & 1) { // shorts
                    mtx[4] = Short(comp); comp += 2;
                    mtx[5] = Short(comp); comp += 2;
                }
                else {
                    mtx[4] = Char(comp); comp += 1;
                    mtx[5] = Char(comp); comp += 1;
                }
            }
            else {
                // @TODO handle matching point
                STBTT_assert(0);
            }
            if (flags & (1 << 3)) { // WE_HAVE_A_SCALE
                mtx[0] = mtx[3] = Short(comp) / 16384.0f; comp += 2;
                mtx[1] = mtx[2] = 0;
            }
            else if (flags & (1 << 6)) { // WE_HAVE_AN_X_AND_YSCALE
                mtx[0] = Short(comp) / 16384.0f; comp += 2;
                mtx[1] = mtx[2] = 0;
                mtx[3] = Short(comp) / 16384.0f; comp += 2;
            }
            else if (flags & (1 << 7)) { // WE_HAVE_A_TWO_BY_TWO
                mtx[0] = Short(comp) / 16384.0f; comp += 2;
                mtx[1] = Short(comp) / 16384.0f; comp += 2;
                mtx[2] = Short(comp) / 16384.0f; comp += 2;
                mtx[3] = Short(comp) / 16384.0f; comp += 2;
            }

            // Find transformation scales.
            m = (float)STBTT_sqrt(mtx[0] * mtx[0] + mtx[1] * mtx[1]);
            n = (float)STBTT_sqrt(mtx[2] * mtx[2] + mtx[3] * mtx[3]);

            // Get indexed glyph.
            comp_num_verts = GetGlyphShape(gidx, &comp_verts);
            if (comp_num_verts > 0) {
                // Transform vertices.
                for (int i = 0; i < comp_num_verts; ++i) {
                    Vertex* v = &comp_verts[i];
                    Vertex::value_t x, y;
                    x = v->x; y = v->y;
                    v->x = static_cast<Vertex::value_t>(m * (mtx[0] * x + mtx[2] * y + mtx[4]));
                    v->y = static_cast<Vertex::value_t>(n * (mtx[1] * x + mtx[3] * y + mtx[5]));
                    x = v->cx; y = v->cy;
                    v->cx = static_cast<Vertex::value_t>(m * (mtx[0] * x + mtx[2] * y + mtx[4]));
                    v->cy = static_cast<Vertex::value_t>(n * (mtx[1] * x + mtx[3] * y + mtx[5]));
                }
                // Append vertices.
                tmp = reinterpret_cast<Vertex*>(STBTT_malloc((num_vertices + comp_num_verts) * sizeof(Vertex), fi.userdata));
                if (!tmp) {
                    if (vertices) STBTT_free(vertices, fi.userdata);
                    if (comp_verts) STBTT_free(comp_verts, fi.userdata);
                    return 0;
                }
                if (num_vertices > 0 && vertices) STBTT_memcpy(tmp, vertices, num_vertices * sizeof(Vertex));
                STBTT_memcpy(tmp + num_vertices, comp_verts, comp_num_verts * sizeof(Vertex));
                if (vertices) STBTT_free(vertices, fi.userdata);
                vertices = tmp;
                STBTT_free(comp_verts, fi.userdata);
                num_vertices += comp_num_verts;
            }
            // More components ?
            more = flags & (1 << 5);
        }
    }
    else {
        // num_contours == 0, do nothing
    }

    *pvertices = vertices;
    return num_vertices;
}

inline int TrueType::GetGlyphShapeT2(int glyph_index, Vertex** pvertices) noexcept {
    // runs the charstring twice, once to count and once to output (to avoid realloc)
    CurveShape cs(1);
    CurveShape out(0);
    if (RunCharString(glyph_index, cs)) {
        *pvertices = reinterpret_cast<Vertex*>(STBTT_malloc(cs.num_vertices * sizeof(Vertex), fi.userdata));
        out.p_vertices = *pvertices;
        if (RunCharString(glyph_index, out)) {
            STBTT_assert(out.num_vertices == cs.num_vertices);
            return out.num_vertices;
        }
    }
    *pvertices = NULL;
    return 0;
}


inline void TrueType::MakeGlyphBitmap(
    unsigned char* output, int glyph_index,
    int out_w, int out_h,
    int out_stride,
    float scale_x, float scale_y,
    float shift_x, float shift_y) noexcept {
    Vertex* vertices;
    int num_verts = GetGlyphShape(glyph_index, &vertices);
    Box box = GetGlyphBitmapBox(glyph_index, scale_x, scale_y, shift_x, shift_y);

    Bitmap bm;
    bm.pixels = output;
    bm.w = out_w;
    bm.h = out_h;
    bm.stride = out_stride;

    if (bm.w && bm.h)
        Rasterize(bm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, box.x0, box.y0, 1, fi.userdata);
    STBTT_free(vertices, fi.userdata);
}

inline void TrueType::AddPoint(Point* points, int n, float x, float y) noexcept {
    if (!points) return; // during first pass, it's unallocated
    points[n].x = x;
    points[n].y = y;
}

// @TODO Why this method always returns "1"? Should we return void instead?
inline int TrueType::TesselateCurve(Point* points, int* num_points,
                   float x0, float y0, float x1, float y1, float x2, float y2,
                   float objspace_flatness_squared, int n) noexcept {
    // midpoint
    float mx = (x0 + 2*x1 + x2)/4;
    float my = (y0 + 2*y1 + y2)/4;
    // versus directly drawn line
    float dx = (x0+x2)/2 - mx;
    float dy = (y0+y2)/2 - my;
    if (n > 16) // 65536 segments on one curve better be enough!
        return 1;

    // half-pixel error allowed... need to be smaller if AA
    if (dx*dx + dy*dy > objspace_flatness_squared) {
        TesselateCurve(points, num_points, x0, y0, (x0+x1)/2.f, (y0+y1)/2.0f, mx, my, objspace_flatness_squared, n+1);
        TesselateCurve(points, num_points, mx, my, (x1+x2)/2.f, (y1+y2)/2.0f, x2, y2, objspace_flatness_squared, n+1);
    }
    else {
        AddPoint(points, *num_points, x2, y2);
        *num_points = *num_points + 1;
    }
    return 1;
}

inline void TrueType::TesselateCubic(Point* points, int* num_points,
                        float x0, float y0, float x1, float y1,
                        float x2, float y2, float x3, float y3,
                        float objspace_flatness_squared, int n) noexcept {
    // @TODO this "flatness" calculation is just
    //       made-up nonsense that seems to work well enough
    float i0 = x1-x0;
    float o0 = y1-y0;
    float i1 = x2-x1;
    float o1 = y2-y1;
    float i2 = x3-x2;
    float o2 = y3-y2;
    float i = x3-x0;
    float o = y3-y0;
    float longlen = static_cast<float>(STBTT_sqrt(i0*i0 + o0*o0) +
                STBTT_sqrt(i1*i1 + o1*o1) + STBTT_sqrt(i2*i2 + o2*o2));

    float shortlen = static_cast<float>(STBTT_sqrt(i*i + o*o));
    float flatness_squared = longlen*longlen - shortlen*shortlen;

    if (n > 16) // 65536 segments on one curve better be enough!
        return;

    if (flatness_squared > objspace_flatness_squared) {
        float x01 = (x0 + x1) / 2;
        float y01 = (y0 + y1) / 2;
        float x12 = (x1 + x2) / 2;
        float y12 = (y1 + y2) / 2;
        float x23 = (x2 + x3) / 2;
        float y23 = (y2 + y3) / 2;

        float xa = (x01 + x12) / 2;
        float ya = (y01 + y12) / 2;
        float xb = (x12 + x23) / 2;
        float yb = (y12 + y23) / 2;

        float mx = (xa + xb) / 2;
        float my = (ya + yb) / 2;
        TesselateCubic(points, num_points, x0, y0, x01, y01, xa, ya, mx, my, objspace_flatness_squared, n + 1);
        TesselateCubic(points, num_points, mx, my, xb, yb, x23, y23, x3, y3, objspace_flatness_squared, n + 1);
    }
    else {
        AddPoint(points, *num_points, x3, y3);
        *num_points = *num_points + 1;
    }
}

Point* TrueType::FlattenCurves(Vertex* vertices, int num_verts,
    float objspace_flatness, int** contour_lengths,
    int* num_contours, void* userdata) noexcept {
    Point* points = nullptr;
    int num_points = 0;

    float objspace_flatness_squared = objspace_flatness * objspace_flatness;
    int i, n=0, start=0;

    // count how many "moves" there are to get the contour count
    for (i = 0; i < num_verts; ++i)
        if (vertices[i].kind == Vertex::Kind::Move)
            ++n;

    *num_contours = n;
    if (n == 0) return 0;

    *contour_lengths = reinterpret_cast<int*>(
        STBTT_malloc(sizeof(**contour_lengths) * n, userdata));
    if (*contour_lengths == 0) {
        *num_contours = 0;
        return 0;
    }

    // make two passes through the points so we don't need to realloc
    for (int pass = 0; pass < 2; ++pass) {
        float x=0, y=0;
        if (pass == 1) {
            points = reinterpret_cast<Point*>(
                STBTT_malloc(num_points * sizeof(points[0]), userdata));
            if (points == nullptr) goto error;
        }
        num_points = 0;
        n = -1;
        for (i=0; i < num_verts; ++i) {
            switch (vertices[i].kind) {
            case Vertex::Kind::Move:
                // start the next contour
                if (n >= 0)
                    (*contour_lengths)[n] = num_points - start;
                ++n;
                start = num_points;

                x = vertices[i].x, y = vertices[i].y;
                AddPoint(points, num_points++, x, y);
                break;
            case Vertex::Kind::Line:
                x = vertices[i].x, y = vertices[i].y;
                AddPoint(points, num_points++, x, y);
                break;
            case Vertex::Kind::Curve:
                TesselateCurve(points, &num_points, x, y,
                               vertices[i].cx, vertices[i].cy,
                               vertices[i].x, vertices[i].y,
                               objspace_flatness_squared, 0);
                x = vertices[i].x, y = vertices[i].y;
                break;
            case Vertex::Kind::Cubic:
                TesselateCubic(points, &num_points, x, y,
                               vertices[i].cx, vertices[i].cy,
                               vertices[i].cx1, vertices[i].cy1,
                               vertices[i].x, vertices[i].y,
                               objspace_flatness_squared, 0);
                x = vertices[i].x, y = vertices[i].y;
                break;
            }
        }
        (*contour_lengths)[n] = num_points - start;
    }

    return points;
error:
    STBTT_free(points, userdata);
    STBTT_free(*contour_lengths, userdata);
    *contour_lengths = 0;
    *num_contours = 0;
    return nullptr;
}

inline void TrueType::Rasterize(Bitmap& out, float flatness_in_pixels,
            Vertex* vertices, int num_verts,
            float scale_x, float scale_y,
            float shift_x, float shift_y,
              int x_off,   int y_off,
          uint8_t invert,  void* userdata) noexcept {
    float scale          = scale_x > scale_y ? scale_y : scale_x;
    int winding_count    = 0;
    int* winding_lengths = nullptr;
    Point* windings      = FlattenCurves(vertices, num_verts, flatness_in_pixels / scale, &winding_lengths, &winding_count, userdata);
    if (windings) {
        RasterizeProcess(out, windings, winding_lengths, winding_count, scale_x, scale_y, shift_x, shift_y, x_off, y_off, invert, userdata);
        STBTT_free(winding_lengths, userdata);
        STBTT_free(windings, userdata);
    }
}

void TrueType::RasterizeProcess(Bitmap& out,
        Point* points,
        int* wcount, int windings,
        float scale_x, float scale_y,
        float shift_x, float shift_y,
        int off_x, int off_y,
        uint8_t invert,
        void* userdata) noexcept {
    float y_scale_inv = invert ? -scale_y : scale_y;
    detail::Edge* e;
    int n, i, j, k, m;
    // now we have to blow out the windings into explicit edge lists
    n = 0;
    for (i = 0; i < windings; ++i)
        n += wcount[i];

    // add an extra one as a sentinel
    e = reinterpret_cast<detail::Edge*>(STBTT_malloc(sizeof(*e) * (n + 1), userdata));
    if (e == 0) return;
    n = 0;

    m = 0;
    for (i = 0; i < windings; ++i) {
        Point* p = points + m;
        m += wcount[i];
        j = wcount[i] - 1;
        for (k = 0; k < wcount[i]; j = k++) {
            int a = k, b = j;
            // skip the edge if horizontal
            if (p[j].y == p[k].y)
                continue;
            // add edge from j to k to the list
            e[n].invert = 0;
            if (invert ? p[j].y > p[k].y : p[j].y < p[k].y) {
                e[n].invert = 1;
                a = j, b = k;
            }
            e[n].x0 = p[a].x * scale_x + shift_x;
            e[n].y0 = (p[a].y * y_scale_inv + shift_y);
            e[n].x1 = p[b].x * scale_x + shift_x;
            e[n].y1 = (p[b].y * y_scale_inv + shift_y);
            ++n;
        }
    }

    // now sort the edges by their highest point (should snap to integer, and then by x)
    //STBTT_sort(e, n, sizeof(e[0]), stbtt__edge_compare);
    SortEdges(e, n);

    // now, traverse the scanlines and find the intersections on each scanline, use xor winding rule
    RasterizeSortedEdges(out, e, n, off_x, off_y, userdata);

    STBTT_free(e, userdata);
}

void TrueType::RasterizeSortedEdges(Bitmap& out,
        detail::Edge* e,   int n_edges,
        int   off_x,       int off_y,
        void* userdata) noexcept {
    using detail::ActiveEdge;

    const size_t bytes = detail::RasterScratchBytes(out.w, n_edges);
    void* mem = STBTT_malloc(bytes, userdata);

    if (!mem) {
        // fail-safe
        for (int row = 0; row < out.h; ++row)
            STBTT_memset(out.pixels + row * out.stride, 0, out.w);
        return;
    }

    detail::RasterScratch scratch =
        detail::RasterScratchBind(mem, bytes, out.w, n_edges);
    STBTT_assert(scratch.pool && scratch.scan);
    
    float* scanline = scratch.scan;          // len out.w
    float* scanline2 = scratch.scan + out.w; // len out.w + 1 (because (2*w+1))

    ActiveEdge* active = nullptr;

    int y = off_y;
    int j = 0;
    int ei = 0; // index into edges

    while (j < out.h) {
        const float scan_y_top    = static_cast<float>(y);
        const float scan_y_bottom = static_cast<float>(y) + 1.0f;

        STBTT_memset(scanline, 0, (2*out.w + 1) * sizeof(float));

        // remove finished edges (single pass pointer-to-pointer)
        ActiveEdge** step = &active;
        while (*step) {
            ActiveEdge* z = *step;
            if (z->ey <= scan_y_top) {
                *step = z->next;
                STBTT_assert(z->direction);
                z->direction = 0.f;
                scratch.free(z);
            }
            else {
                step = &z->next;
            }
        }

        // insert edges starting before bottom of scanline
        // (avoid sentinel by checking ei < n_edges and y0 <= scan_y_bottom)
        while (ei < n_edges && e[ei].y0 <= scan_y_bottom) {
            const detail::Edge* ed = &e[ei];
            ActiveEdge* z = scratch.alloc();

            if (ed->y0 == ed->y1 || !z) {
                ++ei;
                continue;
            }

            ActiveEdge::InitFromEdge(*z, *ed, off_x, scan_y_top);

            if (j==0 && off_y!=0) {
                if (z->ey < scan_y_top)
                    // this can happen due to subpixel positioning and some kind of fp rounding error i think
                    z->ey = scan_y_top;
            }
            STBTT_assert(z->ey >= scan_y_top); // if we get really unlucky a tiny bit of an edge can be out of bounds
            
            // insert at front
            z->next = active;
            active = z;
            ++ei;
        } // while

        // now process all active edges
        if (active)
            active->FillActiveEdges(scanline, scanline2 + 1, out.w, scan_y_top);

        
        float sum = 0.0f;
        unsigned char* dst = out.pixels + j * out.stride;

        // write pixels
        for (int i = 0; i < out.w; ++i) {
            sum += scanline2[i];
            float k = scanline[i] + sum;

            // clamp to [0..255]
            int m = int(STBTT_fabs(k) * 255.0f + 0.5f);
            if (m > 255) m = 255;
            dst[i] = static_cast<unsigned char>(m);
        }
        // advance all the edges
        for (ActiveEdge* z=active; z; z=z->next)
            z->fx += z->fdx;

        ++y;
        ++j;
    } // while

    STBTT_free(mem, userdata);
} // RasterizeSortedEdges





inline void TrueType::_SortEdgesQuicksort(detail::Edge* p, int n) noexcept {
    using detail::Edge;

    /* threshold for transitioning to insertion sort */
    while (n > 12) {
        Edge t;
        bool c01, c12, c;
        int m, i, j;

        /* compute median of three */
        m = n >> 1;
        c01 = Edge::CompareY0(p, 0, m);
        c12 = Edge::CompareY0(p, m, n-1);
        /* if 0 >= mid >= end, or 0 < mid < end, then use mid */
        if (c01 != c12) {
            /* otherwise, we'll need to swap something else to middle */
            int z;
            c = Edge::CompareY0(p, 0, n-1);
            /* 0>mid && mid<n:  0>n => n; 0<n => 0 */
            /* 0<mid && mid>n:  0>n => 0; 0<n => n */
            z = (c == c12) ? 0 : n-1;
            t = p[z];
            p[z] = p[m];
            p[m] = t;
        }
        /* now p[m] is the median-of-three */
        /* swap it to the beginning so it won't move around */
        t = p[0];
        p[0] = p[m];
        p[m] = t;

        /* partition loop */
        i = 1;
        j = n - 1;
        for (;;) {
            /* handling of equality is crucial here */
            /* for sentinels & efficiency with duplicates */
            for (;; ++i) {
                if ( ! Edge::CompareY0(p, i, 0)) break;
            }
            for (;; --j) {
                if ( ! Edge::CompareY0(p, 0, j)) break;
            }
            /* make sure we haven't crossed */
            if (i >= j) break;
            t = p[i];
            p[i] = p[j];
            p[j] = t;

            ++i;
            --j;
        }
        /* recurse on smaller side, iterate on larger */
        if (j < (n - i)) {
            _SortEdgesQuicksort(p, j);
            p = p + i;
            n = n - i;
        }
        else {
            _SortEdgesQuicksort(p + i, n - i);
            n = j;
        }
    }
}

inline void TrueType::_SortEdgesInsSort(detail::Edge* p, int n) noexcept {
    for (int i = 1; i < n; ++i) {
        detail::Edge t = p[i];
        int j = i;
        while (j > 0) {
            if (!(t.y0 < p[j - 1].y0))
                break;
            p[j] = p[j - 1];
            --j;
        }
        if (j != i)
            p[j] = t;
    }
}

// ============================================================================
//                         STATIC   METHODS
// ============================================================================

inline int TrueType::GetFontOffsetForIndex(uint8_t* buff, int index) noexcept {
    if (IsFont(buff)) // if it's just a font, there's only one valid index
        return index == 0 ? 0 : -1;
    if (Tag(buff, "ttcf")) { // check if it's a TTC
        // version 1?
        if (Ulong(buff + 4) == 0x00010000 ||
            Ulong(buff + 4) == 0x00020000) {
            int32_t n = Long(buff + 8);
            if (index >= n) return -1;
            return Ulong(buff + 12 + index*4);
        }
    }
    return -1;
}

inline int TrueType::GetNumberOfFonts(const uint8_t* buff) noexcept {
    // if it's just a font, there's only one valid font
    if (IsFont(buff)) return 1;
    if (Tag(buff, "ttcf")) { // check if it's a TTC
        // version 1?
        if (Ulong(buff + 4) == 0x00010000 || Ulong(buff + 4) == 0x00020000) {
            return Long(buff + 8);
        }
    }
    return 0;
}



} // namespace stb