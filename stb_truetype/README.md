# stb_truetype

`stb_truetype/stb_truetype.hpp` is a C++ rewrite/fork of `stb_truetype.h` (based on v1.26), focused on:

- `stbtt::Font` object API instead of C-style global functions
- freestanding-friendly integration
- easier embedding into custom engines/tools

## Security Warning

Same as the original stb project: this code is **not hardened** for hostile input.
Do not parse untrusted font files with this library.

## Public Header

- `stb_truetype/stb_truetype.hpp`

Everything is inside namespace `stbtt`.

## Main API

Core type:

- `stbtt::Font`

Most-used methods:

- `bool ReadBytes(uint8_t* font_buffer) noexcept`
- `float ScaleForPixelHeight(float height) const noexcept`
- `int FindGlyphIndex(int unicode_codepoint) const noexcept`
- `GlyphHorMetrics GetGlyphHorMetrics(int glyph_index) const noexcept`
- `bool GetGlyphBox(int glyph_index, Box& out) noexcept`
- `Box GetGlyphBitmapBox(int glyph_index, float scale_x, float scale_y, float shift_x = 0, float shift_y = 0) noexcept`
- `void MakeGlyphBitmap(unsigned char* output, int glyph_index, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x = 0, float shift_y = 0) noexcept`
- `static int GetFontOffsetForIndex(uint8_t* font_buffer, int index) noexcept`
- `static int GetNumberOfFonts(const uint8_t* font_buffer) noexcept`

Useful public structs:

- `stbtt::Box`
- `stbtt::Bitmap`
- `stbtt::GlyphHorMetrics`

## Minimal Usage

```cpp
#define STBTT_FREESTANDING
#include "stb_truetype/stb_truetype.hpp"

bool RenderGlyphA(uint8_t* font_bytes, unsigned char* out, int stride) noexcept {
    stbtt::Font font{};
    if (!font.ReadBytes(font_bytes)) return false;

    const int glyph = font.FindGlyphIndex('A');
    if (glyph <= 0) return false;

    const float scale = font.ScaleForPixelHeight(48.0f);
    const stbtt::Box box = font.GetGlyphBitmapBox(glyph, scale, scale);
    const int w = box.x1 - box.x0;
    const int h = box.y1 - box.y0;
    if (w <= 0 || h <= 0) return false;

    const float shift_x = -box.x0 * scale;
    const float shift_y = -box.y0 * scale;
    font.MakeGlyphBitmap(out, glyph, w, h, stride, scale, scale, shift_x, shift_y);
    return true;
}
```

## Freestanding Mode

Enable with:

```cpp
#define STBTT_FREESTANDING
#include "stb_truetype/stb_truetype.hpp"
```

In this mode the header uses:

- `stb_truetype/detail/math_integration.hpp`
- `stb_truetype/detail/libc_integration.hpp`

You can override hooks before include:

- alloc/free: `STBTT_malloc`, `STBTT_free`
- memory/string: `STBTT_memcpy`, `STBTT_memset`, `STBTT_strlen`
- math: `STBTT_ifloor`, `STBTT_iceil`, `STBTT_sqrt`, `STBTT_pow`, `STBTT_fmod`, `STBTT_cos`, `STBTT_acos`, `STBTT_fabs`

## Differences vs Original `stb_truetype.h`

- C API replaced with C++ OOP API (`stbtt::Font`)
- namespaced internals and reorganized code
- freestanding integration is first-class
- API is intentionally not a strict 1:1 symbol match with original C header

Rasterization logic and font table behavior follow the original stb approach, adapted to this C++ structure.

## Examples

- `test/stbtt_example.cpp` (freestanding Win32 glyph render demo)
