# stb_truetype.hpp — C++ freestanding port of stb_truetype.h

### This is a C++ freestanding port of Sean Barrett’s legendary stb_truetype.h

It’s designed for CRT-free, OS-level, or minimal runtime environments — ideal for demoscenes, bootloaders, micro-engines, and embedded systems.

## Overview

`stb_truetype.hpp` keeps full API compatibility with the original `stb_truetype.h`, but rewritten in modern, header-only C++.
When compiled with `#define STBTT_FREESTANDING`, the library automatically provides fallback implementations for:
- math functions (`sqrt`, `pow`, `fabs`, etc.)
- memory operations (`memcpy`, `memset`, `malloc`, `free`)
- basic string utilities (strlen)
- and even platform allocators (`VirtualAlloc` on Windows, `mmap` on POSIX)
  
All fallbacks are **auto-activated** only if **not overridden by user** macros.

## Example

Below: a 24 KB executable rendering a glyph from arialbd.ttf using VirtualAlloc and GDI —
no CRT, no C standard library, pure C++ freestanding code.

![photo_2025-10-21_21-05-46](https://github.com/user-attachments/assets/fb7114de-b8e8-4eef-8c69-d42bb3486a84)
![photo_2025-10-21_21-05-50](https://github.com/user-attachments/assets/8d779807-70ef-45cf-a0a6-5e3cc43d17d1)

## Integration

Just include the header in your project:
```cpp
#define STBTT_FREESTANDING
#include "stb_truetype.hpp"
```
If you don’t define `STBTT_FREESTANDING`, the library will use standard C headers (`math.h`, `stdlib.h`, `string.h`).
When freestanding, you may redefine any of the macros below to plug in your own engine’s runtime.
