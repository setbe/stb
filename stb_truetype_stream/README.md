# stb_truetype_stream

`stb_truetype_stream/stb_truetype_stream.hpp` is a freestanding-oriented C++ fork for deterministic distance-field generation from TrueType fonts.

Primary goals:

- no internal `malloc/new` or STL dependency in the core header
- user-owned memory only
- deterministic two-pass pipeline
- SDF / MSDF / MTSDF output

## Security Warning

Like upstream stb code, this parser is not hardened for untrusted fonts.
Use only on trusted font files.

## Standalone Core Header

Core API lives in one file:

- `stb_truetype_stream/stb_truetype_stream.hpp`

This header is fully usable on its own.
It does **not** require anything from `codepoints/`.

## Core API (namespace `stbtt_stream`)

Main type:

- `stbtt_stream::Font`

Important methods:

- `bool ReadBytes(uint8_t* font_buffer) noexcept`
- `float ScaleForPixelHeight(float height) const noexcept`
- `int FindGlyphIndex(int unicode_codepoint) const noexcept`
- `GlyphHorMetrics GetGlyphHorMetrics(int glyph_index) const noexcept`
- `size_t PlanBytes(const PlanInput& in) const noexcept`
- `bool Plan(const PlanInput& in, void* plan_mem, size_t plan_bytes, FontPlan& out_plan) noexcept`
- `bool Build(const FontPlan& plan, uint8_t* atlas, uint32_t atlas_stride_bytes) noexcept`
- `bool StreamDF(const GlyphPlan& gp, unsigned char* atlas, uint32_t atlas_stride_bytes, DfMode mode, float scale, float spread, GlyphScratch& scratch, uint16_t max_points, uint32_t max_area) noexcept`

Main data types:

- `DfMode` (`SDF`, `MSDF`, `MTSDF`)
- `PlanInput`
- `FontPlan`
- `GlyphPlan`

## Two-Pass Workflow

### Pass 1: memory planning

1. Fill `PlanInput` with mode, pixel height, spread, and codepoint list.
2. Call `PlanBytes(in)` to get required bytes for plan arena.
3. Allocate `plan_mem` once.
4. Call `Plan(in, plan_mem, plan_bytes, plan)` to compute packed glyph layout and bind internal views.

### Pass 2: build output atlas

1. Allocate atlas buffer based on `plan.atlas_side` and component count:
   - `SDF` -> 1 channel
   - `MSDF` -> 3 channels
   - `MTSDF` -> 4 channels
2. Call `Build(plan, atlas, stride_bytes)`.

No internal allocation happens during `Build`.

## Deterministic Memory Reuse (recommended)

For batching multiple fonts / script sets:

1. Run Pass 1 for each job and track:
   - maximum `PlanBytes`
   - maximum atlas byte size
2. Allocate one reusable plan arena and one reusable atlas buffer with these maxima.
3. Process jobs sequentially (`Plan` -> `Build`) reusing the same buffers.

This minimizes fragmentation and prevents runtime reallocation churn.

## Optional Addon: `stbtt_codepoints_stream.hpp`

Addon header:

- `stb_truetype_stream/codepoints/stbtt_codepoints_stream.hpp`

Namespace:

- `stbtt_codepoints`

Purpose:

- script presets (`Script::Latin`, `Script::Cyrillic`, `Script::Kana`, ...)
- helper pass to count/collect codepoints present in a specific font

Important: this addon is optional convenience only.
`stb_truetype_stream.hpp` remains fully independent and standalone.

## Correct `stbtt_codepoints::` Usage

The addon itself follows two passes:

1. `PlanGlyphs(font, scripts...)` -> get required codepoint capacity
2. `CollectGlyphs(font, sink, script)` -> fill your buffer

Recommended usage pattern:

```cpp
#include "stb_truetype_stream/stb_truetype_stream.hpp"
#include "stb_truetype_stream/codepoints/stbtt_codepoints_stream.hpp"

stbtt_stream::Font font{};
// font.ReadBytes(...)

const uint32_t count = stbtt_codepoints::PlanGlyphs(
    font,
    stbtt_codepoints::Script::Latin,
    stbtt_codepoints::Script::Cyrillic,
    stbtt_codepoints::Script::Greek
);

uint32_t* codepoints = /* allocate count entries */;
uint32_t at = 0;

auto sink = [&](uint32_t cp, int /*glyph_index*/) noexcept {
    if (at < count) codepoints[at++] = cp;
};

stbtt_codepoints::CollectGlyphs(font, sink, stbtt_codepoints::Script::Latin);
stbtt_codepoints::CollectGlyphs(font, sink, stbtt_codepoints::Script::Cyrillic);
stbtt_codepoints::CollectGlyphs(font, sink, stbtt_codepoints::Script::Greek);

stbtt_stream::PlanInput in{};
in.mode = stbtt_stream::DfMode::MSDF;
in.pixel_height = 48;
in.spread_px = 4.0f;
in.codepoints = codepoints;
in.codepoint_count = at;
```

Notes:

- sink signature is `sink(uint32_t codepoint, int glyph_index)`
- addon does not deduplicate between scripts; deduplicate on your side if needed
- collected codepoints are exactly what you pass into `stbtt_stream::PlanInput`

## Minimal Core-Only Example (no addon)

```cpp
stbtt_stream::Font font{};
if (!font.ReadBytes(font_bytes)) return false;

uint32_t cps[] = { 'A', 'B', 'C' };
stbtt_stream::PlanInput in{};
in.mode = stbtt_stream::DfMode::SDF;
in.pixel_height = 48;
in.spread_px = 4.0f;
in.codepoints = cps;
in.codepoint_count = 3;

const size_t plan_bytes = font.PlanBytes(in);
void* plan_mem = user_alloc(plan_bytes);

stbtt_stream::FontPlan plan{};
if (!font.Plan(in, plan_mem, plan_bytes, plan)) return false;

const uint32_t comp = 1; // SDF
const uint32_t stride = (uint32_t)plan.atlas_side * comp;
uint8_t* atlas = user_alloc((size_t)plan.atlas_side * (size_t)plan.atlas_side * comp);

if (!font.Build(plan, atlas, stride)) return false;
```

## Example Programs

- `test/stbtt_stream_example.cpp` (freestanding SDF/MSDF window demo)
- `test/stbtt_stream_bitmap_example.cpp` (atlas generation with optional `stbtt_codepoints`)

## Differences vs Original `stb_truetype`

- object-oriented, namespaced C++ API
- explicit caller-owned memory model
- deterministic Plan/Build pipeline
- streaming-oriented distance-field generation path
