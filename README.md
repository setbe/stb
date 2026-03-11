# stb (freestanding C++ fork)

This repository is a freestanding-friendly C++ rewrite/fork of selected stb single-header libraries.

Main goals:

- Freestanding-first builds (optional CRT-free integration).
- Cleaner C++ structure instead of macro-heavy C internals.
- Header-first workflow with deterministic, testable behavior.
- Byte-diff tests against original stb where practical.

## Repository status

- `stb_truetype/` - active C++ rewrite based on `stb_truetype.h`.
- `stb_truetype_stream/` - stream-oriented atlas pipeline (SDF, MSDF, MTSDF), no allocations inside the library path.
- `stb_image_write/` - active C++ rewrite with freestanding hooks.
- `stb_image/` - implemented `stb_image.hpp` two-pass API (Plan + Decode) with format-specific entry points and byte-diff tests.
  Current implementation uses an internal embedded stb_image decoder snapshot (`stb_image/detail/stb_image_internal.hpp`) while C++ internal migration continues.
- `stb_image_resize2/` - minimal/WIP integration.
- `3rd_party/stb/` - upstream stb git submodule used for reference/byte-diff tests.
- `test/` - Catch2 tests and small Windows examples.

## stb_image.hpp status (updated)

`stb_image/stb_image.hpp` is usable now:

- Supported through the C++ API: PNG, BMP, GIF, PSD, PIC, JPEG, PNM, HDR, TGA.
- Two-pass usage is available:
  - Pass 1: `Plan*` computes dimensions/channels/output byte size.
  - Pass 2: `Decode*` writes into caller-provided memory.
- Batch planning helpers are available to compute max/sum memory across many images.
- Byte-diff tests are present against original `stb_image.h`.

Important: current decode internals are still stb-derived, so internal transient allocations may still happen. The two-pass API already removes user-side guessing and supports deterministic pre-allocation strategy.

## Build (CMake)

Example configure:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
```

Example build:

```bash
cmake --build build --config ReleaseMiniNoConsole
```

Common MSVC configs:

- `Debug`
- `Release`
- `ReleaseMini`
- `ReleaseNoConsole`
- `ReleaseMiniNoConsole`

## Security note

As with original stb-family libraries, do not treat these parsers as hardened against hostile input without additional sandboxing and validation.

## License

MIT (see `LICENSE`).
Original stb code is by Sean Barrett and contributors.
