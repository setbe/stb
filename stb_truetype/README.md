# stb_truetype (freestanding C++ rewrite)

This module is a freestanding-friendly C++ rewrite/fork of `stb_truetype.h` (based on v1.26).

Public header:
- `stb_truetype/stb_truetype.hpp`

## Big warning (same as original)

This library is **not hardened** against malicious fonts. It does not fully validate offsets and tables.
Do **not** run it on untrusted font files.

## What changed vs original stb_truetype.h

- C++ namespace + types (`namespace stbtt`)
- Code is reorganized into smaller helpers and `detail/` headers
- Freestanding integration through `detail/libc_integration.hpp` and `detail/math_integration.hpp`
- API is not required to match the original exactly (this repo prioritizes clean integration)

Key public types you’ll see:
- `stbtt::Font` — main font handle (replaces the “info+functions” style)
- `stbtt::Bitmap`, `stbtt::Box`, `stbtt::GlyphHorMetrics`

## Freestanding mode

Enable with:

```cpp
#define STBTT_FREESTANDING
#include "stb_truetype/stb_truetype.hpp"
```

When `STBTT_FREESTANDING` is defined, `stb_truetype.hpp` includes:

- `detail/math_integration.hpp` — provides `STBTT_ifloor`, `STBTT_sqrt`, etc.
- `detail/libc_integration.hpp` — provides allocation and byte utilities

You can override these hooks before including the header.

### Required libc hooks

- `STBTT_malloc(size_t bytes, void* user)`
- `STBTT_free(void* ptr, void* user)`
- `STBTT_memcpy`, `STBTT_memset`
- `STBTT_strlen`

### Required math hooks (freestanding only)

- `STBTT_ifloor`, `STBTT_iceil`
- `STBTT_sqrt`, `STBTT_pow`
- `STBTT_fmod`
- `STBTT_cos`, `STBTT_acos`
- `STBTT_fabs`

## Minimal example

```cpp
#define STBTT_FREESTANDING
#include "stb_truetype/stb_truetype.hpp"

bool render_glyph_A(const uint8_t* font_bytes) {
    stbtt::Font font;
    if (!font.ReadBytes(const_cast<uint8_t*>(font_bytes)))
        return false;

    const float s = font.ScaleForPixelHeight(48.0f);
    const int glyph = font.FindGlyphIndex('A');

    stbtt::Box box = font.GetGlyphBitmapBox(glyph, s, s);
    const int w = box.x1 - box.x0;
    const int h = box.y1 - box.y0;

    // allocate output bitmap using your allocator...
    // font.MakeGlyphBitmap(out, glyph, w, h, stride, s, s);
    return true;
}
```

## Tests

See `test/stbtt_catch.cpp` for unit tests and byte-diff comparisons.
