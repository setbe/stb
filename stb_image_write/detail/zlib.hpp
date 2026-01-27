#pragma once

#include "libc_integration.hpp"

// ------------------- Freestanding-friendly Includes -------------------------
#include <cstddef>
#include <cstdint>

namespace stbiw {
namespace zlib {
    static inline int iabs(int x) noexcept { return x < 0 ? -x : x; }

    static inline std::uint8_t paeth(int a, int b, int c) noexcept {
        const int p = a + b - c;
        const int pa = iabs(p-a);
        const int pb = iabs(p-b);
        const int pc = iabs(p-c);
        if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
        if (pb <= pc) return static_cast<std::uint8_t>(b);
        return static_cast<std::uint8_t>(c);
    }

    // ------------------------------ CRC32 (PNG) ------------------------------
    static inline std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* buf, int len) noexcept {
#ifdef STBIW_crc32
        // If user provides full crc32(buffer,len), we can only use it as one-shot.
        // For incremental update, fall back to builtin.
        (void)crc; (void)buf; (void)len;
        // fallthrough to builtin
#endif
        static const std::uint32_t table[256] = {
            0x00000000u,0x77073096u,0xEE0E612Cu,0x990951BAu,0x076DC419u,0x706AF48Fu,0xE963A535u,0x9E6495A3u,
            0x0EDB8832u,0x79DCB8A4u,0xE0D5E91Eu,0x97D2D988u,0x09B64C2Bu,0x7EB17CBDu,0xE7B82D07u,0x90BF1D91u,
            0x1DB71064u,0x6AB020F2u,0xF3B97148u,0x84BE41DEu,0x1ADAD47Du,0x6DDDE4EBu,0xF4D4B551u,0x83D385C7u,
            0x136C9856u,0x646BA8C0u,0xFD62F97Au,0x8A65C9ECu,0x14015C4Fu,0x63066CD9u,0xFA0F3D63u,0x8D080DF5u,
            0x3B6E20C8u,0x4C69105Eu,0xD56041E4u,0xA2677172u,0x3C03E4D1u,0x4B04D447u,0xD20D85FDu,0xA50AB56Bu,
            0x35B5A8FAu,0x42B2986Cu,0xDBBBC9D6u,0xACBCF940u,0x32D86CE3u,0x45DF5C75u,0xDCD60DCFu,0xABD13D59u,
            0x26D930ACu,0x51DE003Au,0xC8D75180u,0xBFD06116u,0x21B4F4B5u,0x56B3C423u,0xCFBA9599u,0xB8BDA50Fu,
            0x2802B89Eu,0x5F058808u,0xC60CD9B2u,0xB10BE924u,0x2F6F7C87u,0x58684C11u,0xC1611DABu,0xB6662D3Du,
            0x76DC4190u,0x01DB7106u,0x98D220BCu,0xEFD5102Au,0x71B18589u,0x06B6B51Fu,0x9FBFE4A5u,0xE8B8D433u,
            0x7807C9A2u,0x0F00F934u,0x9609A88Eu,0xE10E9818u,0x7F6A0DBBu,0x086D3D2Du,0x91646C97u,0xE6635C01u,
            0x6B6B51F4u,0x1C6C6162u,0x856530D8u,0xF262004Eu,0x6C0695EDu,0x1B01A57Bu,0x8208F4C1u,0xF50FC457u,
            0x65B0D9C6u,0x12B7E950u,0x8BBEB8EAu,0xFCB9887Cu,0x62DD1DDFu,0x15DA2D49u,0x8CD37CF3u,0xFBD44C65u,
            0x4DB26158u,0x3AB551CEu,0xA3BC0074u,0xD4BB30E2u,0x4ADFA541u,0x3DD895D7u,0xA4D1C46Du,0xD3D6F4FBu,
            0x4369E96Au,0x346ED9FCu,0xAD678846u,0xDA60B8D0u,0x44042D73u,0x33031DE5u,0xAA0A4C5Fu,0xDD0D7CC9u,
            0x5005713Cu,0x270241AAu,0xBE0B1010u,0xC90C2086u,0x5768B525u,0x206F85B3u,0xB966D409u,0xCE61E49Fu,
            0x5EDEF90Eu,0x29D9C998u,0xB0D09822u,0xC7D7A8B4u,0x59B33D17u,0x2EB40D81u,0xB7BD5C3Bu,0xC0BA6CADu,
            0xEDB88320u,0x9ABFB3B6u,0x03B6E20Cu,0x74B1D29Au,0xEAD54739u,0x9DD277AFu,0x04DB2615u,0x73DC1683u,
            0xE3630B12u,0x94643B84u,0x0D6D6A3Eu,0x7A6A5AA8u,0xE40ECF0Bu,0x9309FF9Du,0x0A00AE27u,0x7D079EB1u,
            0xF00F9344u,0x8708A3D2u,0x1E01F268u,0x6906C2FEu,0xF762575Du,0x806567CBu,0x196C3671u,0x6E6B06E7u,
            0xFED41B76u,0x89D32BE0u,0x10DA7A5Au,0x67DD4ACCu,0xF9B9DF6Fu,0x8EBEEFF9u,0x17B7BE43u,0x60B08ED5u,
            0xD6D6A3E8u,0xA1D1937Eu,0x38D8C2C4u,0x4FDFF252u,0xD1BB67F1u,0xA6BC5767u,0x3FB506DDu,0x48B2364Bu,
            0xD80D2BDAu,0xAF0A1B4Cu,0x36034AF6u,0x41047A60u,0xDF60EFC3u,0xA867DF55u,0x316E8EEFu,0x4669BE79u,
            0xCB61B38Cu,0xBC66831Au,0x256FD2A0u,0x5268E236u,0xCC0C7795u,0xBB0B4703u,0x220216B9u,0x5505262Fu,
            0xC5BA3BBEu,0xB2BD0B28u,0x2BB45A92u,0x5CB36A04u,0xC2D7FFA7u,0xB5D0CF31u,0x2CD99E8Bu,0x5BDEAE1Du,
            0x9B64C2B0u,0xEC63F226u,0x756AA39Cu,0x026D930Au,0x9C0906A9u,0xEB0E363Fu,0x72076785u,0x05005713u,
            0x95BF4A82u,0xE2B87A14u,0x7BB12BAEu,0x0CB61B38u,0x92D28E9Bu,0xE5D5BE0Du,0x7CDCEFB7u,0x0BDBDF21u,
            0x86D3D2D4u,0xF1D4E242u,0x68DDB3F8u,0x1FDA836Eu,0x81BE16CDu,0xF6B9265Bu,0x6FB077E1u,0x18B74777u,
            0x88085AE6u,0xFF0F6A70u,0x66063BCAu,0x11010B5Cu,0x8F659EFFu,0xF862AE69u,0x616BFFD3u,0x166CCF45u,
            0xA00AE278u,0xD70DD2EEu,0x4E048354u,0x3903B3C2u,0xA7672661u,0xD06016F7u,0x4969474Du,0x3E6E77DBu,
            0xAED16A4Au,0xD9D65ADCu,0x40DF0B66u,0x37D83BF0u,0xA9BCAE53u,0xDEBB9EC5u,0x47B2CF7Fu,0x30B5FFE9u,
            0xBDBDF21Cu,0xCABAC28Au,0x53B39330u,0x24B4A3A6u,0xBAD03605u,0xCDD70693u,0x54DE5729u,0x23D967BFu,
            0xB3667A2Eu,0xC4614AB8u,0x5D681B02u,0x2A6F2B94u,0xB40BBE37u,0xC30C8EA1u,0x5A05DF1Bu,0x2D02EF8Du
        };

        for (int i = 0; i < len; ++i) {
            crc = (crc >> 8) ^ table[static_cast<std::uint8_t>(buf[i] ^ (crc & 0xFFu))];
        }
        return crc;
    }

