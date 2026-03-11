# stb_image.hpp

`stb_image/stb_image.hpp` is a C++ API layer for deterministic two-pass image loading.
It is designed for memory planning workflows (for example, atlas builders) where you want to know sizes first, then decode into caller-owned memory.

Current implementation uses an embedded internal decoder snapshot at `stb_image/detail/stb_image_internal.hpp`.
This means `stb_image.hpp` no longer includes `stb_image/stb_image.h` directly.

## Supported formats

- PNG
- BMP
- GIF
- PSD
- PIC
- JPEG
- PNM
- HDR
- TGA

## Design goals

- Deterministic pass-based API:
  - Pass 1: plan bytes and metadata.
  - Pass 2: decode into caller-provided output buffer.
- Minimize allocator churn on the user side:
  - Precompute max/sum requirements for many images.
  - Reuse one scratch arena across sequential decodes.
- Keep API small and C++-friendly (`struct`-based options and plans).

## Two-pass model

### Pass 1: planning

Use `Plan` (or a format-specific `PlanX`) to fill `ImagePlan`:

- dimensions (`width`, `height`)
- channels in file and output channels
- source bit depth hint (`source_bits_per_channel`)
- required output bytes (`pixel_bytes`)
- scratch recommendation (`scratch_bytes`)

### Pass 2: decoding

Use `Decode` (or `DecodeX`) with:

- original image bytes
- `ImagePlan` from pass 1
- scratch pointer/size
- output pointer/size

Decode writes pixels to `out_pixels` with no reallocation of your output buffers.

## About fragmentation and arena reuse

1. In pass 1 you compute per-image memory requirements.
2. Across all images you compute the maximum scratch need.
3. In pass 2 you decode images one-by-one, reusing the same arena sized by that maximum.

This significantly reduces user-side memory fragmentation because you avoid repeated allocate/free cycles for temporary buffers in your code.

Current implementation note:

- `scratch_bytes` is currently `0` for this wrapper path.
- "no extra allocations" is true for caller-managed buffers.

## Public API reference

Include:

```cpp
#include "stb_image/stb_image.hpp"
```

### Enums

- `stbi::Format`
  - `Unknown, Png, Bmp, Gif, Psd, Pic, Jpeg, Pnm, Hdr, Tga`
- `stbi::SampleType`
  - `U8, U16, F32`

### `DecodeOptions`

- `uint8_t desired_channels`
  - `0` keeps source channel count.
  - `1..4` forces output channel count.
- `SampleType sample_type`
  - output sample type (`U8`, `U16`, `F32`)
- `bool flip_vertically`
  - if true, output is vertically flipped after decode

### `ImagePlan`

- `Format format`
- `SampleType sample_type`
- `bool flip_vertically`
- `uint32_t width, height`
- `uint8_t channels_in_file`
- `uint8_t output_channels`
- `uint8_t source_bits_per_channel`
- `size_t pixel_bytes`
- `size_t scratch_bytes`

### Batch planning helpers

`stbi::BatchPlanner` accumulates many `ImagePlan` values:

- `Reset()`
- `Add(const ImagePlan&)`
- `Get() -> const BatchPlanSummary&`
- `ReusableScratchBytes()`
- `TotalPixelBytes()`
- `MaxImageBytes()`

`BatchPlanSummary` includes:

- image count
- max width/height/components
- max pixel/scratch/total bytes
- sum of pixel bytes
- sum of total bytes

### Free functions

- `failure_reason()`
- `sample_bytes(SampleType)`
- `total_bytes(const ImagePlan&)`

Planning:

- `Plan(...)`
- `PlanPng(...)`, `PlanBmp(...)`, `PlanGif(...)`, `PlanPsd(...)`, `PlanPic(...)`
- `PlanJpeg(...)`, `PlanPnm(...)`, `PlanHdr(...)`, `PlanTga(...)`

Decoding:

- `Decode(...)`
- `DecodePng(...)`, `DecodeBmp(...)`, `DecodeGif(...)`, `DecodePsd(...)`, `DecodePic(...)`
- `DecodeJpeg(...)`, `DecodePnm(...)`, `DecodeHdr(...)`, `DecodeTga(...)`

### `stbi::Decoder` class

Methods:

- `ReadBytes(const uint8_t* bytes, size_t byte_count)`
- `Clear()`
- `Plan(...)`, `Decode(...)`
- format-specific `PlanX(...)` and `DecodeX(...)`
- `FailureReason()`
- `Bytes()`, `ByteCount()`

## Usage examples

### Single image (generic)

```cpp
stbi::Decoder dec;
if (!dec.ReadBytes(file_bytes, file_size)) {
    // handle error
}

stbi::DecodeOptions opt{};
opt.desired_channels = 4;
opt.sample_type = stbi::SampleType::U8;
opt.flip_vertically = false;

stbi::ImagePlan plan{};
if (!dec.Plan(opt, plan)) {
    // handle error: dec.FailureReason()
}

void* scratch = arena_ptr;      // can be shared across images
size_t scratch_bytes = arena_sz; // >= plan.scratch_bytes
void* pixels = output_ptr;      // caller-owned output
size_t pixels_bytes = output_sz; // >= plan.pixel_bytes

if (!dec.Decode(plan, scratch, scratch_bytes, pixels, pixels_bytes)) {
    // handle error: dec.FailureReason()
}
```

### Batch planning + sequential arena reuse

```cpp
stbi::BatchPlanner batch{};

for (auto& img : all_images) {
    stbi::Decoder d;
    stbi::ImagePlan p{};
    if (!d.ReadBytes(img.bytes, img.size)) continue;
    if (!d.Plan(options, p)) continue;
    batch.Add(p);
    img.plan = p;
}

const size_t arena_bytes = batch.ReusableScratchBytes();
void* shared_arena = allocate(arena_bytes ? arena_bytes : 1);

for (auto& img : all_images) {
    stbi::Decoder d;
    if (!d.ReadBytes(img.bytes, img.size)) continue;
    d.Decode(img.plan, shared_arena, arena_bytes, img.output, img.plan.pixel_bytes);
}
```

## Differences from original `stb_image.h`

- C++ two-pass API (`Plan` + `Decode`) instead of one-shot decode as the primary workflow.
- Explicit memory planning structs (`ImagePlan`, `BatchPlanner`).
- Memory input is the primary path.
- Format-specific Plan/Decode entry points are first-class.
- No direct compile-time dependency on `3rd_party/stb_image.h` in wrapper builds; decoder code is embedded internally.
- Built with these upstream options in this wrapper:
  - `STBI_NO_STDIO`
  - `STBI_NO_SIMD`
  - `STBI_NO_THREAD_LOCALS`
- Not all original APIs are surfaced directly (for example, animated GIF frame APIs and some low-level helpers are not wrapper-first APIs).

## Determinism notes

- Given identical bytes and options, pass-1 planning is deterministic.
- Pass-2 validates decode metadata against the plan (`width`, `height`, `channels_in_file`) before copying output.
- This helps catch mismatches early if bytes/options differ from what was planned.

## Testing

Repository tests include byte-diff comparisons against upstream `stb_image.h` (from `3rd_party/stb` submodule) for the `cat.*` sample set, including GIF.
