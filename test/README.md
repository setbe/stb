# Tests and examples

This folder contains:

- `catch.hpp` — Catch2 single-header used for unit tests
- `*_catch.cpp` — strict unit tests (including byte-diff tests vs original stb headers)
- `*_bench.cpp` — micro-bench / sanity checks
- `*_example.cpp` — freestanding-style examples, typically using only OS APIs

## Running tests (CMake)

Configure:

```bash
cmake -S .. -B ../build -G "Visual Studio 17 2022" -A Win32
```

Build & run (example):

```bash
cmake --build ../build --config Debug
../build/Debug/iw_catch.exe
```

## Freestanding examples

Examples like `stbiw_example.cpp` are written to be “almost freestanding”:
- they use Win32 APIs directly (`windows.h`)
- they avoid the C++ standard library
- they rely on the module’s `*_FREESTANDING` integration headers
