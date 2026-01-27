# stb_truetype_stream.hpp

`stb_truetype_stream.hpp` is a **freestanding, allocation-free TrueType glyph streaming library** inspired by `stb_truetype.h`, but designed for **low-level engines**, **custom renderers**, and **no-CRT / no-heap environments**.

The entire implementation is contained in a single header (~32 KB of pure C++), requires **no dynamic memory allocation**, and emits glyph geometry **incrementally** through a user-defined sink interface.

This makes it suitable for:
- game engines
- custom font rasterizers
- SDF / MSDF pipelines
- embedded or freestanding environments
- tooling that needs full control over glyph processing

---

## Key Features

- **Single-header library**
- **No `malloc`, no `new`, no STL**
- **Freestanding-friendly**
- **TrueType (`.ttf`) support WITHOUT OpenType (`.otf`)**
- **NO OpenType (`.otf`) SUPPORT**
- **Streaming glyph outlines** (no intermediate buffers required)
- **Simple and composite glyph support**
- **Direct access to glyph outlines**
- **Designed for SDF generation**
- **Unicode codepoint based glyph lookup**
- **Deterministic behavior**

---

## Design Philosophy

Unlike `stb_truetype.h`, which builds intermediate buffers and performs internal allocations, `stb_truetype_stream`:

- Parses font data **directly from memory**
- Streams glyph outlines **point-by-point**
- Pushes geometry to a user-defined `GlyphSink`
- Avoids hidden state and heap usage
- Leaves rasterization decisions to the caller

This gives you full control over:
- coordinate transforms
- contour handling
- winding rules
- distance field generation
- curve tessellation strategy

---

## Basic Usage

### 1. Load font data

```cpp
stbtt_stream::Font font;
font.ReadBytes(font_data);
