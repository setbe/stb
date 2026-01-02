#pragma once

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

namespace stbtt {
    namespace detail {
        struct Buf {
            uint8_t* data;
            int cursor;
            int size;

            inline uint8_t Get8() noexcept;
            inline uint8_t Peek8() const noexcept;
            inline void Seek(int o) noexcept;
            inline void Skip(int o) noexcept { Seek(cursor + o); }
            inline uint32_t Get(int n) noexcept;
            inline uint32_t Get16() noexcept { return Get(2); }
            inline uint32_t Get32() noexcept { return Get(4); }

            inline Buf Range(int o, int s) const noexcept;

            inline Buf CffGetIndex() noexcept;
            inline Buf CffGetIndex(int i) noexcept; // Overloaded method
            inline uint32_t CffInt() noexcept;
            inline void CffSkipOperand() noexcept;
            inline int CffIndexCount() noexcept { Seek(0); return Get16(); }
            inline Buf DictGet(int key) noexcept;
            inline void DictGetInts(int key, int outcount, uint32_t* out) noexcept;


            static inline Buf GetSubr(Buf& idx, int n) noexcept;
            static inline Buf GetSubrs(Buf& cff, Buf& fontdict) noexcept;
        }; // struct Buf



        inline uint8_t Buf::Get8() noexcept {
            if (cursor >= size) return 0;
            return data[cursor++];
        }

        inline uint8_t Buf::Peek8() const noexcept {
            if (cursor >= size) return 0;
            return data[cursor];
        }

        inline void Buf::Seek(int o) noexcept {
            STBTT_assert(!(o > size || o < 0));
            cursor = (o > size || o < 0) ? size : o;
        }

        inline uint32_t Buf::Get(int n) noexcept {
            STBTT_assert(n >= 1 && n <= 4);
            uint32_t v = 0;
            for (int i = 0; i < n; ++i)
                v = (v << 8) | Get8();
            return v;
        }

        inline Buf Buf::Range(int o, int s) const noexcept {
            Buf r{};
            if (o < 0 || s < 0 || o > size || s > size - o)
                return r;
            r.data = data + o;
            r.size = s;
            return r;
        }

        inline Buf Buf::CffGetIndex() noexcept {
            int count, start, offsize;
            start = cursor;
            count = Get16();
            if (count) {
                offsize = Get8();
                STBTT_assert(offsize >= 1 && offsize <= 4);
                Skip(offsize * count);
                Skip(Get(offsize) - 1);
            }
            return Range(start, cursor - start);
        }

        inline uint32_t Buf::CffInt() noexcept {
            int b0 = Get8();
            if (b0 >= 32 && b0 <= 246)       return b0 - 139;
            else if (b0 >= 247 && b0 <= 250) return (b0 - 247) * 256 + Get8() + 108;
            else if (b0 >= 251 && b0 <= 254) return -(b0 - 251) * 256 - Get8() - 108;
            else if (b0 == 28)               return Get16();
            else if (b0 == 29)               return Get32();
            STBTT_assert(0);
            return 0;
        }

        inline void Buf::CffSkipOperand() noexcept {
            int v, b0 = Peek8();
            STBTT_assert(b0 >= 28);
            if (b0 != 30) {
                CffInt();
            }
            else {
                Skip(1);
                while (cursor < size) {
                    v = Get8();
                    if ((v & 0xF) == 0xF || (v >> 4) == 0xF)
                        break;
                }
            }
        }

        inline Buf Buf::DictGet(int key) noexcept {
            Seek(0);
            while (cursor < size) {
                int start = cursor, end, op;
                while (Peek8() >= 28) CffSkipOperand();
                end = cursor;
                op = Get8();
                if (op == 12)  op = Get8() | 0x100;
                if (op == key)
                    return Range(start, end - start);
            }
            return Range(0, 0);
        }

        inline void Buf::DictGetInts(int key, int outcount, uint32_t* out) noexcept {
            int i;
            Buf operands = DictGet(key);
            for (i = 0; i < outcount && operands.cursor < operands.size; i++)
                out[i] = operands.CffInt();
        }

        // Overloaded method
        inline Buf Buf::CffGetIndex(int i) noexcept {
            int count, offsize, start, end;
            Seek(0);
            count = Get16();
            offsize = Get8();
            STBTT_assert(i >= 0 && i < count);
            STBTT_assert(offsize >= 1 && offsize <= 4);
            Skip(i * offsize);
            start = Get(offsize);
            end = Get(offsize);
            return Range(2 + (count + 1) * offsize + start,
                end - start);
        }


        inline Buf Buf::GetSubr(Buf& idx, int n) noexcept {
            int count = idx.CffIndexCount();
            int bias = 107;
            if (count >= 33900) bias = 32768;
            else if (count >= 1240)  bias = 1131;

            n += bias;
            return (n < 0 || n >= count) ? Buf{}
            : idx.CffGetIndex(n);
        }

        inline Buf Buf::GetSubrs(Buf& cff, Buf& fontdict) noexcept {
            uint32_t subrsoff{};
            uint32_t private_loc[2]{};

            fontdict.DictGetInts(18, 2, private_loc);
            if (!private_loc[1] || !private_loc[0])
                return Buf{};

            Buf pdict = cff.Range(private_loc[1], private_loc[0]);
            pdict.DictGetInts(19, 1, &subrsoff);
            if (!subrsoff)
                return Buf{};

            cff.Seek(private_loc[1] + subrsoff);
            return cff.CffGetIndex();
        }
    } // namespace detail
} // namespace stbtt