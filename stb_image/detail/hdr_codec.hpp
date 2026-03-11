#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace stbi { namespace detail {

struct HdrCodec {
    static inline const char*& LastError() noexcept {
        static const char* e = nullptr;
        return e;
    }

    static inline void SetError(const char* s) noexcept {
        LastError() = s;
    }

    static inline const char* FailureReason() noexcept {
        return LastError();
    }

    static inline bool StrEq(const char* a, const char* b) noexcept {
        if (!a || !b) return false;
        while (*a && *b) {
            if (*a != *b) return false;
            ++a;
            ++b;
        }
        return *a == '\0' && *b == '\0';
    }

    static inline bool StartsWith(const char* a, const char* p) noexcept {
        if (!a || !p) return false;
        while (*p) {
            if (*a != *p) return false;
            ++a;
            ++p;
        }
        return true;
    }

    static inline bool ParsePositiveInt(const char*& p, int& out) noexcept {
        if (!p || *p < '0' || *p > '9') return false;
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            const int d = int(*p - '0');
            if (v > 214748364 || (v == 214748364 && d > 7)) return false;
            v = v * 10 + d;
            ++p;
        }
        out = v;
        return v > 0;
    }

    static inline float& H2LGammaInv() noexcept {
        static float g = 1.0f / 2.2f;
        return g;
    }

    static inline float& H2LScaleInv() noexcept {
        static float s = 1.0f;
        return s;
    }

    static inline bool IsHdr(const uint8_t* b, int n) noexcept {
        if (!b || n < 10) return false;
        const char s0[] = "#?RADIANCE\n";
        const char s1[] = "#?RGBE\n";
        bool ok0 = (size_t)n >= (sizeof(s0) - 1u);
        for (size_t i = 0; ok0 && i < sizeof(s0) - 1u; ++i) ok0 = b[i] == (uint8_t)s0[i];
        bool ok1 = (size_t)n >= (sizeof(s1) - 1u);
        for (size_t i = 0; ok1 && i < sizeof(s1) - 1u; ++i) ok1 = b[i] == (uint8_t)s1[i];
        if (ok0 || ok1) return true;
        return false;
    }

    static inline bool ReadLine(const uint8_t* bytes, size_t len, size_t& at,
                                char* out, size_t out_cap) noexcept {
        if (!out || out_cap == 0) return false;
        size_t n = 0;
        while (at < len) {
            const uint8_t c = bytes[at++];
            if (c == '\n') break;
            if (c == '\r') continue;
            if (n + 1 < out_cap) out[n++] = (char)c;
        }
        out[n] = '\0';
        return true;
    }

    static inline bool ParseDims(const char* line, int& w, int& h) noexcept {
        // Expect "-Y <h> +X <w>"
        if (!line) return false;
        if (!StartsWith(line, "-Y ")) return false;
        const char* p = line + 3;
        int hh = 0;
        if (!ParsePositiveInt(p, hh)) return false;
        while (*p == ' ') ++p;
        if (!StartsWith(p, "+X ")) return false;
        p += 3;
        int ww = 0;
        if (!ParsePositiveInt(p, ww)) return false;
        w = ww;
        h = hh;
        return true;
    }

    static inline void ConvertRgbe(float* out, const uint8_t* rgbe, int req_comp) noexcept {
        if (rgbe[3] != 0) {
            const float f1 = ldexpf(1.0f, (int)rgbe[3] - (128 + 8));
            if (req_comp <= 2) {
                out[0] = (rgbe[0] + rgbe[1] + rgbe[2]) * f1 / 3.0f;
            } else {
                out[0] = rgbe[0] * f1;
                out[1] = rgbe[1] * f1;
                out[2] = rgbe[2] * f1;
            }
            if (req_comp == 2) out[1] = 1.0f;
            if (req_comp == 4) out[3] = 1.0f;
        } else {
            if (req_comp == 4) {
                out[0] = out[1] = out[2] = 0.0f;
                out[3] = 1.0f;
            } else if (req_comp == 3) {
                out[0] = out[1] = out[2] = 0.0f;
            } else if (req_comp == 2) {
                out[0] = 0.0f;
                out[1] = 1.0f;
            } else {
                out[0] = 0.0f;
            }
        }
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count,
                                   int& w, int& h, size_t& data_offset) noexcept {
        SetError(nullptr);
        if (!IsHdr(bytes, byte_count)) return false;

        size_t at = 0;
        char line[512];
        if (!ReadLine(bytes, (size_t)byte_count, at, line, sizeof(line))) return false;
        if (!(StrEq(line, "#?RADIANCE") || StrEq(line, "#?RGBE"))) return false;

        bool valid_format = false;
        for (;;) {
            if (!ReadLine(bytes, (size_t)byte_count, at, line, sizeof(line))) {
                SetError("bad HDR header");
                return false;
            }
            if (line[0] == '\0') break;
            if (StrEq(line, "FORMAT=32-bit_rle_rgbe")) valid_format = true;
        }
        if (!valid_format) {
            SetError("unsupported HDR format");
            return false;
        }

        if (!ReadLine(bytes, (size_t)byte_count, at, line, sizeof(line))) {
            SetError("missing HDR dimensions");
            return false;
        }
        if (!ParseDims(line, w, h)) {
            SetError("unsupported HDR layout");
            return false;
        }
        data_offset = at;
        return true;
    }

    static inline bool DecodeFlat(const uint8_t* bytes, size_t len, size_t& at,
                                  int w, int h, int req_comp, float* out) noexcept {
        const size_t px_count = (size_t)w * (size_t)h;
        for (size_t i = 0; i < px_count; ++i) {
            if (at + 4 > len) return false;
            ConvertRgbe(out + i * (size_t)req_comp, bytes + at, req_comp);
            at += 4;
        }
        return true;
    }

    static inline bool DecodeRle(const uint8_t* bytes, size_t len, size_t& at,
                                 int w, int h, int req_comp, float* out) noexcept {
        uint8_t* scan = (uint8_t*)malloc((size_t)w * 4u);
        if (!scan) {
            SetError("out of memory");
            return false;
        }

        for (int j = 0; j < h; ++j) {
            if (at + 4 > len) {
                free(scan);
                return false;
            }
            const uint8_t c1 = bytes[at++];
            const uint8_t c2 = bytes[at++];
            uint8_t len_hi = bytes[at++];

            if (c1 != 2 || c2 != 2 || (len_hi & 0x80)) {
                // old flat layout fallback from this point:
                at -= 3;
                bool ok = DecodeFlat(bytes, len, at, w, h - j, req_comp, out + (size_t)j * (size_t)w * (size_t)req_comp);
                free(scan);
                return ok;
            }

            if (at >= len) {
                free(scan);
                return false;
            }
            const int scan_w = (int)((uint16_t(len_hi) << 8) | uint16_t(bytes[at++]));
            if (scan_w != w) {
                free(scan);
                SetError("bad HDR scanline width");
                return false;
            }

            for (int k = 0; k < 4; ++k) {
                int i = 0;
                while (i < w) {
                    if (at >= len) {
                        free(scan);
                        return false;
                    }
                    uint8_t count = bytes[at++];
                    if (count > 128) {
                        count = (uint8_t)(count - 128);
                        if (count == 0 || i + count > w || at >= len) {
                            free(scan);
                            SetError("bad HDR RLE run");
                            return false;
                        }
                        const uint8_t v = bytes[at++];
                        for (uint8_t z = 0; z < count; ++z) scan[(i++ * 4) + k] = v;
                    } else {
                        if (count == 0 || i + count > w || at + count > len) {
                            free(scan);
                            SetError("bad HDR RLE raw");
                            return false;
                        }
                        for (uint8_t z = 0; z < count; ++z) scan[(i++ * 4) + k] = bytes[at++];
                    }
                }
            }

            for (int i = 0; i < w; ++i) {
                ConvertRgbe(out + ((size_t)j * (size_t)w + (size_t)i) * (size_t)req_comp, scan + (size_t)i * 4u, req_comp);
            }
        }

        free(scan);
        return true;
    }

    static inline void* LoadF32(const uint8_t* bytes, int byte_count,
                                int* x, int* y, int* comp, int req_comp) noexcept {
        int w = 0, h = 0;
        size_t at = 0;
        if (!ParseHeader(bytes, byte_count, w, h, at)) return nullptr;

        if (comp) *comp = 3;
        if (req_comp == 0) req_comp = 3;
        if (req_comp < 1 || req_comp > 4) {
            SetError("bad req_comp");
            return nullptr;
        }

        const size_t total = (size_t)w * (size_t)h * (size_t)req_comp;
        float* out = (float*)malloc(total * sizeof(float));
        if (!out) {
            SetError("out of memory");
            return nullptr;
        }

        bool ok = false;
        if (w < 8 || w >= 32768) {
            ok = DecodeFlat(bytes, (size_t)byte_count, at, w, h, req_comp, out);
        } else {
            ok = DecodeRle(bytes, (size_t)byte_count, at, w, h, req_comp, out);
        }
        if (!ok) {
            free(out);
            if (!LastError()) SetError("corrupt HDR data");
            return nullptr;
        }

        if (x) *x = w;
        if (y) *y = h;
        return out;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        const int out_comp = req_comp ? req_comp : 3;
        float* f = (float*)LoadF32(bytes, byte_count, x, y, comp, out_comp);
        if (!f) return nullptr;

        const size_t px_count = (size_t)(*x) * (size_t)(*y);
        uint8_t* out = (uint8_t*)malloc(px_count * (size_t)out_comp);
        if (!out) {
            free(f);
            SetError("out of memory");
            return nullptr;
        }

        const int n = (out_comp & 1) ? out_comp : (out_comp - 1);
        for (size_t i = 0; i < px_count; ++i) {
            int k = 0;
            for (; k < n; ++k) {
                float z = powf(f[i * (size_t)out_comp + (size_t)k] * H2LScaleInv(), H2LGammaInv()) * 255.0f + 0.5f;
                if (z < 0.0f) z = 0.0f;
                if (z > 255.0f) z = 255.0f;
                out[i * (size_t)out_comp + (size_t)k] = (uint8_t)((int)z);
            }
            if (k < out_comp) {
                float z = f[i * (size_t)out_comp + (size_t)k] * 255.0f + 0.5f;
                if (z < 0.0f) z = 0.0f;
                if (z > 255.0f) z = 255.0f;
                out[i * (size_t)out_comp + (size_t)k] = (uint8_t)((int)z);
            }
        }

        free(f);
        return out;
    }
};

} // namespace detail
} // namespace stbi
