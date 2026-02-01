# stb_truetype_stream

`stb_truetype_stream.hpp` is a freestanding-oriented fork of `stb_truetype`,
designed for **SDF / MSDF generation with zero internal dynamic allocations** and
**fully user-controlled memory**.

The library follows a **streaming glyph processing model**:
glyph outlines are parsed and consumed *on the fly*, without ever building or
storing full contour representations in memory.

---

## Design Goals

- ❌ No `malloc`, `new`, STL, or CRT usage inside the library
- ✅ Freestanding-friendly (custom allocators, OS APIs, bare metal)
- ✅ Deterministic memory usage
- ✅ Explicit two-pass pipeline (Plan → Build)
- ✅ SDF and MSDF generation
- ✅ Safe composite glyph handling
- ✅ Suitable for games, GUI libraries, and asset pipelines

---

## What “stream” means here

The word **stream** in `stb_truetype_stream` does **not** mean file I/O
or `std::istream`.

In this project, *stream* means:

> **Glyph geometry is processed incrementally and immediately consumed,
> without being stored in intermediate structures.**

### In practice

- Glyph outlines are read directly from the `glyf` table
- Segments are emitted immediately into a consumer (`GlyphSink`)
- No retained contours, no edge lists, no temporary geometry buffers
- Composite glyphs are resolved recursively with a guarded visit stack
- Distance fields are accumulated during traversal

This enables:

- predictable memory usage
- very small scratch buffers
- safe use in freestanding environments
- processing large fonts without transient allocations

---

## Core Library (`stb_truetype_stream.hpp`)

The core header is **fully standalone**.

It does **not** depend on anything in the `codepoints/` directory.

### Responsibilities

- Parsing TrueType tables (`cmap`, `glyf`, `loca`, etc.)
- Streaming glyph outlines
- Handling composite glyphs safely
- Generating:
  - **SDF** (single-channel signed distance field)
  - **MSDF** (multi-channel signed distance field)
- Exposing a memory-explicit API

---

## Two-Pass API Overview

### Pass 1: Plan

The **Plan** step answers:

> *“How much memory do I need, and how will glyphs be laid out?”*

```cpp
PlanInput in{};
in.mode = DfMode::SDF;          // or MSDF
in.pixel_height = 32;
in.spread_px = 4.0f;
in.codepoints = cps;
in.codepoint_count = count;

size_t bytes = font.PlanBytes(in);
void* plan_mem = user_allocate(bytes);

FontPlan plan{};
font.Plan(in, plan_mem, bytes, plan);
```

What happens here:

- Glyphs are analyzed
- Bounding boxes are expanded by spread
- Maximum point counts and scratch sizes are computed
- A skyline atlas is packed
- All internal pointers are bound into `FontPlan`

No rendering happens here.

---

### Pass 2: Build

The **Build** step performs actual SDF/MSDF generation.

```cpp
uint8_t* atlas = user_allocate(plan.atlas_side * plan.atlas_side * components);
font.Build(plan, atlas, stride_bytes);
```

What happens here:

- Each glyph is streamed from `glyf`
- Distance passes are executed
- Inside/outside sign is resolved
- Results are written directly into the atlas

No allocations occur.

---

## Streaming a Single Glyph (No Atlas)

You can bypass the atlas system and stream **one glyph directly** into a buffer.

Useful for debugging, tools, or dynamic rendering.

```cpp
font.StreamDF(
    glyph_plan,
    pixels,
    stride_bytes,
    DfMode::SDF,
    scale,
    spread_fu,
    scratch,
    max_points,
    max_area
);
```

---

## Composite Glyph Handling

Composite glyphs (accented characters, CJK compositions, etc.) are:

- resolved recursively
- transformed via affine matrices
- protected by a **cycle-safe visit stack**

No stack corruption, no double pops, no infinite recursion.

---

## Optional Module: `codepoints/`

The `codepoints/` directory is a **purely optional helper module**.

The core library:

- does **not** include it
- does **not** depend on it
- does **not** require it

### Purpose

`stbtt_codepoints` exists to:

- define Unicode script ranges
- help users build *conservative* glyph sets
- avoid shipping massive, unnecessary atlases

---

## Supported Scripts (Optional Module)

Currently provided scripts:

- Latin
- Cyrillic
- Greek
- Arabic
- Hebrew
- Devanagari
- Kana (Hiragana + Katakana)
- Jōyō Kanji
- CJK Unified Ideographs

Each script is defined as:

- Unicode ranges
- optional explicit codepoint lists (“singles”)

---

## Using `stbtt_codepoints` (Pseudocode)

The optional module follows a **two-step pattern**.

### Step 1: Count glyphs (planning)

```cpp
count = PlanGlyphs(font,
    Latin,
    Cyrillic,
    Greek
);
```

This step:

- iterates script ranges
- checks `font.FindGlyphIndex(cp)`
- counts only glyphs actually present in the font

No memory allocation happens here.

---

### Step 2: Collect codepoints

```cpp
allocate array codepoints[count]

index = 0
sink(cp, glyph_index):
    codepoints[index++] = cp

BuildGlyphs(font, Latin, sink)
BuildGlyphs(font, Cyrillic, sink)
BuildGlyphs(font, Greek, sink)
```

Now you have a **minimal, conservative codepoint list**.

---

## Full Bitmap Generation Pipeline (SDF / MSDF)

```text
Font bytes
   ↓
Font::ReadBytes
   ↓
(optional) stbtt_codepoints::PlanGlyphs
   ↓
(optional) stbtt_codepoints::BuildGlyphs
   ↓
Font::PlanBytes
   ↓
allocate plan memory
   ↓
Font::Plan
   ↓
allocate atlas bitmap
   ↓
Font::Build
   ↓
SDF / MSDF bitmap ready
```

Key properties:

- all allocations are user-owned
- scratch memory is bounded and known
- pipeline is deterministic
- same flow works for SDF and MSDF

---

## About Codepoint Coverage

The provided script ranges are **intentionally conservative but not minimal**.

Unicode blocks often include:

- historical glyphs
- deprecated characters
- rarely used symbols

These increase:

- atlas size
- generation time
- memory usage

### Philosophy

> Minimalism matters — in the library *and* in generated data.

If you know a script well and can safely reduce its ranges,
**pull requests are very welcome**.

Smaller codepoint sets mean:

- smaller atlases
- faster builds
- less GPU memory usage

---

## What This Library Is *Not*

- ❌ Not a text shaping engine (no HarfBuzz logic)
- ❌ Not a layout engine
- ❌ Not a rasterizer
- ❌ Not tied to OpenGL, Vulkan, or DirectX

It is intentionally low-level.

---

## Typical Use Cases

- Custom GUI libraries
- Game engines (SDF/MSDF text rendering)
- Asset pipelines
- Tools and font preprocessors
- Freestanding or CRT-free environments

---

## MIT License