    static inline std::uint32_t crc32_one_shot(const std::uint8_t* buf, int len) noexcept {
#ifdef STBIW_crc32
        return (std::uint32_t)STBIW_crc32((unsigned char*)buf, len);
#else
        std::uint32_t crc = ~0u;
        crc = crc32_update(crc, buf, len);
        return ~crc;
#endif
    }



    // ------------------------ tiny dynamic buffer ---------------------------
    struct Buf {
        std::uint8_t* p{ nullptr };
        std::uint32_t n{ 0 };
        std::uint32_t cap{ 0 };

        void free() noexcept {
            if (p) STBIW_free(p);
            p = nullptr; n = 0; cap = 0;
        }

        bool reserve(std::uint32_t need) noexcept {
            if (need <= cap) return true;
            std::uint32_t newcap = cap?cap : 64;
            while (newcap < need) newcap = newcap * 2;
            void* np = STBIW_realloc_sized(p, static_cast<size_t>(cap), 
                                         static_cast<size_t>(newcap), nullptr);
            if (!np) return false;
            p = reinterpret_cast<std::uint8_t*>(np);
            cap = newcap;
            return true;
        }

        bool push(std::uint8_t v) noexcept {
            if (!reserve(n + 1)) return false;
            p[n++] = v;
            return true;
        }

