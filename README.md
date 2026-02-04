# stb (freestanding C++ fork)

This repository is a **freestanding-friendly C++ rewrite/fork** of a subset of Sean Barrett’s *stb* single-header libraries.

The main goals:

- **Freestanding-first**: build without the C runtime (CRT) and without the C++ standard library, when you choose.
- **Readable C++**: restructure and rename internals into small, typed helpers instead of macro-heavy C.
- **Header-only**: each module remains a single public header (`.hpp`) with OPTIONAL`detail/` integration headers.
- **Strict tests**: Catch2 byte-diff tests compare outputs against the original stb headers where it makes sense.

> **Security note (stb_truetype, stb_truetype_stream)**: like the original, this code is not designed to safely parse untrusted font files. Do not use it on hostile input.

## What’s inside

- `stb_truetype/` — freestanding C++ rewrite based on `stb_truetype.h` (v1.26). Rasterizer as original.
- `stb_truetype_stream/` — freestanding C++ library based on `stb_truetype.h`. Stream in this context means that no memory allocations occur inside the lib. Generates SDF, MSDF, MTSDF atlases with skylines.
  Public headers: `stb_truetype.hpp, `stb_truetype_stream.hpp`.
- `stb_image_write/` — freestanding C++ rewrite based on `stb_image_write.hpp`.  
  Public header: `stb_image_write.hpp`
- `stb_image/` — wrapper/rewrite work-in-progress (currently minimal).
- `stb_image_resize2/` — header-only resize (currently minimal/WIP).
- `test/` — Catch2 unit tests + tiny freestanding examples.

## Philosophy: “integration headers”

In freestanding mode, you usually don’t have `malloc/free/memcpy/memset/strlen` or `math.h`.
Instead of pulling the standard library, each module includes a small `detail/*_integration.hpp`
that defines (or lets you override) the required primitives.

- Define `STBTT_FREESTANDING` to enable freestanding mode in `stb_truetype.hpp` (stream version doesn't depend on any of lib C functions, so no need for this macro if you use stb_truetype_stream.hpp)
- Define `STBIW_FREESTANDING` to enable freestanding mode in `stb_image_write.hpp`

You can override any of the hooks with your own platform/engine functions.

## Build (CMake)

This repo ships a CMake setup that supports normal and freestanding builds on MSVC and GCC/Clang.

### MSVC configs

- `Debug`
- `Release`
- `ReleaseMini`
- `ReleaseNoConsole`
- `ReleaseMiniNoConsole`

The `Release*` “freestanding” configs typically:
- disable RTTI/exceptions (where possible),
- prefer `/MT` (static CRT) or a freestanding-friendly runtime setup,
- build tiny Win32 GUI subsystem examples (`WinMain`) for NoConsole configs.

> If you are going *fully* CRT-free on Windows, you’ll also need a minimal runtime strategy
> (entry point, termination, memset/memcpy, etc.). See each module’s README for integration notes.

### Quick commands

Configure (Visual Studio generator example):

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
```

Build a configuration:

```bash
cmake --build build --config ReleaseMiniNoConsole
```

## Quick usage

### stb_image_write (Writer)

```cpp
#define STBIW_FREESTANDING
#include "stb_image_write/stb_image_write.hpp"

// Provide a callback that writes bytes somewhere (file, socket, buffer, etc.)
static void my_write(void* ctx, const void* data, int size);

stbiw::Writer w;
w.start_callbacks(my_write, ctx);
w.set_flip_vertically(false);

// pixels = RGBA (comp=4)
w.write_bmp_core(width, height, 4, pixels);
w.flush();
```

### stb_truetype (Font)

```cpp
#define STBTT_FREESTANDING
#include "stb_truetype/stb_truetype.hpp"

// font_buffer must contain the entire font file in memory:
stbtt::Font font;
if (!font.ReadBytes(font_buffer)) { /* handle error */ }

float s = font.ScaleForPixelHeight(32.0f);
int glyph = font.FindGlyphIndex('A');
```

## License

MIT (see `LICENSE`). Original stb code: Sean Barrett and contributors.
This fork: additional refactors and freestanding integration.
