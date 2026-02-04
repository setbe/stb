# stb_image_write (freestanding C++ rewrite)

This module is a freestanding-friendly C++ rewrite/fork of `stb_image_write.h`.

Public header:
- `stb_image_write/stb_image_write.hpp`

## Main API

The core type is:

- `stbiw::Writer`

It writes image data through a user-provided callback:

```cpp
using Func = void (*)(void* ctx, const void* data, int size);
```

### Key methods

- `start_callbacks(Func, void* ctx)`
- `flush()`
- `set_flip_vertically(bool)`
- `set_tga_rle(bool)`
- `write_bmp_core(w, h, comp, pixels)`
- `write_tga_core(w, h, comp, pixels)`

`comp` is the number of channels in the input pixel buffer (1..4).

## Output formats

Currently implemented here:

- BMP (24-bit for RGB, 32-bit w/ alpha with V4 header + masks)
- TGA (uncompressed and RLE variants)

## Freestanding mode

Enable with:

```cpp
#define STBIW_FREESTANDING
#include "stb_image_write/stb_image_write.hpp"
```

In freestanding mode, the header includes:

- `stb_image_write/detail/libc_integration.hpp`

### Required libc hooks

- `STBIW_malloc(size_t bytes, void* user)`
- `STBIW_free(void* ptr, void* user)`
- `STBIW_memcpy`, `STBIW_memset`
- `STBIW_strlen`

Optional (recommended if you want `realloc` semantics):
- `STBIW_realloc(ptr, new_size, user)` **or**
- `STBIW_realloc_sized(ptr, old_size, new_size, user)`

## Example: write BMP/TGA to a Win32 file

See `test/stbiw_example.cpp` for a minimal Windows-only freestanding example that:
- allocates a RGBA image,
- draws a vertical alpha gradient + 3 colored horizontal lines,
- writes both `.bmp` and `.tga` using Win32 `CreateFileW/WriteFile`.

## Tests

See `test/stbiw_catch.cpp` for strict byte-diff tests (against the original stb header).
