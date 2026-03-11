#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct GifCodec {
    struct Header {
        int width{};
        int height{};
        uint8_t packed{};
        uint8_t bg_index{};
        uint8_t aspect{};
        bool has_gct{};
        int gct_entries{};
        size_t gct_offset{};
        size_t after_header{};
    };

    struct GraphicControl {
        int transparent_index{-1};
        uint8_t flags{};
    };

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

    static inline uint16_t ReadU16Le(const uint8_t* p) noexcept {
        return (uint16_t)(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
    }

    static inline bool IsGif(const uint8_t* b, int n) noexcept {
        if (!b || n < 13) return false;
        const bool v87 = b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8' && b[4] == '7' && b[5] == 'a';
        const bool v89 = b[0] == 'G' && b[1] == 'I' && b[2] == 'F' && b[3] == '8' && b[4] == '9' && b[5] == 'a';
        return v87 || v89;
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count, Header& out) noexcept {
        SetError(nullptr);
        if (!IsGif(bytes, byte_count)) return false;
        if (byte_count < 13) {
            SetError("truncated GIF header");
            return false;
        }

        Header h{};
        h.width = (int)ReadU16Le(bytes + 6);
        h.height = (int)ReadU16Le(bytes + 8);
        h.packed = bytes[10];
        h.bg_index = bytes[11];
        h.aspect = bytes[12];

        if (h.width <= 0 || h.height <= 0) {
            SetError("bad GIF dimensions");
            return false;
        }

        h.has_gct = (h.packed & 0x80u) != 0;
        h.gct_entries = h.has_gct ? (1 << ((h.packed & 0x07u) + 1u)) : 0;
        h.gct_offset = 13u;
        h.after_header = 13u + (size_t)h.gct_entries * 3u;

        if (h.after_header > (size_t)byte_count) {
            SetError("truncated GIF color table");
            return false;
        }

        out = h;
        return true;
    }

    static inline bool SkipSubBlocks(const uint8_t* bytes, size_t len, size_t& at) noexcept {
        while (at < len) {
            const uint8_t n = bytes[at++];
            if (n == 0) return true;
            if (at + (size_t)n > len) {
                SetError("truncated GIF sub-block");
                return false;
            }
            at += (size_t)n;
        }
        SetError("truncated GIF stream");
        return false;
    }

    static inline bool ParseColorTable(const uint8_t* src, int entries, int transparent_index,
                                       uint8_t table[256][4]) noexcept {
        if (!src || entries <= 0 || entries > 256) return false;
        for (int i = 0; i < entries; ++i) {
            const uint8_t r = src[i * 3 + 0];
            const uint8_t g = src[i * 3 + 1];
            const uint8_t b = src[i * 3 + 2];
            table[i][0] = r;
            table[i][1] = g;
            table[i][2] = b;
            table[i][3] = (i == transparent_index) ? 0 : 255;
        }
        for (int i = entries; i < 256; ++i) {
            table[i][0] = 0;
            table[i][1] = 0;
            table[i][2] = 0;
            table[i][3] = 255;
        }
        return true;
    }

    static inline bool CollectImageData(const uint8_t* bytes, size_t len, size_t& at,
                                        uint8_t*& out_data, size_t& out_bytes) noexcept {
        out_data = nullptr;
        out_bytes = 0;

        if (at >= len) {
            SetError("truncated GIF image data");
            return false;
        }
        ++at; // skip LZW min code size, caller already validated

        while (at < len) {
            const uint8_t n = bytes[at++];
            if (n == 0) return true;
            if (at + (size_t)n > len) {
                free(out_data);
                out_data = nullptr;
                out_bytes = 0;
                SetError("truncated GIF image data block");
                return false;
            }
            const size_t old_bytes = out_bytes;
            const size_t new_bytes = old_bytes + (size_t)n;
            uint8_t* next = (uint8_t*)realloc(out_data, new_bytes ? new_bytes : 1u);
            if (!next) {
                free(out_data);
                out_data = nullptr;
                out_bytes = 0;
                SetError("out of memory");
                return false;
            }
            out_data = next;
            memcpy(out_data + old_bytes, bytes + at, (size_t)n);
            out_bytes = new_bytes;
            at += (size_t)n;
        }

        free(out_data);
        out_data = nullptr;
        out_bytes = 0;
        SetError("truncated GIF image data");
        return false;
    }

    static inline bool LzwDecode(const uint8_t* data, size_t data_bytes, int min_code_size,
                                 uint8_t* out, size_t out_count) noexcept {
        if (!data || !out || out_count == 0) return false;
        if (min_code_size < 2 || min_code_size > 8) {
            SetError("unsupported GIF LZW code size");
            return false;
        }

        uint16_t prefix[4096]{};
        uint8_t suffix[4096]{};
        uint8_t stack[4096]{};

        const int clear = 1 << min_code_size;
        const int end_code = clear + 1;
        int next_code = clear + 2;
        int code_size = min_code_size + 1;
        int code_mask = (1 << code_size) - 1;

        for (int i = 0; i < clear; ++i) {
            prefix[i] = 0;
            suffix[i] = (uint8_t)i;
        }

        uint32_t bit_buffer = 0;
        int bit_count = 0;
        size_t in_at = 0;
        size_t out_at = 0;
        int old_code = -1;
        uint8_t first = 0;

        while (out_at < out_count) {
            while (bit_count < code_size) {
                if (in_at >= data_bytes) {
                    SetError("truncated GIF LZW stream");
                    return false;
                }
                bit_buffer |= (uint32_t)data[in_at++] << bit_count;
                bit_count += 8;
            }

            const int code = (int)(bit_buffer & (uint32_t)code_mask);
            bit_buffer >>= code_size;
            bit_count -= code_size;

            if (code == clear) {
                next_code = clear + 2;
                code_size = min_code_size + 1;
                code_mask = (1 << code_size) - 1;
                old_code = -1;
                continue;
            }
            if (code == end_code) {
                break;
            }
            if (code > 4095) {
                SetError("corrupt GIF LZW code");
                return false;
            }

            int cur = code;
            int in_code = code;
            int top = 0;

            if (cur >= next_code) {
                if (old_code < 0) {
                    SetError("corrupt GIF LZW stream");
                    return false;
                }
                stack[top++] = first;
                cur = old_code;
            }

            while (cur >= clear) {
                if (cur >= 4096 || top >= 4096) {
                    SetError("corrupt GIF LZW chain");
                    return false;
                }
                stack[top++] = suffix[cur];
                cur = prefix[cur];
            }
            if (cur < 0 || cur >= clear) {
                SetError("corrupt GIF LZW symbol");
                return false;
            }

            first = suffix[cur];
            if (top >= 4096) {
                SetError("corrupt GIF LZW stack");
                return false;
            }
            stack[top++] = first;

            while (top > 0) {
                if (out_at >= out_count) break;
                out[out_at++] = stack[--top];
            }

            if (old_code >= 0 && next_code < 4096) {
                prefix[next_code] = (uint16_t)old_code;
                suffix[next_code] = first;
                ++next_code;
                if (next_code == (1 << code_size) && code_size < 12) {
                    ++code_size;
                    code_mask = (1 << code_size) - 1;
                }
            }
            old_code = in_code;
        }

        if (out_at < out_count) {
            // Match stb behavior tolerance: treat remaining pixels as 0-index color.
            for (; out_at < out_count; ++out_at) out[out_at] = 0;
        }
        return true;
    }

    static inline void BlitIndicesToRgba(const uint8_t* indices, int iw, int ih,
                                         int left, int top, bool interlaced,
                                         int screen_w, int screen_h,
                                         const uint8_t table[256][4], int table_entries,
                                         uint8_t* rgba, uint8_t* drawn) noexcept {
        auto write_row = [&](int row_in_image, int src_row) noexcept {
            const int dy = top + row_in_image;
            if (dy < 0 || dy >= screen_h) return;
            const uint8_t* src = indices + (size_t)src_row * (size_t)iw;
            for (int x = 0; x < iw; ++x) {
                const int dx = left + x;
                if (dx < 0 || dx >= screen_w) continue;
                const uint8_t idx = src[x];
                if ((int)idx >= table_entries) continue;

                const size_t p = (size_t)dy * (size_t)screen_w + (size_t)dx;
                drawn[p] = 1;

                const uint8_t* c = table[idx];
                if (c[3] > 128) {
                    uint8_t* d = rgba + p * 4u;
                    d[0] = c[0];
                    d[1] = c[1];
                    d[2] = c[2];
                    d[3] = c[3];
                }
            }
        };

        if (!interlaced) {
            for (int row = 0; row < ih; ++row) {
                write_row(row, row);
            }
            return;
        }

        int src_row = 0;
        for (int row = 0; row < ih; row += 8) write_row(row, src_row++);
        for (int row = 4; row < ih; row += 8) write_row(row, src_row++);
        for (int row = 2; row < ih; row += 4) write_row(row, src_row++);
        for (int row = 1; row < ih; row += 2) write_row(row, src_row++);
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        Header h{};
        if (!ParseHeader(bytes, byte_count, h)) return nullptr;

        uint8_t global_table[256][4]{};
        if (h.has_gct) {
            if (!ParseColorTable(bytes + h.gct_offset, h.gct_entries, -1, global_table)) {
                SetError("bad GIF color table");
                return nullptr;
            }
        }

        const size_t px_count = (size_t)h.width * (size_t)h.height;
        uint8_t* rgba = (uint8_t*)malloc(px_count * 4u);
        uint8_t* drawn = (uint8_t*)malloc(px_count ? px_count : 1u);
        if (!rgba || !drawn) {
            free(rgba);
            free(drawn);
            SetError("out of memory");
            return nullptr;
        }
        memset(rgba, 0, px_count * 4u);
        memset(drawn, 0, px_count);

        GraphicControl gce{};
        size_t at = h.after_header;
        bool got_image = false;

        while (at < (size_t)byte_count) {
            const uint8_t tag = bytes[at++];
            if (tag == 0x3B) { // trailer
                break;
            }

            if (tag == 0x21) { // extension
                if (at >= (size_t)byte_count) {
                    SetError("truncated GIF extension");
                    free(rgba);
                    free(drawn);
                    return nullptr;
                }
                const uint8_t ext = bytes[at++];
                if (ext == 0xF9) { // Graphic Control Extension
                    if (at >= (size_t)byte_count) {
                        SetError("truncated GIF GCE");
                        free(rgba);
                        free(drawn);
                        return nullptr;
                    }
                    const uint8_t len = bytes[at++];
                    if (len != 4 || at + 4 > (size_t)byte_count) {
                        SetError("bad GIF GCE");
                        free(rgba);
                        free(drawn);
                        return nullptr;
                    }
                    gce.flags = bytes[at + 0];
                    // delay is bytes[at+1..2], ignored
                    const uint8_t transp = bytes[at + 3];
                    gce.transparent_index = (gce.flags & 0x01u) ? (int)transp : -1;
                    at += 4;
                    if (at >= (size_t)byte_count || bytes[at] != 0) {
                        SetError("bad GIF GCE terminator");
                        free(rgba);
                        free(drawn);
                        return nullptr;
                    }
                    ++at;
                } else {
                    if (!SkipSubBlocks(bytes, (size_t)byte_count, at)) {
                        free(rgba);
                        free(drawn);
                        return nullptr;
                    }
                }
                continue;
            }

            if (tag != 0x2C) {
                SetError("unknown GIF block");
                free(rgba);
                free(drawn);
                return nullptr;
            }

            if (at + 9 > (size_t)byte_count) {
                SetError("truncated GIF image descriptor");
                free(rgba);
                free(drawn);
                return nullptr;
            }

            const int left = (int)ReadU16Le(bytes + at + 0);
            const int top = (int)ReadU16Le(bytes + at + 2);
            const int iw = (int)ReadU16Le(bytes + at + 4);
            const int ih = (int)ReadU16Le(bytes + at + 6);
            const uint8_t ipacked = bytes[at + 8];
            at += 9;

            if (iw < 0 || ih < 0 || left < 0 || top < 0 || left + iw > h.width || top + ih > h.height) {
                SetError("bad GIF image bounds");
                free(rgba);
                free(drawn);
                return nullptr;
            }

            const bool has_lct = (ipacked & 0x80u) != 0;
            const bool interlaced = (ipacked & 0x40u) != 0;
            const int lct_entries = has_lct ? (1 << ((ipacked & 0x07u) + 1u)) : 0;

            uint8_t local_table[256][4]{};
            const uint8_t (*active_table)[4] = nullptr;
            int active_entries = 0;

            if (has_lct) {
                const size_t lct_bytes = (size_t)lct_entries * 3u;
                if (at + lct_bytes > (size_t)byte_count) {
                    SetError("truncated GIF local table");
                    free(rgba);
                    free(drawn);
                    return nullptr;
                }
                if (!ParseColorTable(bytes + at, lct_entries, gce.transparent_index, local_table)) {
                    SetError("bad GIF local table");
                    free(rgba);
                    free(drawn);
                    return nullptr;
                }
                at += lct_bytes;
                active_table = local_table;
                active_entries = lct_entries;
            } else {
                if (!h.has_gct) {
                    SetError("missing GIF color table");
                    free(rgba);
                    free(drawn);
                    return nullptr;
                }
                if (!ParseColorTable(bytes + h.gct_offset, h.gct_entries, gce.transparent_index, global_table)) {
                    SetError("bad GIF global table");
                    free(rgba);
                    free(drawn);
                    return nullptr;
                }
                active_table = global_table;
                active_entries = h.gct_entries;
            }

            if (at >= (size_t)byte_count) {
                SetError("truncated GIF raster data");
                free(rgba);
                free(drawn);
                return nullptr;
            }
            const int min_code_size = (int)bytes[at];
            if (min_code_size > 12) {
                SetError("bad GIF LZW header");
                free(rgba);
                free(drawn);
                return nullptr;
            }

            uint8_t* packed = nullptr;
            size_t packed_bytes = 0;
            if (!CollectImageData(bytes, (size_t)byte_count, at, packed, packed_bytes)) {
                free(rgba);
                free(drawn);
                return nullptr;
            }

            const size_t idx_count = (size_t)iw * (size_t)ih;
            uint8_t* indices = (uint8_t*)malloc(idx_count ? idx_count : 1u);
            if (!indices) {
                free(packed);
                free(rgba);
                free(drawn);
                SetError("out of memory");
                return nullptr;
            }
            if (!LzwDecode(packed, packed_bytes, min_code_size, indices, idx_count)) {
                free(indices);
                free(packed);
                free(rgba);
                free(drawn);
                return nullptr;
            }
            free(packed);

            BlitIndicesToRgba(indices, iw, ih, left, top, interlaced, h.width, h.height,
                              active_table, active_entries, rgba, drawn);
            free(indices);

            got_image = true;
            break; // stbi_load() returns first frame
        }

        if (!got_image) {
            free(rgba);
            free(drawn);
            SetError("missing GIF image block");
            return nullptr;
        }

        if (h.bg_index > 0 && h.has_gct && h.bg_index < h.gct_entries) {
            const uint8_t* bg = global_table[h.bg_index];
            for (size_t i = 0; i < px_count; ++i) {
                if (drawn[i] == 0) {
                    uint8_t* p = rgba + i * 4u;
                    p[0] = bg[0];
                    p[1] = bg[1];
                    p[2] = bg[2];
                    p[3] = 255;
                }
            }
        }

        free(drawn);

        void* out = PngCodec::ConvertU8(rgba, h.width, h.height, 4, req_comp);
        free(rgba);
        if (!out) {
            SetError("GIF channel conversion failed");
            return nullptr;
        }

        if (x) *x = h.width;
        if (y) *y = h.height;
        if (comp) *comp = 4;
        return out;
    }
};

} // namespace detail
} // namespace stbi
