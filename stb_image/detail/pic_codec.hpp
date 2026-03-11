#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace stbi { namespace detail {

struct PicCodec {
    struct Packet {
        uint8_t size{};
        uint8_t type{};
        uint8_t channel{};
    };

    struct Header {
        int width{};
        int height{};
        int comp{};
        Packet packets[10]{};
        int packet_count{};
        size_t data_offset{};
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

    static inline uint16_t ReadU16Be(const uint8_t* p) noexcept {
        return (uint16_t)((uint16_t(p[0]) << 8) | uint16_t(p[1]));
    }

    static inline bool IsPic(const uint8_t* b, int n) noexcept {
        if (!b || n < 92) return false;
        if (!(b[0] == 0x53 && b[1] == 0x80 && b[2] == 0xF6 && b[3] == 0x34)) return false;
        if (!(b[88] == 'P' && b[89] == 'I' && b[90] == 'C' && b[91] == 'T')) return false;
        return true;
    }

    static inline bool ParseHeader(const uint8_t* bytes, int byte_count, Header& out) noexcept {
        SetError(nullptr);
        if (!IsPic(bytes, byte_count)) return false;

        const size_t len = (size_t)byte_count;
        size_t at = 92; // magic + 84 + "PICT"
        if (at + 12 > len) {
            SetError("truncated PIC header");
            return false;
        }

        Header h{};
        h.width = (int)ReadU16Be(bytes + at); at += 2;
        h.height = (int)ReadU16Be(bytes + at); at += 2;
        at += 4; // ratio
        at += 2; // fields
        at += 2; // pad

        if (h.width <= 0 || h.height <= 0) {
            SetError("bad PIC dimensions");
            return false;
        }

        int act_comp = 0;
        int num_packets = 0;
        for (;;) {
            if (at + 4 > len) {
                SetError("truncated PIC packet header");
                return false;
            }
            if (num_packets >= 10) {
                SetError("too many PIC packets");
                return false;
            }

            const uint8_t chained = bytes[at++];
            Packet& p = h.packets[num_packets++];
            p.size = bytes[at++];
            p.type = bytes[at++];
            p.channel = bytes[at++];

            if (p.size != 8) {
                SetError("unsupported PIC packet size");
                return false;
            }

            act_comp |= p.channel;
            if (!chained) break;
        }

        h.packet_count = num_packets;
        h.comp = (act_comp & 0x10) ? 4 : 3;
        h.data_offset = at;
        out = h;
        return true;
    }

    static inline bool ReadVal(const uint8_t* bytes, size_t len, size_t& at,
                               int channel, uint8_t* dest) noexcept {
        int mask = 0x80;
        for (int i = 0; i < 4; ++i, mask >>= 1) {
            if (channel & mask) {
                if (at >= len) {
                    SetError("truncated PIC payload");
                    return false;
                }
                dest[i] = bytes[at++];
            }
        }
        return true;
    }

    static inline void CopyVal(int channel, uint8_t* dest, const uint8_t* src) noexcept {
        int mask = 0x80;
        for (int i = 0; i < 4; ++i, mask >>= 1) {
            if (channel & mask) {
                dest[i] = src[i];
            }
        }
    }

    static inline bool LoadPayload(const uint8_t* bytes, size_t len, size_t& at,
                                   const Header& h, uint8_t* rgba) noexcept {
        for (int y = 0; y < h.height; ++y) {
            for (int pi = 0; pi < h.packet_count; ++pi) {
                const Packet& packet = h.packets[pi];
                uint8_t* dest = rgba + (size_t)y * (size_t)h.width * 4u;

                if (packet.type == 0) { // uncompressed
                    for (int x = 0; x < h.width; ++x, dest += 4) {
                        if (!ReadVal(bytes, len, at, packet.channel, dest)) return false;
                    }
                } else if (packet.type == 1) { // pure RLE
                    int left = h.width;
                    while (left > 0) {
                        if (at >= len) {
                            SetError("truncated PIC pure-RLE");
                            return false;
                        }
                        int count = (int)bytes[at++];
                        if (count <= 0) {
                            SetError("corrupt PIC pure-RLE count");
                            return false;
                        }
                        if (count > left) count = left;

                        uint8_t value[4] = {0, 0, 0, 0};
                        if (!ReadVal(bytes, len, at, packet.channel, value)) return false;

                        for (int i = 0; i < count; ++i, dest += 4) {
                            CopyVal(packet.channel, dest, value);
                        }
                        left -= count;
                    }
                } else if (packet.type == 2) { // mixed RLE
                    int left = h.width;
                    while (left > 0) {
                        if (at >= len) {
                            SetError("truncated PIC mixed-RLE");
                            return false;
                        }
                        int count = (int)bytes[at++];
                        if (count >= 128) {
                            if (count == 128) {
                                if (at + 2 > len) {
                                    SetError("truncated PIC mixed-RLE long count");
                                    return false;
                                }
                                count = (int)ReadU16Be(bytes + at);
                                at += 2;
                            } else {
                                count -= 127;
                            }
                            if (count <= 0 || count > left) {
                                SetError("corrupt PIC mixed-RLE run");
                                return false;
                            }
                            uint8_t value[4] = {0, 0, 0, 0};
                            if (!ReadVal(bytes, len, at, packet.channel, value)) return false;
                            for (int i = 0; i < count; ++i, dest += 4) {
                                CopyVal(packet.channel, dest, value);
                            }
                        } else {
                            count += 1;
                            if (count <= 0 || count > left) {
                                SetError("corrupt PIC mixed-RLE raw");
                                return false;
                            }
                            for (int i = 0; i < count; ++i, dest += 4) {
                                if (!ReadVal(bytes, len, at, packet.channel, dest)) return false;
                            }
                        }
                        left -= count;
                    }
                } else {
                    SetError("unsupported PIC compression type");
                    return false;
                }
            }
        }
        return true;
    }

    static inline void* LoadU8(const uint8_t* bytes, int byte_count,
                               int* x, int* y, int* comp, int req_comp) noexcept {
        Header h{};
        if (!ParseHeader(bytes, byte_count, h)) return nullptr;

        const size_t px_count = (size_t)h.width * (size_t)h.height;
        uint8_t* rgba = (uint8_t*)malloc(px_count * 4u);
        if (!rgba) {
            SetError("out of memory");
            return nullptr;
        }
        memset(rgba, 0xff, px_count * 4u);

        size_t at = h.data_offset;
        if (!LoadPayload(bytes, (size_t)byte_count, at, h, rgba)) {
            free(rgba);
            return nullptr;
        }

        int out_comp = req_comp;
        if (out_comp == 0) out_comp = h.comp;

        void* out = PngCodec::ConvertU8(rgba, h.width, h.height, 4, out_comp);
        free(rgba);
        if (!out) {
            SetError("PIC channel conversion failed");
            return nullptr;
        }

        if (x) *x = h.width;
        if (y) *y = h.height;
        if (comp) *comp = h.comp;
        return out;
    }
};

} // namespace detail
} // namespace stbi