        bool append(const void* src, std::uint32_t bytes) noexcept {
            if (bytes <= 0) return true;
            if (!reserve(n + bytes)) return false;
            STBIW_memmove(p + n, src, static_cast<size_t>(bytes));
            n += bytes;
            return true;
        }

        std::uint8_t* release() noexcept {
            std::uint8_t* r = p;
            p = nullptr; n = 0; cap = 0;
            return r;
        }
    };

    struct PtrBuf {
        const std::uint8_t** p{ nullptr };
        std::uint32_t n{ 0 };
        std::uint32_t cap{ 0 };

        void free() noexcept {
            if (p) STBIW_free(p);
            p = nullptr; n = 0; cap = 0;
        }

        bool reserve(std::uint32_t need) noexcept {
            if (need <= cap) return true;
            std::uint32_t newcap = cap?cap : 16;
            while (newcap < need) newcap = newcap * 2;
            void* np = STBIW_realloc_sized((void*)p,
                                   static_cast<size_t>(cap)    * sizeof(void*),
                                   static_cast<size_t>(newcap) * sizeof(void*),
                                   nullptr);
            if (!np) return false;
            p = reinterpret_cast<const std::uint8_t**>(np);
            cap = newcap;
            return true;
        }

        bool push(const std::uint8_t* v) noexcept {
            if (!reserve(n+1)) return false;
            p[n++] = v;
            return true;
        }

        void trim_front(std::uint32_t k) noexcept {
            if (k<=0 || k>=n) { n = 0; return; }
            STBIW_memmove(p, p+k, static_cast<size_t>(n-k) * sizeof(void*));
            n -= k;
        }
    };

    // ------------------------------ builtin deflate (fixed Huffman) ------------------------------
    static inline std::uint32_t bitrev(std::uint32_t code, std::uint32_t bits) noexcept {
        std::uint32_t res = 0;
        while (bits--) { res = (res<<1) | (code&1); code >>= 1; }
        return res;
    }

    static inline std::uint32_t zhash(const std::uint8_t* d) noexcept {
        std::uint32_t h = static_cast<std::uint32_t>(d[0])
                       + (static_cast<std::uint32_t>(d[1]) << 8)
                       + (static_cast<std::uint32_t>(d[2]) << 16);
        h ^= h<<3;   h += h>>5;
        h ^= h<<4;   h += h>>17;
        h ^= h<<25;  h += h>>6;
        return h;
    }

    static inline std::uint32_t countm(const std::uint8_t* a, const std::uint8_t* b, std::uint32_t limit) noexcept {
        std::uint32_t i = 0;
        for (; i < limit && i < 258; ++i) if (a[i] != b[i]) break;
        return i;
    }

