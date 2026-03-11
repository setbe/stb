#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct PnmCodec {
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

    static inline bool IsPnm(const uint8_t* b, int n) noexcept {
        return b && n >= 2 && b[0] == 'P' && (b[1] == '5' || b[1] == '6');
    }

    static inline bool IsSpace(uint8_t c) noexcept {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    }

    static inline bool SkipWsAndComments(const uint8_t* bytes, size_t len, size_t& at) noexcept {
        while (at < len) {
            while (at < len && IsSpace(bytes[at])) ++at;
            if (at >= len) return true;
            if (bytes[at] != '#') return true;
            while (at < len && bytes[at] != '\n' && bytes[at] != '\r') ++at;
        }
        return true;
    }

    static inline bool ParseInt(const uint8_t* bytes, size_t len, size_t& at, int& out) noexcept {
        if (!SkipWsAndComments(bytes, len, at)) return false;
        if (at >= len || bytes[at] < '0' || bytes[at] > '9') return false;
        int v = 0;
        while (at < len && bytes[at] >= '0' && bytes[at] <= '9') {
            const int d = (int)(bytes[at] - '0');
            if (v > 214748364 || (v == 214748364 && d > 7)) return false;
            v = v * 10 + d;
            ++at;
        }
        out = v;
        return true;
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count,
                                   int& x, int& y, int& comp, int& maxval,
                                   size_t& data_offset) noexcept {
        SetError(nullptr);
        if (!IsPnm(bytes, byte_count)) return false;

        comp = (bytes[1] == '6') ? 3 : 1;
        size_t at = 2;

        if (!ParseInt(bytes, (size_t)byte_count, at, x) || x <= 0) {
            SetError("bad PNM width");
            return false;
        }
        if (!ParseInt(bytes, (size_t)byte_count, at, y) || y <= 0) {
            SetError("bad PNM height");
            return false;
        }
        if (!ParseInt(bytes, (size_t)byte_count, at, maxval) || maxval <= 0 || maxval > 65535) {
            SetError("bad PNM max value");
            return false;
        }

        if (at >= (size_t)byte_count) {
            SetError("truncated PNM header");
            return false;
        }
        if (!IsSpace(bytes[at])) {
            SetError("bad PNM separator");
            return false;
        }
        ++at;
        data_offset = at;
        return true;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        int w = 0, h = 0, c = 0, maxv = 0;
        size_t data_at = 0;
        if (!ParseHeader(bytes, byte_count, w, h, c, maxv, data_at)) return nullptr;
        if (maxv > 255) {
            SetError("PNM clean decoder supports only <=8-bit");
            return nullptr;
        }

        const size_t src_bytes = (size_t)w * (size_t)h * (size_t)c;
        if (data_at + src_bytes > (size_t)byte_count) {
            SetError("truncated PNM data");
            return nullptr;
        }

        uint8_t* src = (uint8_t*)malloc(src_bytes);
        if (!src) {
            SetError("out of memory");
            return nullptr;
        }
        memcpy(src, bytes + data_at, src_bytes);

        if (maxv != 255) {
            for (size_t i = 0; i < src_bytes; ++i) {
                src[i] = (uint8_t)(((uint32_t)src[i] * 255u + (uint32_t)(maxv / 2)) / (uint32_t)maxv);
            }
        }

        void* out = PngCodec::ConvertU8(src, w, h, c, req_comp);
        free(src);
        if (!out) {
            SetError("PNM channel conversion failed");
            return nullptr;
        }

        if (x) *x = w;
        if (y) *y = h;
        if (comp) *comp = c;
        return out;
    }
};

} // namespace detail
} // namespace stbi