    static unsigned char* zlib_compress_builtin(std::uint8_t* data,
                                                std::uint32_t data_len,
                                                std::uint32_t* out_len,
                                                std::uint32_t quality) noexcept
    {
        // refit to freestanding-friendly buffers
        static constexpr std::uint16_t lengthc[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258,259 };
        static constexpr std::uint8_t  lengtheb[] = { 0,0,0,0,0,0,0,0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
        static constexpr std::uint16_t distc[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,32768 };
        static constexpr std::uint8_t  disteb[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

        if (quality < 5) quality = 5;

        // hash table buckets
        constexpr std::uint32_t ZHASH = 16384;
        PtrBuf* buckets = reinterpret_cast<PtrBuf*>(STBIW_malloc(
                                   sizeof(PtrBuf) * static_cast<size_t>(ZHASH),
                                   nullptr));
        if (!buckets) return nullptr;
        // zero-init
        STBIW_memset(buckets, 0, sizeof(PtrBuf) * static_cast<size_t>(ZHASH));

        Buf out;

        std::uint32_t bitbuf, bitcount;
        bitbuf = bitcount = 0;

        auto flush_bits = [&]() noexcept -> bool {
            while (bitcount >= 8) {
                if (!out.push(static_cast<std::uint8_t>(bitbuf & 0xFFu)))
                    return false;
                bitbuf >>= 8;
                bitcount -= 8;
            }
            return true;
        };

        auto add_bits = [&](std::uint32_t code, std::uint32_t bits) noexcept -> bool {
            bitbuf |= (code << bitcount);
            bitcount += bits;
            return flush_bits();
        };

        auto huffa = [&](std::uint32_t code, std::uint32_t bits) noexcept -> bool {
            return add_bits(bitrev(code, bits), bits);
        };

        auto huff = [&](std::uint32_t n) noexcept -> bool {
            // fixed tables
            if (n <= 143) return huffa(0x30  +  n,        8);
            if (n <= 255) return huffa(0x190 + (n - 144), 9);
            if (n <= 279) return huffa(0     + (n - 256), 7);
                          return huffa(0xC0  + (n - 280), 8);
        };

        auto huffb = [&](std::uint32_t n) noexcept -> bool {
            if (n <= 143) return huffa(0x30  +  n,      8);
                          return huffa(0x190 + (n-144), 9);
        };

        std::uint32_t i = 0;

        // zlib header
        if (!out.push(0x78)) goto fail; // DEFLATE 32K window
        if (!out.push(0x5e)) goto fail; // FLEVEL=1
        if (!add_bits(1, 1)) goto fail; // BFINAL=1
        if (!add_bits(1, 2)) goto fail; // BTYPE=1 (fixed Huffman)

        while (i < data_len-3) {
            // hash next 3 bytes of data to be compressed
            const std::uint32_t h       = zhash(data+i) & (ZHASH-1);
                  std::uint32_t best    = 3;
            const std::uint8_t* bestloc = nullptr;

            PtrBuf& list = buckets[h];

            for (std::uint32_t j=0; j < list.n; ++j) {
                const std::uint8_t* loc = list.p[j];
                if (static_cast<std::size_t>(loc-data) > i-32768) { // if entry lies within window
                    const std::uint32_t d = countm(loc, data+i, data_len-i);
                    if (d >= best) { best=d; bestloc=loc; }
                }
            }

            // when hash table entry is too long, delete half the entries
            // cap list to 2*quality by trimming half
            if (list.n == 2 * quality) list.trim_front(quality);

            if (!list.push(data+i)) goto fail;

            // lazy matching - check match at *next* byte, and if it's better, do cur byte as literal
            if (bestloc) {
                const std::uint32_t h2 = zhash(data+i+1) & (ZHASH-1);
                PtrBuf& list2 = buckets[h2];
                for (std::uint32_t j = 0; j < list2.n; ++j) {
                    const std::uint8_t* loc = list2.p[j];
                    if (static_cast<std::size_t>(loc-data) > i - 32767) {
                        const std::uint32_t e = countm(loc, data+i+1, data_len-i-1);
                        // if next match is better, bail on current match
                        if (e > best) { bestloc = nullptr; break; }
                    }
                }
            }

            if (bestloc) {
                // distance back
                const std::uint32_t dist = static_cast<std::uint32_t>((data+i) - bestloc);
                STBIW_assert(dist <= 32767 && best <= 258);

                std::uint32_t j = 0;

                while (best > static_cast<std::uint32_t>(lengthc[j+1]-1))
                    ++j;
                if (!huff(j+257)) goto fail;
                if (lengtheb[j] && !add_bits(best - lengthc[j], lengtheb[j])) goto fail;

                j = 0;

                while (dist > static_cast<std::uint32_t>(distc[j+1]-1))
                    ++j;
                if (!add_bits(bitrev(j, 5), 5)) goto fail;
                if (disteb[j] && !add_bits((dist - distc[j]), disteb[j])) goto fail;

                i += best;
            }
            else {
                if (!huffb(data[i])) goto fail;
                ++i;
            }
        }

        // write out final bytes
        for (; i < data_len; ++i) if (!huffb(data[i])) goto fail;
        if (!huff(256)) goto fail; // end block

        // pad with 0 bits to byte boundary
        while (bitcount) if (!add_bits(0, 1)) goto fail;

        // free buckets lists
        for (int k = 0; k < ZHASH; ++k) buckets[k].free();
        STBIW_free(buckets);

        { // fallback to uncompressed if compression was worse
            const std::uint32_t min_store_overhead =
                    2 + ((data_len + 32766) / 32767) * 5;

            if (out.n > data_len + min_store_overhead) {
                out.n = 2; // keep zlib header
                for (std::uint32_t j = 0; j < data_len;) {

                    const std::uint32_t blocklen =
                        data_len-j > 32767 ? data_len-j : 32767;

                    const std::uint8_t bfinal =
                        static_cast<std::uint8_t>(data_len-j == blocklen);

                    if (!out.push(bfinal)) goto fail;   // BFINAL + BTYPE=0 packed into byte as stb does (valid for stored blocks)
                    if (!out.push(static_cast<std::uint8_t>( blocklen       & 0xFF))) goto fail;
                    if (!out.push(static_cast<std::uint8_t>((blocklen >> 8) & 0xFF))) goto fail;

                    const std::uint16_t nlen = static_cast<std::uint16_t>( ~ blocklen);

                    if (!out.push(static_cast<std::uint8_t>( nlen       & 0xFF))) goto fail;
                    if (!out.push(static_cast<std::uint8_t>((nlen >> 8) & 0xFF))) goto fail;

                    if (!out.append(data + j, blocklen)) goto fail;
                    j += blocklen;
                }
            }
        }

        // adler32
        {
            std::uint32_t s1, s2, j;
            s1 = 1;
            s2=j=0;

            std::uint32_t blocklen = data_len % 5552;
            while (j < data_len) {
                for (std::uint32_t k=0; k<blocklen; ++k) {
                    s1 += static_cast<std::uint32_t>( data[j+k] );
                    s2 += s1;
                }
                s1 %= 65521u;
                s2 %= 65521u;
                j += blocklen;
                blocklen = 5552;
            }
            if (!out.push(static_cast<std::uint8_t>((s2 >> 8) & 0xFF))) goto fail;
            if (!out.push(static_cast<std::uint8_t>( s2       & 0xFF))) goto fail;
            if (!out.push(static_cast<std::uint8_t>((s1 >> 8) & 0xFF))) goto fail;
            if (!out.push(static_cast<std::uint8_t>( s1       & 0xFF))) goto fail;
        }

        *out_len = out.n;
        return out.release();

    fail:
        if (buckets) {
            for (std::uint32_t k=0; k < ZHASH; ++k) buckets[k].free();
            STBIW_free(buckets);
        }
        out.free();
        return nullptr;
    }

    static inline unsigned char* zlib_compress(unsigned char* data, int data_len, int* out_len, int quality) noexcept {
#ifdef STBIW_zlib_compress
        // user provided a zlib compress implementation, use that
        return STBIW_zlib_compress(data, data_len, out_len, quality);
#else // use builtin
        return zlib_compress_builtin(
                 static_cast<std::uint8_t*>(data), 
                 static_cast<std::uint32_t>(data_len),
            reinterpret_cast<std::uint32_t*>(out_len),
                 static_cast<std::uint32_t>(quality));
#endif
    }

    static inline void store_be32(std::uint8_t out[4], std::uint32_t v) noexcept {
        out[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        out[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        out[2] = static_cast<std::uint8_t>((v >> 8)  & 0xFFu);
        out[3] = static_cast<std::uint8_t>( v        & 0xFFu);
    }

} // namespace zlib
} // namespace stbiw