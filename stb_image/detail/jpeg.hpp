namespace stbi { namespace detail { namespace core {

//  "baseline" JPEG/JFIF decoder
//
//    simple implementation
//      - doesn't support delayed output of y-dimension
//      - simple interface (only one output format: 8-bit interleaved RGB)
//      - doesn't try to recover corrupt jpegs
//      - doesn't allow partial loading, loading multiple at once
//      - still fast on x86 (copying globals into locals doesn't help x86)
//      - allocates lots of intermediate memory (full size of all components)
//        - non-interleaved case requires this anyway
//        - allows good upsampling (see next)
//    high-quality
//      - upsampled channels are bilinearly interpolated, even across blocks
//      - quality integer IDCT derived from IJG's 'slow'
//    performance
//      - fast huffman; reasonable integer IDCT
//      - some SIMD kernels for common paths on targets with SSE2/NEON
//      - uses a lot of intermediate memory, could cache poorly

#ifndef STBI_NO_JPEG

// huffman decoding acceleration
#define FAST_BITS   9  // larger handles more cases; smaller stomps less cache

typedef struct
{
   uc  fast[1 << FAST_BITS];
   // weirdly, repacking this into AoS is a 10% speed loss, instead of a win
   uint16 code[256];
   uc  values[256];
   uc  size[257];
   unsigned int maxcode[18];
   int    delta[17];   // old 'firstsymbol' - old 'firstcode'
} huffman;

typedef struct
{
   context *s;
   huffman huff_dc[4];
   huffman huff_ac[4];
   uint16 dequant[4][64];
   int16 fast_ac[4][1 << FAST_BITS];

// sizes for components, interleaved MCUs
   int h_max, v_max;
   int mcu_x, mcu_y;
   int mcu_w, mcu_h;

// definition of jpeg image component
   struct
   {
      int id;
      int h,v;
      int tq;
      int hd,ha;
      int dc_pred;

      int x,y,w2,h2;
      uc *data;
      void *raw_data, *raw_coeff;
      uc *linebuf;
      short   *coeff;   // progressive only
      int      coeff_w, coeff_h; // number of 8x8 coefficient blocks
   } comp[4];

   uint32   code_buffer; // jpeg entropy-coded buffer
   int            code_bits;   // number of valid bits
   unsigned char  marker;      // marker seen while filling entropy buffer
   int            nomore;      // flag if we saw a marker so must stop

   int            progressive;
   int            spec_start;
   int            spec_end;
   int            succ_high;
   int            succ_low;
   int            eob_run;
   int            jfif;
   int            app14_color_transform; // Adobe APP14 tag
   int            rgb;

   int scan_n, order[4];
   int restart_interval, todo;

// kernels
   void (*idct_block_kernel)(uc *out, int out_stride, short data[64]);
   void (*YCbCr_to_RGB_kernel)(uc *out, const uc *y, const uc *pcb, const uc *pcr, int count, int step);
   uc *(*resample_row_hv_2_kernel)(uc *out, uc *in_near, uc *in_far, int w, int hs);
} jpeg;

static int build_huffman(huffman *h, int *count) noexcept
{
   int i,j,k=0;
   unsigned int code;
   // build size list for each symbol (from JPEG spec)
   for (i=0; i < 16; ++i) {
      for (j=0; j < count[i]; ++j) {
         h->size[k++] = (uc) (i+1);
         if(k >= 257) return err("bad size list","Corrupt JPEG");
      }
   }
   h->size[k] = 0;

   // compute actual symbols (from jpeg spec)
   code = 0;
   k = 0;
   for(j=1; j <= 16; ++j) {
      // compute delta to add to code to compute symbol id
      h->delta[j] = k - code;
      if (h->size[k] == j) {
         while (h->size[k] == j)
            h->code[k++] = (uint16) (code++);
         if (code-1 >= (1u << j)) return err("bad code lengths","Corrupt JPEG");
      }
      // compute largest code + 1 for this size, preshifted as needed later
      h->maxcode[j] = code << (16-j);
      code <<= 1;
   }
   h->maxcode[j] = 0xffffffff;

   // build non-spec acceleration table; 255 is flag for not-accelerated
   memset(h->fast, 255, 1 << FAST_BITS);
   for (i=0; i < k; ++i) {
      int s = h->size[i];
      if (s <= FAST_BITS) {
         int c = h->code[i] << (FAST_BITS-s);
         int m = 1 << (FAST_BITS-s);
         for (j=0; j < m; ++j) {
            h->fast[c+j] = (uc) i;
         }
      }
   }
   return 1;
}

// build a table that decodes both magnitude and value of small ACs in
// one go.
static void build_fast_ac(int16 *fast_ac, huffman *h) noexcept
{
   int i;
   for (i=0; i < (1 << FAST_BITS); ++i) {
      uc fast = h->fast[i];
      fast_ac[i] = 0;
      if (fast < 255) {
         int rs = h->values[fast];
         int run = (rs >> 4) & 15;
         int magbits = rs & 15;
         int len = h->size[fast];

         if (magbits && len + magbits <= FAST_BITS) {
            // magnitude code followed by receive_extend code
            int k = ((i << len) & ((1 << FAST_BITS) - 1)) >> (FAST_BITS - magbits);
            int m = 1 << (magbits - 1);
            if (k < m) k += (~0U << magbits) + 1;
            // if the result is small enough, we can fit it in fast_ac table
            if (k >= -128 && k <= 127)
               fast_ac[i] = (int16) ((k * 256) + (run * 16) + (len + magbits));
         }
      }
   }
}

static void grow_buffer_unsafe(jpeg *j) noexcept
{
   do {
      unsigned int b = j->nomore ? 0 : get8(j->s);
      if (b == 0xff) {
         int c = get8(j->s);
         while (c == 0xff) c = get8(j->s); // consume fill bytes
         if (c != 0) {
            j->marker = (unsigned char) c;
            j->nomore = 1;
            return;
         }
      }
      j->code_buffer |= b << (24 - j->code_bits);
      j->code_bits += 8;
   } while (j->code_bits <= 24);
}

// (1 << n) - 1
static const uint32 bmask[17]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};

// decode a jpeg huffman value from the bitstream
inline static int jpeg_huff_decode(jpeg *j, huffman *h) noexcept
{
   unsigned int temp;
   int c,k;

   if (j->code_bits < 16) grow_buffer_unsafe(j);

   // look at the top FAST_BITS and determine what symbol ID it is,
   // if the code is <= FAST_BITS
   c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
   k = h->fast[c];
   if (k < 255) {
      int s = h->size[k];
      if (s > j->code_bits)
         return -1;
      j->code_buffer <<= s;
      j->code_bits -= s;
      return h->values[k];
   }

   // naive test is to shift the code_buffer down so k bits are
   // valid, then test against maxcode. To speed this up, we've
   // preshifted maxcode left so that it has (16-k) 0s at the
   // end; in other words, regardless of the number of bits, it
   // wants to be compared against something shifted to have 16;
   // that way we don't need to shift inside the loop.
   temp = j->code_buffer >> 16;
   for (k=FAST_BITS+1 ; ; ++k)
      if (temp < h->maxcode[k])
         break;
   if (k == 17) {
      // error! code not found
      j->code_bits -= 16;
      return -1;
   }

   if (k > j->code_bits)
      return -1;

   // convert the huffman code to the symbol id
   c = ((j->code_buffer >> (32 - k)) & bmask[k]) + h->delta[k];
   if(c < 0 || c >= 256) // symbol id out of bounds!
       return -1;
   STBI_ASSERT((((j->code_buffer) >> (32 - h->size[c])) & bmask[h->size[c]]) == h->code[c]);

   // convert the id to a symbol
   j->code_bits -= k;
   j->code_buffer <<= k;
   return h->values[c];
}

// bias[n] = (-1<<n) + 1
static const int jbias[16] = {0,-1,-3,-7,-15,-31,-63,-127,-255,-511,-1023,-2047,-4095,-8191,-16383,-32767};

// combined JPEG 'receive' and JPEG 'extend', since baseline
// always extends everything it receives.
inline static int extend_receive(jpeg *j, int n) noexcept
{
   unsigned int k;
   int sgn;
   if (j->code_bits < n) grow_buffer_unsafe(j);
   if (j->code_bits < n) return 0; // ran out of bits from stream, return 0s intead of continuing

   sgn = j->code_buffer >> 31; // sign bit always in MSB; 0 if MSB clear (positive), 1 if MSB set (negative)
   k = lrot(j->code_buffer, n);
   j->code_buffer = k & ~bmask[n];
   k &= bmask[n];
   j->code_bits -= n;
   return k + (jbias[n] & (sgn - 1));
}

// get some unsigned bits
inline static int jpeg_get_bits(jpeg *j, int n) noexcept
{
   unsigned int k;
   if (j->code_bits < n) grow_buffer_unsafe(j);
   if (j->code_bits < n) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = lrot(j->code_buffer, n);
   j->code_buffer = k & ~bmask[n];
   k &= bmask[n];
   j->code_bits -= n;
   return k;
}

inline static int jpeg_get_bit(jpeg *j) noexcept
{
   unsigned int k;
   if (j->code_bits < 1) grow_buffer_unsafe(j);
   if (j->code_bits < 1) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = j->code_buffer;
   j->code_buffer <<= 1;
   --j->code_bits;
   return k & 0x80000000;
}

// given a value that's at position X in the zigzag stream,
// where does it appear in the 8x8 matrix coded as row-major?
static const uc jpeg_dezigzag[64+15] =
{
    0,  1,  8, 16,  9,  2,  3, 10,
   17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34,
   27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36,
   29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46,
   53, 60, 61, 54, 47, 55, 62, 63,
   // let corrupt input sample past end
   63, 63, 63, 63, 63, 63, 63, 63,
   63, 63, 63, 63, 63, 63, 63
};

// decode one 64-entry block--
static int jpeg_decode_block(jpeg *j, short data[64], huffman *hdc, huffman *hac, int16 *fac, int b, uint16 *dequant) noexcept
{
   int diff,dc,k;
   int t;

   if (j->code_bits < 16) grow_buffer_unsafe(j);
   t = jpeg_huff_decode(j, hdc);
   if (t < 0 || t > 15) return err("bad huffman code","Corrupt JPEG");

   // 0 all the ac values now so we can do it 32-bits at a time
   memset(data,0,64*sizeof(data[0]));

   diff = t ? extend_receive(j, t) : 0;
   if (!addints_valid(j->comp[b].dc_pred, diff)) return err("bad delta","Corrupt JPEG");
   dc = j->comp[b].dc_pred + diff;
   j->comp[b].dc_pred = dc;
   if (!mul2shorts_valid(dc, dequant[0])) return err("can't merge dc and ac", "Corrupt JPEG");
   data[0] = (short) (dc * dequant[0]);

   // decode AC components, see JPEG spec
   k = 1;
   do {
      unsigned int zig;
      int c,r,s;
      if (j->code_bits < 16) grow_buffer_unsafe(j);
      c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
      r = fac[c];
      if (r) { // fast-AC path
         k += (r >> 4) & 15; // run
         s = r & 15; // combined length
         if (s > j->code_bits) return err("bad huffman code", "Combined length longer than code bits available");
         j->code_buffer <<= s;
         j->code_bits -= s;
         // decode into unzigzag'd location
         zig = jpeg_dezigzag[k++];
         data[zig] = (short) ((r >> 8) * dequant[zig]);
      } else {
         int rs = jpeg_huff_decode(j, hac);
         if (rs < 0) return err("bad huffman code","Corrupt JPEG");
         s = rs & 15;
         r = rs >> 4;
         if (s == 0) {
            if (rs != 0xf0) break; // end block
            k += 16;
         } else {
            k += r;
            // decode into unzigzag'd location
            zig = jpeg_dezigzag[k++];
            data[zig] = (short) (extend_receive(j,s) * dequant[zig]);
         }
      }
   } while (k < 64);
   return 1;
}

static int jpeg_decode_block_prog_dc(jpeg *j, short data[64], huffman *hdc, int b) noexcept
{
   int diff,dc;
   int t;
   if (j->spec_end != 0) return err("can't merge dc and ac", "Corrupt JPEG");

   if (j->code_bits < 16) grow_buffer_unsafe(j);

   if (j->succ_high == 0) {
      // first scan for DC coefficient, must be first
      memset(data,0,64*sizeof(data[0])); // 0 all the ac values now
      t = jpeg_huff_decode(j, hdc);
      if (t < 0 || t > 15) return err("can't merge dc and ac", "Corrupt JPEG");
      diff = t ? extend_receive(j, t) : 0;

      if (!addints_valid(j->comp[b].dc_pred, diff)) return err("bad delta", "Corrupt JPEG");
      dc = j->comp[b].dc_pred + diff;
      j->comp[b].dc_pred = dc;
      if (!mul2shorts_valid(dc, 1 << j->succ_low)) return err("can't merge dc and ac", "Corrupt JPEG");
      data[0] = (short) (dc * (1 << j->succ_low));
   } else {
      // refinement scan for DC coefficient
      if (jpeg_get_bit(j))
         data[0] += (short) (1 << j->succ_low);
   }
   return 1;
}

// @OPTIMIZE: store non-zigzagged during the decode passes,
// and only de-zigzag when dequantizing
static int jpeg_decode_block_prog_ac(jpeg *j, short data[64], huffman *hac, int16 *fac) noexcept
{
   int k;
   if (j->spec_start == 0) return err("can't merge dc and ac", "Corrupt JPEG");

   if (j->succ_high == 0) {
      int shift = j->succ_low;

      if (j->eob_run) {
         --j->eob_run;
         return 1;
      }

      k = j->spec_start;
      do {
         unsigned int zig;
         int c,r,s;
         if (j->code_bits < 16) grow_buffer_unsafe(j);
         c = (j->code_buffer >> (32 - FAST_BITS)) & ((1 << FAST_BITS)-1);
         r = fac[c];
         if (r) { // fast-AC path
            k += (r >> 4) & 15; // run
            s = r & 15; // combined length
            if (s > j->code_bits) return err("bad huffman code", "Combined length longer than code bits available");
            j->code_buffer <<= s;
            j->code_bits -= s;
            zig = jpeg_dezigzag[k++];
            data[zig] = (short) ((r >> 8) * (1 << shift));
         } else {
            int rs = jpeg_huff_decode(j, hac);
            if (rs < 0) return err("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  j->eob_run = (1 << r);
                  if (r)
                     j->eob_run += jpeg_get_bits(j, r);
                  --j->eob_run;
                  break;
               }
               k += 16;
            } else {
               k += r;
               zig = jpeg_dezigzag[k++];
               data[zig] = (short) (extend_receive(j,s) * (1 << shift));
            }
         }
      } while (k <= j->spec_end);
   } else {
      // refinement scan for these AC coefficients

      short bit = (short) (1 << j->succ_low);

      if (j->eob_run) {
         --j->eob_run;
         for (k = j->spec_start; k <= j->spec_end; ++k) {
            short *p = &data[jpeg_dezigzag[k]];
            if (*p != 0)
               if (jpeg_get_bit(j))
                  if ((*p & bit)==0) {
                     if (*p > 0)
                        *p += bit;
                     else
                        *p -= bit;
                  }
         }
      } else {
         k = j->spec_start;
         do {
            int r,s;
            int rs = jpeg_huff_decode(j, hac); // @OPTIMIZE see if we can use the fast path here, advance-by-r is so slow, eh
            if (rs < 0) return err("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  j->eob_run = (1 << r) - 1;
                  if (r)
                     j->eob_run += jpeg_get_bits(j, r);
                  r = 64; // force end of block
               } else {
                  // r=15 s=0 should write 16 0s, so we just do
                  // a run of 15 0s and then write s (which is 0),
                  // so we don't have to do anything special here
               }
            } else {
               if (s != 1) return err("bad huffman code", "Corrupt JPEG");
               // sign bit
               if (jpeg_get_bit(j))
                  s = bit;
               else
                  s = -bit;
            }

            // advance by r
            while (k <= j->spec_end) {
               short *p = &data[jpeg_dezigzag[k++]];
               if (*p != 0) {
                  if (jpeg_get_bit(j))
                     if ((*p & bit)==0) {
                        if (*p > 0)
                           *p += bit;
                        else
                           *p -= bit;
                     }
               } else {
                  if (r == 0) {
                     *p = (short) s;
                     break;
                  }
                  --r;
               }
            }
         } while (k <= j->spec_end);
      }
   }
   return 1;
}

// take a -128..127 value and clamp it and convert to 0..255
inline static uc clamp(int x) noexcept
{
   // trick to use a single test to catch both cases
   if ((unsigned int) x > 255) {
      if (x < 0) return 0;
      if (x > 255) return 255;
   }
   return (uc) x;
}

#define f2f(x)  ((int) (((x) * 4096 + 0.5)))
#define fsh(x)  ((x) * 4096)

// derived from jidctint -- DCT_ISLOW
#define STBI__IDCT_1D(s0,s1,s2,s3,s4,s5,s6,s7) \
   int t0,t1,t2,t3,p1,p2,p3,p4,p5,x0,x1,x2,x3; \
   p2 = s2;                                    \
   p3 = s6;                                    \
   p1 = (p2+p3) * f2f(0.5411961f);       \
   t2 = p1 + p3*f2f(-1.847759065f);      \
   t3 = p1 + p2*f2f( 0.765366865f);      \
   p2 = s0;                                    \
   p3 = s4;                                    \
   t0 = fsh(p2+p3);                      \
   t1 = fsh(p2-p3);                      \
   x0 = t0+t3;                                 \
   x3 = t0-t3;                                 \
   x1 = t1+t2;                                 \
   x2 = t1-t2;                                 \
   t0 = s7;                                    \
   t1 = s5;                                    \
   t2 = s3;                                    \
   t3 = s1;                                    \
   p3 = t0+t2;                                 \
   p4 = t1+t3;                                 \
   p1 = t0+t3;                                 \
   p2 = t1+t2;                                 \
   p5 = (p3+p4)*f2f( 1.175875602f);      \
   t0 = t0*f2f( 0.298631336f);           \
   t1 = t1*f2f( 2.053119869f);           \
   t2 = t2*f2f( 3.072711026f);           \
   t3 = t3*f2f( 1.501321110f);           \
   p1 = p5 + p1*f2f(-0.899976223f);      \
   p2 = p5 + p2*f2f(-2.562915447f);      \
   p3 = p3*f2f(-1.961570560f);           \
   p4 = p4*f2f(-0.390180644f);           \
   t3 += p1+p4;                                \
   t2 += p2+p3;                                \
   t1 += p2+p4;                                \
   t0 += p1+p3;

static void idct_block(uc *out, int out_stride, short data[64]) noexcept
{
   int i,val[64],*v=val;
   uc *o;
   short *d = data;

   // columns
   for (i=0; i < 8; ++i,++d, ++v) {
      // if all zeroes, shortcut -- this avoids dequantizing 0s and IDCTing
      if (d[ 8]==0 && d[16]==0 && d[24]==0 && d[32]==0
           && d[40]==0 && d[48]==0 && d[56]==0) {
         //    no shortcut                 0     seconds
         //    (1|2|3|4|5|6|7)==0          0     seconds
         //    all separate               -0.047 seconds
         //    1 && 2|3 && 4|5 && 6|7:    -0.047 seconds
         int dcterm = d[0]*4;
         v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dcterm;
      } else {
         STBI__IDCT_1D(d[ 0],d[ 8],d[16],d[24],d[32],d[40],d[48],d[56])
         // constants scaled things up by 1<<12; let's bring them back
         // down, but keep 2 extra bits of precision
         x0 += 512; x1 += 512; x2 += 512; x3 += 512;
         v[ 0] = (x0+t3) >> 10;
         v[56] = (x0-t3) >> 10;
         v[ 8] = (x1+t2) >> 10;
         v[48] = (x1-t2) >> 10;
         v[16] = (x2+t1) >> 10;
         v[40] = (x2-t1) >> 10;
         v[24] = (x3+t0) >> 10;
         v[32] = (x3-t0) >> 10;
      }
   }

   for (i=0, v=val, o=out; i < 8; ++i,v+=8,o+=out_stride) {
      // no fast case since the first 1D IDCT spread components out
      STBI__IDCT_1D(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7])
      // constants scaled things up by 1<<12, plus we had 1<<2 from first
      // loop, plus horizontal and vertical each scale by sqrt(8) so together
      // we've got an extra 1<<3, so 1<<17 total we need to remove.
      // so we want to round that, which means adding 0.5 * 1<<17,
      // aka 65536. Also, we'll end up with -128 to 127 that we want
      // to encode as 0..255 by adding 128, so we'll add that before the shift
      x0 += 65536 + (128<<17);
      x1 += 65536 + (128<<17);
      x2 += 65536 + (128<<17);
      x3 += 65536 + (128<<17);
      // tried computing the shifts into temps, or'ing the temps to see
      // if any were out of range, but that was slower
      o[0] = clamp((x0+t3) >> 17);
      o[7] = clamp((x0-t3) >> 17);
      o[1] = clamp((x1+t2) >> 17);
      o[6] = clamp((x1-t2) >> 17);
      o[2] = clamp((x2+t1) >> 17);
      o[5] = clamp((x2-t1) >> 17);
      o[3] = clamp((x3+t0) >> 17);
      o[4] = clamp((x3-t0) >> 17);
   }
}

#ifdef STBI_SSE2
// sse2 integer IDCT. not the fastest possible implementation but it
// produces bit-identical results to the generic C version so it's
// fully "transparent".
static void idct_simd(uc *out, int out_stride, short data[64]) noexcept
{
   // This is constructed to match our regular (generic) integer IDCT exactly.
   __m128i row0, row1, row2, row3, row4, row5, row6, row7;
   __m128i tmp;

   // dot product constant: even elems=x, odd elems=y
   #define dct_const(x,y)  _mm_setr_epi16((x),(y),(x),(y),(x),(y),(x),(y))

   // out(0) = c0[even]*x + c0[odd]*y   (c0, x, y 16-bit, out 32-bit)
   // out(1) = c1[even]*x + c1[odd]*y
   #define dct_rot(out0,out1, x,y,c0,c1) \
      __m128i c0##lo = _mm_unpacklo_epi16((x),(y)); \
      __m128i c0##hi = _mm_unpackhi_epi16((x),(y)); \
      __m128i out0##_l = _mm_madd_epi16(c0##lo, c0); \
      __m128i out0##_h = _mm_madd_epi16(c0##hi, c0); \
      __m128i out1##_l = _mm_madd_epi16(c0##lo, c1); \
      __m128i out1##_h = _mm_madd_epi16(c0##hi, c1)

   // out = in << 12  (in 16-bit, out 32-bit)
   #define dct_widen(out, in) \
      __m128i out##_l = _mm_srai_epi32(_mm_unpacklo_epi16(_mm_setzero_si128(), (in)), 4); \
      __m128i out##_h = _mm_srai_epi32(_mm_unpackhi_epi16(_mm_setzero_si128(), (in)), 4)

   // wide add
   #define dct_wadd(out, a, b) \
      __m128i out##_l = _mm_add_epi32(a##_l, b##_l); \
      __m128i out##_h = _mm_add_epi32(a##_h, b##_h)

   // wide sub
   #define dct_wsub(out, a, b) \
      __m128i out##_l = _mm_sub_epi32(a##_l, b##_l); \
      __m128i out##_h = _mm_sub_epi32(a##_h, b##_h)

   // butterfly a/b, add bias, then shift by "s" and pack
   #define dct_bfly32o(out0, out1, a,b,bias,s) \
      { \
         __m128i abiased_l = _mm_add_epi32(a##_l, bias); \
         __m128i abiased_h = _mm_add_epi32(a##_h, bias); \
         dct_wadd(sum, abiased, b); \
         dct_wsub(dif, abiased, b); \
         out0 = _mm_packs_epi32(_mm_srai_epi32(sum_l, s), _mm_srai_epi32(sum_h, s)); \
         out1 = _mm_packs_epi32(_mm_srai_epi32(dif_l, s), _mm_srai_epi32(dif_h, s)); \
      }

   // 8-bit interleave step (for transposes)
   #define dct_interleave8(a, b) \
      tmp = a; \
      a = _mm_unpacklo_epi8(a, b); \
      b = _mm_unpackhi_epi8(tmp, b)

   // 16-bit interleave step (for transposes)
   #define dct_interleave16(a, b) \
      tmp = a; \
      a = _mm_unpacklo_epi16(a, b); \
      b = _mm_unpackhi_epi16(tmp, b)

   #define dct_pass(bias,shift) \
      { \
         /* even part */ \
         dct_rot(t2e,t3e, row2,row6, rot0_0,rot0_1); \
         __m128i sum04 = _mm_add_epi16(row0, row4); \
         __m128i dif04 = _mm_sub_epi16(row0, row4); \
         dct_widen(t0e, sum04); \
         dct_widen(t1e, dif04); \
         dct_wadd(x0, t0e, t3e); \
         dct_wsub(x3, t0e, t3e); \
         dct_wadd(x1, t1e, t2e); \
         dct_wsub(x2, t1e, t2e); \
         /* odd part */ \
         dct_rot(y0o,y2o, row7,row3, rot2_0,rot2_1); \
         dct_rot(y1o,y3o, row5,row1, rot3_0,rot3_1); \
         __m128i sum17 = _mm_add_epi16(row1, row7); \
         __m128i sum35 = _mm_add_epi16(row3, row5); \
         dct_rot(y4o,y5o, sum17,sum35, rot1_0,rot1_1); \
         dct_wadd(x4, y0o, y4o); \
         dct_wadd(x5, y1o, y5o); \
         dct_wadd(x6, y2o, y5o); \
         dct_wadd(x7, y3o, y4o); \
         dct_bfly32o(row0,row7, x0,x7,bias,shift); \
         dct_bfly32o(row1,row6, x1,x6,bias,shift); \
         dct_bfly32o(row2,row5, x2,x5,bias,shift); \
         dct_bfly32o(row3,row4, x3,x4,bias,shift); \
      }

   __m128i rot0_0 = dct_const(f2f(0.5411961f), f2f(0.5411961f) + f2f(-1.847759065f));
   __m128i rot0_1 = dct_const(f2f(0.5411961f) + f2f( 0.765366865f), f2f(0.5411961f));
   __m128i rot1_0 = dct_const(f2f(1.175875602f) + f2f(-0.899976223f), f2f(1.175875602f));
   __m128i rot1_1 = dct_const(f2f(1.175875602f), f2f(1.175875602f) + f2f(-2.562915447f));
   __m128i rot2_0 = dct_const(f2f(-1.961570560f) + f2f( 0.298631336f), f2f(-1.961570560f));
   __m128i rot2_1 = dct_const(f2f(-1.961570560f), f2f(-1.961570560f) + f2f( 3.072711026f));
   __m128i rot3_0 = dct_const(f2f(-0.390180644f) + f2f( 2.053119869f), f2f(-0.390180644f));
   __m128i rot3_1 = dct_const(f2f(-0.390180644f), f2f(-0.390180644f) + f2f( 1.501321110f));

   // rounding biases in column/row passes, see idct_block for explanation.
   __m128i bias_0 = _mm_set1_epi32(512);
   __m128i bias_1 = _mm_set1_epi32(65536 + (128<<17));

   // load
   row0 = _mm_load_si128((const __m128i *) (data + 0*8));
   row1 = _mm_load_si128((const __m128i *) (data + 1*8));
   row2 = _mm_load_si128((const __m128i *) (data + 2*8));
   row3 = _mm_load_si128((const __m128i *) (data + 3*8));
   row4 = _mm_load_si128((const __m128i *) (data + 4*8));
   row5 = _mm_load_si128((const __m128i *) (data + 5*8));
   row6 = _mm_load_si128((const __m128i *) (data + 6*8));
   row7 = _mm_load_si128((const __m128i *) (data + 7*8));

   // column pass
   dct_pass(bias_0, 10);

   {
      // 16bit 8x8 transpose pass 1
      dct_interleave16(row0, row4);
      dct_interleave16(row1, row5);
      dct_interleave16(row2, row6);
      dct_interleave16(row3, row7);

      // transpose pass 2
      dct_interleave16(row0, row2);
      dct_interleave16(row1, row3);
      dct_interleave16(row4, row6);
      dct_interleave16(row5, row7);

      // transpose pass 3
      dct_interleave16(row0, row1);
      dct_interleave16(row2, row3);
      dct_interleave16(row4, row5);
      dct_interleave16(row6, row7);
   }

   // row pass
   dct_pass(bias_1, 17);

   {
      // pack
      __m128i p0 = _mm_packus_epi16(row0, row1); // a0a1a2a3...a7b0b1b2b3...b7
      __m128i p1 = _mm_packus_epi16(row2, row3);
      __m128i p2 = _mm_packus_epi16(row4, row5);
      __m128i p3 = _mm_packus_epi16(row6, row7);

      // 8bit 8x8 transpose pass 1
      dct_interleave8(p0, p2); // a0e0a1e1...
      dct_interleave8(p1, p3); // c0g0c1g1...

      // transpose pass 2
      dct_interleave8(p0, p1); // a0c0e0g0...
      dct_interleave8(p2, p3); // b0d0f0h0...

      // transpose pass 3
      dct_interleave8(p0, p2); // a0b0c0d0...
      dct_interleave8(p1, p3); // a4b4c4d4...

      // store
      _mm_storel_epi64((__m128i *) out, p0); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p0, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p2); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p2, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p1); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p1, 0x4e)); out += out_stride;
      _mm_storel_epi64((__m128i *) out, p3); out += out_stride;
      _mm_storel_epi64((__m128i *) out, _mm_shuffle_epi32(p3, 0x4e));
   }

#undef dct_const
#undef dct_rot
#undef dct_widen
#undef dct_wadd
#undef dct_wsub
#undef dct_bfly32o
#undef dct_interleave8
#undef dct_interleave16
#undef dct_pass
}

#endif // STBI_SSE2

#ifdef STBI_NEON

// NEON integer IDCT. should produce bit-identical
// results to the generic C version.
static void idct_simd(uc *out, int out_stride, short data[64]) noexcept
{
   int16x8_t row0, row1, row2, row3, row4, row5, row6, row7;

   int16x4_t rot0_0 = vdup_n_s16(f2f(0.5411961f));
   int16x4_t rot0_1 = vdup_n_s16(f2f(-1.847759065f));
   int16x4_t rot0_2 = vdup_n_s16(f2f( 0.765366865f));
   int16x4_t rot1_0 = vdup_n_s16(f2f( 1.175875602f));
   int16x4_t rot1_1 = vdup_n_s16(f2f(-0.899976223f));
   int16x4_t rot1_2 = vdup_n_s16(f2f(-2.562915447f));
   int16x4_t rot2_0 = vdup_n_s16(f2f(-1.961570560f));
   int16x4_t rot2_1 = vdup_n_s16(f2f(-0.390180644f));
   int16x4_t rot3_0 = vdup_n_s16(f2f( 0.298631336f));
   int16x4_t rot3_1 = vdup_n_s16(f2f( 2.053119869f));
   int16x4_t rot3_2 = vdup_n_s16(f2f( 3.072711026f));
   int16x4_t rot3_3 = vdup_n_s16(f2f( 1.501321110f));

#define dct_long_mul(out, inq, coeff) \
   int32x4_t out##_l = vmull_s16(vget_low_s16(inq), coeff); \
   int32x4_t out##_h = vmull_s16(vget_high_s16(inq), coeff)

#define dct_long_mac(out, acc, inq, coeff) \
   int32x4_t out##_l = vmlal_s16(acc##_l, vget_low_s16(inq), coeff); \
   int32x4_t out##_h = vmlal_s16(acc##_h, vget_high_s16(inq), coeff)

#define dct_widen(out, inq) \
   int32x4_t out##_l = vshll_n_s16(vget_low_s16(inq), 12); \
   int32x4_t out##_h = vshll_n_s16(vget_high_s16(inq), 12)

// wide add
#define dct_wadd(out, a, b) \
   int32x4_t out##_l = vaddq_s32(a##_l, b##_l); \
   int32x4_t out##_h = vaddq_s32(a##_h, b##_h)

// wide sub
#define dct_wsub(out, a, b) \
   int32x4_t out##_l = vsubq_s32(a##_l, b##_l); \
   int32x4_t out##_h = vsubq_s32(a##_h, b##_h)

// butterfly a/b, then shift using "shiftop" by "s" and pack
#define dct_bfly32o(out0,out1, a,b,shiftop,s) \
   { \
      dct_wadd(sum, a, b); \
      dct_wsub(dif, a, b); \
      out0 = vcombine_s16(shiftop(sum_l, s), shiftop(sum_h, s)); \
      out1 = vcombine_s16(shiftop(dif_l, s), shiftop(dif_h, s)); \
   }

#define dct_pass(shiftop, shift) \
   { \
      /* even part */ \
      int16x8_t sum26 = vaddq_s16(row2, row6); \
      dct_long_mul(p1e, sum26, rot0_0); \
      dct_long_mac(t2e, p1e, row6, rot0_1); \
      dct_long_mac(t3e, p1e, row2, rot0_2); \
      int16x8_t sum04 = vaddq_s16(row0, row4); \
      int16x8_t dif04 = vsubq_s16(row0, row4); \
      dct_widen(t0e, sum04); \
      dct_widen(t1e, dif04); \
      dct_wadd(x0, t0e, t3e); \
      dct_wsub(x3, t0e, t3e); \
      dct_wadd(x1, t1e, t2e); \
      dct_wsub(x2, t1e, t2e); \
      /* odd part */ \
      int16x8_t sum15 = vaddq_s16(row1, row5); \
      int16x8_t sum17 = vaddq_s16(row1, row7); \
      int16x8_t sum35 = vaddq_s16(row3, row5); \
      int16x8_t sum37 = vaddq_s16(row3, row7); \
      int16x8_t sumodd = vaddq_s16(sum17, sum35); \
      dct_long_mul(p5o, sumodd, rot1_0); \
      dct_long_mac(p1o, p5o, sum17, rot1_1); \
      dct_long_mac(p2o, p5o, sum35, rot1_2); \
      dct_long_mul(p3o, sum37, rot2_0); \
      dct_long_mul(p4o, sum15, rot2_1); \
      dct_wadd(sump13o, p1o, p3o); \
      dct_wadd(sump24o, p2o, p4o); \
      dct_wadd(sump23o, p2o, p3o); \
      dct_wadd(sump14o, p1o, p4o); \
      dct_long_mac(x4, sump13o, row7, rot3_0); \
      dct_long_mac(x5, sump24o, row5, rot3_1); \
      dct_long_mac(x6, sump23o, row3, rot3_2); \
      dct_long_mac(x7, sump14o, row1, rot3_3); \
      dct_bfly32o(row0,row7, x0,x7,shiftop,shift); \
      dct_bfly32o(row1,row6, x1,x6,shiftop,shift); \
      dct_bfly32o(row2,row5, x2,x5,shiftop,shift); \
      dct_bfly32o(row3,row4, x3,x4,shiftop,shift); \
   }

   // load
   row0 = vld1q_s16(data + 0*8);
   row1 = vld1q_s16(data + 1*8);
   row2 = vld1q_s16(data + 2*8);
   row3 = vld1q_s16(data + 3*8);
   row4 = vld1q_s16(data + 4*8);
   row5 = vld1q_s16(data + 5*8);
   row6 = vld1q_s16(data + 6*8);
   row7 = vld1q_s16(data + 7*8);

   // add DC bias
   row0 = vaddq_s16(row0, vsetq_lane_s16(1024, vdupq_n_s16(0), 0));

   // column pass
   dct_pass(vrshrn_n_s32, 10);

   // 16bit 8x8 transpose
   {
// these three map to a single VTRN.16, VTRN.32, and VSWP, respectively.
// whether compilers actually get this is another story, sadly.
#define dct_trn16(x, y) { int16x8x2_t t = vtrnq_s16(x, y); x = t.val[0]; y = t.val[1]; }
#define dct_trn32(x, y) { int32x4x2_t t = vtrnq_s32(vreinterpretq_s32_s16(x), vreinterpretq_s32_s16(y)); x = vreinterpretq_s16_s32(t.val[0]); y = vreinterpretq_s16_s32(t.val[1]); }
#define dct_trn64(x, y) { int16x8_t x0 = x; int16x8_t y0 = y; x = vcombine_s16(vget_low_s16(x0), vget_low_s16(y0)); y = vcombine_s16(vget_high_s16(x0), vget_high_s16(y0)); }

      // pass 1
      dct_trn16(row0, row1); // a0b0a2b2a4b4a6b6
      dct_trn16(row2, row3);
      dct_trn16(row4, row5);
      dct_trn16(row6, row7);

      // pass 2
      dct_trn32(row0, row2); // a0b0c0d0a4b4c4d4
      dct_trn32(row1, row3);
      dct_trn32(row4, row6);
      dct_trn32(row5, row7);

      // pass 3
      dct_trn64(row0, row4); // a0b0c0d0e0f0g0h0
      dct_trn64(row1, row5);
      dct_trn64(row2, row6);
      dct_trn64(row3, row7);

#undef dct_trn16
#undef dct_trn32
#undef dct_trn64
   }

   // row pass
   // vrshrn_n_s32 only supports shifts up to 16, we need
   // 17. so do a non-rounding shift of 16 first then follow
   // up with a rounding shift by 1.
   dct_pass(vshrn_n_s32, 16);

   {
      // pack and round
      uint8x8_t p0 = vqrshrun_n_s16(row0, 1);
      uint8x8_t p1 = vqrshrun_n_s16(row1, 1);
      uint8x8_t p2 = vqrshrun_n_s16(row2, 1);
      uint8x8_t p3 = vqrshrun_n_s16(row3, 1);
      uint8x8_t p4 = vqrshrun_n_s16(row4, 1);
      uint8x8_t p5 = vqrshrun_n_s16(row5, 1);
      uint8x8_t p6 = vqrshrun_n_s16(row6, 1);
      uint8x8_t p7 = vqrshrun_n_s16(row7, 1);

      // again, these can translate into one instruction, but often don't.
#define dct_trn8_8(x, y) { uint8x8x2_t t = vtrn_u8(x, y); x = t.val[0]; y = t.val[1]; }
#define dct_trn8_16(x, y) { uint16x4x2_t t = vtrn_u16(vreinterpret_u16_u8(x), vreinterpret_u16_u8(y)); x = vreinterpret_u8_u16(t.val[0]); y = vreinterpret_u8_u16(t.val[1]); }
#define dct_trn8_32(x, y) { uint32x2x2_t t = vtrn_u32(vreinterpret_u32_u8(x), vreinterpret_u32_u8(y)); x = vreinterpret_u8_u32(t.val[0]); y = vreinterpret_u8_u32(t.val[1]); }

      // sadly can't use interleaved stores here since we only write
      // 8 bytes to each scan line!

      // 8x8 8-bit transpose pass 1
      dct_trn8_8(p0, p1);
      dct_trn8_8(p2, p3);
      dct_trn8_8(p4, p5);
      dct_trn8_8(p6, p7);

      // pass 2
      dct_trn8_16(p0, p2);
      dct_trn8_16(p1, p3);
      dct_trn8_16(p4, p6);
      dct_trn8_16(p5, p7);

      // pass 3
      dct_trn8_32(p0, p4);
      dct_trn8_32(p1, p5);
      dct_trn8_32(p2, p6);
      dct_trn8_32(p3, p7);

      // store
      vst1_u8(out, p0); out += out_stride;
      vst1_u8(out, p1); out += out_stride;
      vst1_u8(out, p2); out += out_stride;
      vst1_u8(out, p3); out += out_stride;
      vst1_u8(out, p4); out += out_stride;
      vst1_u8(out, p5); out += out_stride;
      vst1_u8(out, p6); out += out_stride;
      vst1_u8(out, p7);

#undef dct_trn8_8
#undef dct_trn8_16
#undef dct_trn8_32
   }

#undef dct_long_mul
#undef dct_long_mac
#undef dct_widen
#undef dct_wadd
#undef dct_wsub
#undef dct_bfly32o
#undef dct_pass
}

#endif // STBI_NEON

#define STBI__MARKER_none  0xff
// if there's a pending marker from the entropy stream, return that
// otherwise, fetch from the stream and get a marker. if there's no
// marker, return 0xff, which is never a valid marker value
static uc get_marker(jpeg *j) noexcept
{
   uc x;
   if (j->marker != STBI__MARKER_none) { x = j->marker; j->marker = STBI__MARKER_none; return x; }
   x = get8(j->s);
   if (x != 0xff) return STBI__MARKER_none;
   while (x == 0xff)
      x = get8(j->s); // consume repeated 0xff fill bytes
   return x;
}

// in each scan, we'll have scan_n components, and the order
// of the components is specified by order[]
#define STBI__RESTART(x)     ((x) >= 0xd0 && (x) <= 0xd7)

// after a restart interval, jpeg_reset the entropy decoder and
// the dc prediction
static void jpeg_reset(jpeg *j) noexcept
{
   j->code_bits = 0;
   j->code_buffer = 0;
   j->nomore = 0;
   j->comp[0].dc_pred = j->comp[1].dc_pred = j->comp[2].dc_pred = j->comp[3].dc_pred = 0;
   j->marker = STBI__MARKER_none;
   j->todo = j->restart_interval ? j->restart_interval : 0x7fffffff;
   j->eob_run = 0;
   // no more than 1<<31 MCUs if no restart_interal? that's plenty safe,
   // since we don't even allow 1<<30 pixels
}

static int parse_entropy_coded_data(jpeg *z) noexcept
{
   jpeg_reset(z);
   if (!z->progressive) {
      if (z->scan_n == 1) {
         int i,j;
         STBI_SIMD_ALIGN(short, data[64]);
         int n = z->order[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (z->comp[n].x+7) >> 3;
         int h = (z->comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               int ha = z->comp[n].ha;
               if (!jpeg_decode_block(z, data, z->huff_dc+z->comp[n].hd, z->huff_ac+ha, z->fast_ac[ha], n, z->dequant[z->comp[n].tq])) return 0;
               z->idct_block_kernel(z->comp[n].data+z->comp[n].w2*j*8+i*8, z->comp[n].w2, data);
               // every data block is an MCU, so countdown the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) grow_buffer_unsafe(z);
                  // if it's NOT a restart, then just bail, so we get corrupt data
                  // rather than no data
                  if (!STBI__RESTART(z->marker)) return 1;
                  jpeg_reset(z);
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         STBI_SIMD_ALIGN(short, data[64]);
         for (j=0; j < z->mcu_y; ++j) {
            for (i=0; i < z->mcu_x; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < z->scan_n; ++k) {
                  int n = z->order[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < z->comp[n].v; ++y) {
                     for (x=0; x < z->comp[n].h; ++x) {
                        int x2 = (i*z->comp[n].h + x)*8;
                        int y2 = (j*z->comp[n].v + y)*8;
                        int ha = z->comp[n].ha;
                        if (!jpeg_decode_block(z, data, z->huff_dc+z->comp[n].hd, z->huff_ac+ha, z->fast_ac[ha], n, z->dequant[z->comp[n].tq])) return 0;
                        z->idct_block_kernel(z->comp[n].data+z->comp[n].w2*y2+x2, z->comp[n].w2, data);
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  jpeg_reset(z);
               }
            }
         }
         return 1;
      }
   } else {
      if (z->scan_n == 1) {
         int i,j;
         int n = z->order[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (z->comp[n].x+7) >> 3;
         int h = (z->comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = z->comp[n].coeff + 64 * (i + j * z->comp[n].coeff_w);
               if (z->spec_start == 0) {
                  if (!jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->comp[n].hd], n))
                     return 0;
               } else {
                  int ha = z->comp[n].ha;
                  if (!jpeg_decode_block_prog_ac(z, data, &z->huff_ac[ha], z->fast_ac[ha]))
                     return 0;
               }
               // every data block is an MCU, so countdown the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  jpeg_reset(z);
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         for (j=0; j < z->mcu_y; ++j) {
            for (i=0; i < z->mcu_x; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < z->scan_n; ++k) {
                  int n = z->order[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < z->comp[n].v; ++y) {
                     for (x=0; x < z->comp[n].h; ++x) {
                        int x2 = (i*z->comp[n].h + x);
                        int y2 = (j*z->comp[n].v + y);
                        short *data = z->comp[n].coeff + 64 * (x2 + y2 * z->comp[n].coeff_w);
                        if (!jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->comp[n].hd], n))
                           return 0;
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--z->todo <= 0) {
                  if (z->code_bits < 24) grow_buffer_unsafe(z);
                  if (!STBI__RESTART(z->marker)) return 1;
                  jpeg_reset(z);
               }
            }
         }
         return 1;
      }
   }
}

static void jpeg_dequantize(short *data, uint16 *dequant) noexcept
{
   int i;
   for (i=0; i < 64; ++i)
      data[i] *= dequant[i];
}

static void jpeg_finish(jpeg *z) noexcept
{
   if (z->progressive) {
      // dequantize and idct the data
      int i,j,n;
      for (n=0; n < z->s->n; ++n) {
         int w = (z->comp[n].x+7) >> 3;
         int h = (z->comp[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = z->comp[n].coeff + 64 * (i + j * z->comp[n].coeff_w);
               jpeg_dequantize(data, z->dequant[z->comp[n].tq]);
               z->idct_block_kernel(z->comp[n].data+z->comp[n].w2*j*8+i*8, z->comp[n].w2, data);
            }
         }
      }
   }
}

static int process_marker(jpeg *z, int m) noexcept
{
   int L;
   switch (m) {
      case STBI__MARKER_none: // no marker found
         return err("expected marker","Corrupt JPEG");

      case 0xDD: // DRI - specify restart interval
         if (get16be(z->s) != 4) return err("bad DRI len","Corrupt JPEG");
         z->restart_interval = get16be(z->s);
         return 1;

      case 0xDB: // DQT - define quantization table
         L = get16be(z->s)-2;
         while (L > 0) {
            int q = get8(z->s);
            int p = q >> 4, sixteen = (p != 0);
            int t = q & 15,i;
            if (p != 0 && p != 1) return err("bad DQT type","Corrupt JPEG");
            if (t > 3) return err("bad DQT table","Corrupt JPEG");

            for (i=0; i < 64; ++i)
               z->dequant[t][jpeg_dezigzag[i]] = (uint16)(sixteen ? get16be(z->s) : get8(z->s));
            L -= (sixteen ? 129 : 65);
         }
         return L==0;

      case 0xC4: // DHT - define huffman table
         L = get16be(z->s)-2;
         while (L > 0) {
            uc *v;
            int sizes[16],i,n=0;
            int q = get8(z->s);
            int tc = q >> 4;
            int th = q & 15;
            if (tc > 1 || th > 3) return err("bad DHT header","Corrupt JPEG");
            for (i=0; i < 16; ++i) {
               sizes[i] = get8(z->s);
               n += sizes[i];
            }
            if(n > 256) return err("bad DHT header","Corrupt JPEG"); // Loop over i < n would write past end of values!
            L -= 17;
            if (tc == 0) {
               if (!build_huffman(z->huff_dc+th, sizes)) return 0;
               v = z->huff_dc[th].values;
            } else {
               if (!build_huffman(z->huff_ac+th, sizes)) return 0;
               v = z->huff_ac[th].values;
            }
            for (i=0; i < n; ++i)
               v[i] = get8(z->s);
            if (tc != 0)
               build_fast_ac(z->fast_ac[th], z->huff_ac + th);
            L -= n;
         }
         return L==0;
   }

   // check for comment block or APP blocks
   if ((m >= 0xE0 && m <= 0xEF) || m == 0xFE) {
      L = get16be(z->s);
      if (L < 2) {
         if (m == 0xFE)
            return err("bad COM len","Corrupt JPEG");
         else
            return err("bad APP len","Corrupt JPEG");
      }
      L -= 2;

      if (m == 0xE0 && L >= 5) { // JFIF APP0 segment
         static const unsigned char tag[5] = {'J','F','I','F','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 5; ++i)
            if (get8(z->s) != tag[i])
               ok = 0;
         L -= 5;
         if (ok)
            z->jfif = 1;
      } else if (m == 0xEE && L >= 12) { // Adobe APP14 segment
         static const unsigned char tag[6] = {'A','d','o','b','e','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 6; ++i)
            if (get8(z->s) != tag[i])
               ok = 0;
         L -= 6;
         if (ok) {
            get8(z->s); // version
            get16be(z->s); // flags0
            get16be(z->s); // flags1
            z->app14_color_transform = get8(z->s); // color transform
            L -= 6;
         }
      }

      skip(z->s, L);
      return 1;
   }

   return err("unknown marker","Corrupt JPEG");
}

// after we see SOS
static int process_scan_header(jpeg *z) noexcept
{
   int i;
   int Ls = get16be(z->s);
   z->scan_n = get8(z->s);
   if (z->scan_n < 1 || z->scan_n > 4 || z->scan_n > (int) z->s->n) return err("bad SOS component count","Corrupt JPEG");
   if (Ls != 6+2*z->scan_n) return err("bad SOS len","Corrupt JPEG");
   for (i=0; i < z->scan_n; ++i) {
      int id = get8(z->s), which;
      int q = get8(z->s);
      for (which = 0; which < z->s->n; ++which)
         if (z->comp[which].id == id)
            break;
      if (which == z->s->n) return 0; // no match
      z->comp[which].hd = q >> 4;   if (z->comp[which].hd > 3) return err("bad DC huff","Corrupt JPEG");
      z->comp[which].ha = q & 15;   if (z->comp[which].ha > 3) return err("bad AC huff","Corrupt JPEG");
      z->order[i] = which;
   }

   {
      int aa;
      z->spec_start = get8(z->s);
      z->spec_end   = get8(z->s); // should be 63, but might be 0
      aa = get8(z->s);
      z->succ_high = (aa >> 4);
      z->succ_low  = (aa & 15);
      if (z->progressive) {
         if (z->spec_start > 63 || z->spec_end > 63  || z->spec_start > z->spec_end || z->succ_high > 13 || z->succ_low > 13)
            return err("bad SOS", "Corrupt JPEG");
      } else {
         if (z->spec_start != 0) return err("bad SOS","Corrupt JPEG");
         if (z->succ_high != 0 || z->succ_low != 0) return err("bad SOS","Corrupt JPEG");
         z->spec_end = 63;
      }
   }

   return 1;
}

static int free_jpeg_components(jpeg *z, int ncomp, int why) noexcept
{
   int i;
   for (i=0; i < ncomp; ++i) {
      if (z->comp[i].raw_data) {
         free(z->comp[i].raw_data);
         z->comp[i].raw_data = NULL;
         z->comp[i].data = NULL;
      }
      if (z->comp[i].raw_coeff) {
         free(z->comp[i].raw_coeff);
         z->comp[i].raw_coeff = 0;
         z->comp[i].coeff = 0;
      }
      if (z->comp[i].linebuf) {
         free(z->comp[i].linebuf);
         z->comp[i].linebuf = NULL;
      }
   }
   return why;
}

static int process_frame_header(jpeg *z, int scan) noexcept
{
   context *s = z->s;
   int Lf,p,i,q, h_max=1,v_max=1,c;
   Lf = get16be(s);         if (Lf < 11) return err("bad SOF len","Corrupt JPEG"); // JPEG
   p  = get8(s);            if (p != 8) return err("only 8-bit","JPEG format not supported: 8-bit only"); // JPEG baseline
   s->y = get16be(s);   if (s->y == 0) return err("no header height", "JPEG format not supported: delayed height"); // Legal, but we don't handle it--but neither does IJG
   s->x = get16be(s);   if (s->x == 0) return err("0 width","Corrupt JPEG"); // JPEG requires
   if (s->y > STBI_MAX_DIMENSIONS) return err("too large","Very large image (corrupt?)");
   if (s->x > STBI_MAX_DIMENSIONS) return err("too large","Very large image (corrupt?)");
   c = get8(s);
   if (c != 3 && c != 1 && c != 4) return err("bad component count","Corrupt JPEG");
   s->n = c;
   for (i=0; i < c; ++i) {
      z->comp[i].data = NULL;
      z->comp[i].linebuf = NULL;
   }

   if (Lf != 8+3*s->n) return err("bad SOF len","Corrupt JPEG");

   z->rgb = 0;
   for (i=0; i < s->n; ++i) {
      static const unsigned char rgb[3] = { 'R', 'G', 'B' };
      z->comp[i].id = get8(s);
      if (s->n == 3 && z->comp[i].id == rgb[i])
         ++z->rgb;
      q = get8(s);
      z->comp[i].h = (q >> 4);  if (!z->comp[i].h || z->comp[i].h > 4) return err("bad H","Corrupt JPEG");
      z->comp[i].v = q & 15;    if (!z->comp[i].v || z->comp[i].v > 4) return err("bad V","Corrupt JPEG");
      z->comp[i].tq = get8(s);  if (z->comp[i].tq > 3) return err("bad TQ","Corrupt JPEG");
   }

   if (scan != STBI__SCAN_load) return 1;

   if (!mad3sizes_valid(s->x, s->y, s->n, 0)) return err("too large", "Image too large to decode");

   for (i=0; i < s->n; ++i) {
      if (z->comp[i].h > h_max) h_max = z->comp[i].h;
      if (z->comp[i].v > v_max) v_max = z->comp[i].v;
   }

   // check that plane subsampling factors are integer ratios; our resamplers can't deal with fractional ratios
   // and I've never seen a non-corrupted JPEG file actually use them
   for (i=0; i < s->n; ++i) {
      if (h_max % z->comp[i].h != 0) return err("bad H","Corrupt JPEG");
      if (v_max % z->comp[i].v != 0) return err("bad V","Corrupt JPEG");
   }

   // compute interleaved mcu info
   z->h_max = h_max;
   z->v_max = v_max;
   z->mcu_w = h_max * 8;
   z->mcu_h = v_max * 8;
   // these sizes can't be more than 17 bits
   z->mcu_x = (s->x + z->mcu_w-1) / z->mcu_w;
   z->mcu_y = (s->y + z->mcu_h-1) / z->mcu_h;

   for (i=0; i < s->n; ++i) {
      // number of effective pixels (e.g. for non-interleaved MCU)
      z->comp[i].x = (s->x * z->comp[i].h + h_max-1) / h_max;
      z->comp[i].y = (s->y * z->comp[i].v + v_max-1) / v_max;
      // to simplify generation, we'll allocate enough memory to decode
      // the bogus oversized data from using interleaved MCUs and their
      // big blocks (e.g. a 16x16 iMCU on an image of width 33); we won't
      // discard the extra data until colorspace conversion
      //
      // mcu_x, mcu_y: <=17 bits; comp[i].h and .v are <=4 (checked earlier)
      // so these muls can't overflow with 32-bit ints (which we require)
      z->comp[i].w2 = z->mcu_x * z->comp[i].h * 8;
      z->comp[i].h2 = z->mcu_y * z->comp[i].v * 8;
      z->comp[i].coeff = 0;
      z->comp[i].raw_coeff = 0;
      z->comp[i].linebuf = NULL;
      z->comp[i].raw_data = malloc_mad2(z->comp[i].w2, z->comp[i].h2, 15);
      if (z->comp[i].raw_data == NULL)
         return free_jpeg_components(z, i+1, err("outofmem", "Out of memory"));
      // align blocks for idct using mmx/sse
      z->comp[i].data = (uc*) (((size_t) z->comp[i].raw_data + 15) & ~15);
      if (z->progressive) {
         // w2, h2 are multiples of 8 (see above)
         z->comp[i].coeff_w = z->comp[i].w2 / 8;
         z->comp[i].coeff_h = z->comp[i].h2 / 8;
         z->comp[i].raw_coeff = malloc_mad3(z->comp[i].w2, z->comp[i].h2, sizeof(short), 15);
         if (z->comp[i].raw_coeff == NULL)
            return free_jpeg_components(z, i+1, err("outofmem", "Out of memory"));
         z->comp[i].coeff = (short*) (((size_t) z->comp[i].raw_coeff + 15) & ~15);
      }
   }

   return 1;
}

// use comparisons since in some cases we handle more than one case (e.g. SOF)
#define DNL(x)         ((x) == 0xdc)
#define SOI(x)         ((x) == 0xd8)
#define EOI(x)         ((x) == 0xd9)
#define SOF(x)         ((x) == 0xc0 || (x) == 0xc1 || (x) == 0xc2)
#define SOS(x)         ((x) == 0xda)

#define SOF_progressive(x)   ((x) == 0xc2)

static int decode_jpeg_header(jpeg *z, int scan) noexcept
{
   int m;
   z->jfif = 0;
   z->app14_color_transform = -1; // valid values are 0,1,2
   z->marker = STBI__MARKER_none; // initialize cached marker to empty
   m = get_marker(z);
   if (!SOI(m)) return err("no SOI","Corrupt JPEG");
   if (scan == STBI__SCAN_type) return 1;
   m = get_marker(z);
   while (!SOF(m)) {
      if (!process_marker(z,m)) return 0;
      m = get_marker(z);
      while (m == STBI__MARKER_none) {
         // some files have extra padding after their blocks, so ok, we'll scan
         if (at_eof(z->s)) return err("no SOF", "Corrupt JPEG");
         m = get_marker(z);
      }
   }
   z->progressive = SOF_progressive(m);
   if (!process_frame_header(z, scan)) return 0;
   return 1;
}

static uc skip_jpeg_junk_at_end(jpeg *j) noexcept
{
   // some JPEGs have junk at end, skip over it but if we find what looks
   // like a valid marker, resume there
   while (!at_eof(j->s)) {
      uc x = get8(j->s);
      while (x == 0xff) { // might be a marker
         if (at_eof(j->s)) return STBI__MARKER_none;
         x = get8(j->s);
         if (x != 0x00 && x != 0xff) {
            // not a stuffed zero or lead-in to another marker, looks
            // like an actual marker, return it
            return x;
         }
         // stuffed zero has x=0 now which ends the loop, meaning we go
         // back to regular scan loop.
         // repeated 0xff keeps trying to read the next byte of the marker.
      }
   }
   return STBI__MARKER_none;
}

// decode image to YCbCr format
static int decode_jpeg_image(jpeg *j) noexcept
{
   int m;
   for (m = 0; m < 4; m++) {
      j->comp[m].raw_data = NULL;
      j->comp[m].raw_coeff = NULL;
   }
   j->restart_interval = 0;
   if (!decode_jpeg_header(j, STBI__SCAN_load)) return 0;
   m = get_marker(j);
   while (!EOI(m)) {
      if (SOS(m)) {
         if (!process_scan_header(j)) return 0;
         if (!parse_entropy_coded_data(j)) return 0;
         if (j->marker == STBI__MARKER_none ) {
         j->marker = skip_jpeg_junk_at_end(j);
            // if we reach eof without hitting a marker, get_marker() below will fail and we'll eventually return 0
         }
         m = get_marker(j);
         if (STBI__RESTART(m))
            m = get_marker(j);
      } else if (DNL(m)) {
         int Ld = get16be(j->s);
         uint32 NL = get16be(j->s);
         if (Ld != 4) return err("bad DNL len", "Corrupt JPEG");
         if (NL != j->s->y) return err("bad DNL height", "Corrupt JPEG");
         m = get_marker(j);
      } else {
         if (!process_marker(j, m)) return 1;
         m = get_marker(j);
      }
   }
   if (j->progressive)
      jpeg_finish(j);
   return 1;
}

// static jfif-centered resampling (across block boundaries)

typedef uc *(*resample_row_func)(uc *out, uc *in0, uc *in1,
                                    int w, int hs);

#define div4(x) ((uc) ((x) >> 2))

static uc *resample_row_1(uc *out, uc *in_near, uc *in_far, int w, int hs) noexcept
{
   STBI_NOTUSED(out);
   STBI_NOTUSED(in_far);
   STBI_NOTUSED(w);
   STBI_NOTUSED(hs);
   return in_near;
}

static uc* resample_row_v_2(uc *out, uc *in_near, uc *in_far, int w, int hs) noexcept
{
   // need to generate two samples vertically for every one in input
   int i;
   STBI_NOTUSED(hs);
   for (i=0; i < w; ++i)
      out[i] = div4(3*in_near[i] + in_far[i] + 2);
   return out;
}

static uc*  resample_row_h_2(uc *out, uc *in_near, uc *in_far, int w, int hs) noexcept
{
   // need to generate two samples horizontally for every one in input
   int i;
   uc *input = in_near;

   if (w == 1) {
      // if only one sample, can't do any interpolation
      out[0] = out[1] = input[0];
      return out;
   }

   out[0] = input[0];
   out[1] = div4(input[0]*3 + input[1] + 2);
   for (i=1; i < w-1; ++i) {
      int n = 3*input[i]+2;
      out[i*2+0] = div4(n+input[i-1]);
      out[i*2+1] = div4(n+input[i+1]);
   }
   out[i*2+0] = div4(input[w-2]*3 + input[w-1] + 2);
   out[i*2+1] = input[w-1];

   STBI_NOTUSED(in_far);
   STBI_NOTUSED(hs);

   return out;
}

#define div16(x) ((uc) ((x) >> 4))

static uc *resample_row_hv_2(uc *out, uc *in_near, uc *in_far, int w, int hs) noexcept
{
   // need to generate 2x2 samples for every one in input
   int i,t0,t1;
   if (w == 1) {
      out[0] = out[1] = div4(3*in_near[0] + in_far[0] + 2);
      return out;
   }

   t1 = 3*in_near[0] + in_far[0];
   out[0] = div4(t1+2);
   for (i=1; i < w; ++i) {
      t0 = t1;
      t1 = 3*in_near[i]+in_far[i];
      out[i*2-1] = div16(3*t0 + t1 + 8);
      out[i*2  ] = div16(3*t1 + t0 + 8);
   }
   out[w*2-1] = div4(t1+2);

   STBI_NOTUSED(hs);

   return out;
}

#if defined(STBI_SSE2) || defined(STBI_NEON)
static uc *resample_row_hv_2_simd(uc *out, uc *in_near, uc *in_far, int w, int hs) noexcept
{
   // need to generate 2x2 samples for every one in input
   int i=0,t0,t1;

   if (w == 1) {
      out[0] = out[1] = div4(3*in_near[0] + in_far[0] + 2);
      return out;
   }

   t1 = 3*in_near[0] + in_far[0];
   // process groups of 8 pixels for as long as we can.
   // note we can't handle the last pixel in a row in this loop
   // because we need to handle the filter boundary conditions.
   for (; i < ((w-1) & ~7); i += 8) {
#if defined(STBI_SSE2)
      // load and perform the vertical filtering pass
      // this uses 3*x + y = 4*x + (y - x)
      __m128i zero  = _mm_setzero_si128();
      __m128i farb  = _mm_loadl_epi64((__m128i *) (in_far + i));
      __m128i nearb = _mm_loadl_epi64((__m128i *) (in_near + i));
      __m128i farw  = _mm_unpacklo_epi8(farb, zero);
      __m128i nearw = _mm_unpacklo_epi8(nearb, zero);
      __m128i diff  = _mm_sub_epi16(farw, nearw);
      __m128i nears = _mm_slli_epi16(nearw, 2);
      __m128i curr  = _mm_add_epi16(nears, diff); // current row

      // horizontal filter works the same based on shifted vers of current
      // row. "prev" is current row shifted right by 1 pixel; we need to
      // insert the previous pixel value (from t1).
      // "next" is current row shifted left by 1 pixel, with first pixel
      // of next block of 8 pixels added in.
      __m128i prv0 = _mm_slli_si128(curr, 2);
      __m128i nxt0 = _mm_srli_si128(curr, 2);
      __m128i prev = _mm_insert_epi16(prv0, t1, 0);
      __m128i next = _mm_insert_epi16(nxt0, 3*in_near[i+8] + in_far[i+8], 7);

      // horizontal filter, polyphase implementation since it's convenient:
      // even pixels = 3*cur + prev = cur*4 + (prev - cur)
      // odd  pixels = 3*cur + next = cur*4 + (next - cur)
      // note the shared term.
      __m128i bias  = _mm_set1_epi16(8);
      __m128i curs = _mm_slli_epi16(curr, 2);
      __m128i prvd = _mm_sub_epi16(prev, curr);
      __m128i nxtd = _mm_sub_epi16(next, curr);
      __m128i curb = _mm_add_epi16(curs, bias);
      __m128i even = _mm_add_epi16(prvd, curb);
      __m128i odd  = _mm_add_epi16(nxtd, curb);

      // interleave even and odd pixels, then undo scaling.
      __m128i int0 = _mm_unpacklo_epi16(even, odd);
      __m128i int1 = _mm_unpackhi_epi16(even, odd);
      __m128i de0  = _mm_srli_epi16(int0, 4);
      __m128i de1  = _mm_srli_epi16(int1, 4);

      // pack and write output
      __m128i outv = _mm_packus_epi16(de0, de1);
      _mm_storeu_si128((__m128i *) (out + i*2), outv);
#elif defined(STBI_NEON)
      // load and perform the vertical filtering pass
      // this uses 3*x + y = 4*x + (y - x)
      uint8x8_t farb  = vld1_u8(in_far + i);
      uint8x8_t nearb = vld1_u8(in_near + i);
      int16x8_t diff  = vreinterpretq_s16_u16(vsubl_u8(farb, nearb));
      int16x8_t nears = vreinterpretq_s16_u16(vshll_n_u8(nearb, 2));
      int16x8_t curr  = vaddq_s16(nears, diff); // current row

      // horizontal filter works the same based on shifted vers of current
      // row. "prev" is current row shifted right by 1 pixel; we need to
      // insert the previous pixel value (from t1).
      // "next" is current row shifted left by 1 pixel, with first pixel
      // of next block of 8 pixels added in.
      int16x8_t prv0 = vextq_s16(curr, curr, 7);
      int16x8_t nxt0 = vextq_s16(curr, curr, 1);
      int16x8_t prev = vsetq_lane_s16(t1, prv0, 0);
      int16x8_t next = vsetq_lane_s16(3*in_near[i+8] + in_far[i+8], nxt0, 7);

      // horizontal filter, polyphase implementation since it's convenient:
      // even pixels = 3*cur + prev = cur*4 + (prev - cur)
      // odd  pixels = 3*cur + next = cur*4 + (next - cur)
      // note the shared term.
      int16x8_t curs = vshlq_n_s16(curr, 2);
      int16x8_t prvd = vsubq_s16(prev, curr);
      int16x8_t nxtd = vsubq_s16(next, curr);
      int16x8_t even = vaddq_s16(curs, prvd);
      int16x8_t odd  = vaddq_s16(curs, nxtd);

      // undo scaling and round, then store with even/odd phases interleaved
      uint8x8x2_t o;
      o.val[0] = vqrshrun_n_s16(even, 4);
      o.val[1] = vqrshrun_n_s16(odd,  4);
      vst2_u8(out + i*2, o);
#endif

      // "previous" value for next iter
      t1 = 3*in_near[i+7] + in_far[i+7];
   }

   t0 = t1;
   t1 = 3*in_near[i] + in_far[i];
   out[i*2] = div16(3*t1 + t0 + 8);

   for (++i; i < w; ++i) {
      t0 = t1;
      t1 = 3*in_near[i]+in_far[i];
      out[i*2-1] = div16(3*t0 + t1 + 8);
      out[i*2  ] = div16(3*t1 + t0 + 8);
   }
   out[w*2-1] = div4(t1+2);

   STBI_NOTUSED(hs);

   return out;
}
#endif

static uc *resample_row_generic(uc *out, uc *in_near, uc *in_far, int w, int hs) noexcept
{
   // resample with nearest-neighbor
   int i,j;
   STBI_NOTUSED(in_far);
   for (i=0; i < w; ++i)
      for (j=0; j < hs; ++j)
         out[i*hs+j] = in_near[i];
   return out;
}

// this is a reduced-precision calculation of YCbCr-to-RGB introduced
// to make sure the code produces the same results in both SIMD and scalar
#define float2fixed(x)  (((int) ((x) * 4096.0f + 0.5f)) << 8)
static void YCbCr_to_RGB_row(uc *out, const uc *y, const uc *pcb, const uc *pcr, int count, int step) noexcept
{
   int i;
   for (i=0; i < count; ++i) {
      int y_fixed = (y[i] << 20) + (1<<19); // rounding
      int r,g,b;
      int cr = pcr[i] - 128;
      int cb = pcb[i] - 128;
      r = y_fixed +  cr* float2fixed(1.40200f);
      g = y_fixed + (cr*-float2fixed(0.71414f)) + ((cb*-float2fixed(0.34414f)) & 0xffff0000);
      b = y_fixed                                     +   cb* float2fixed(1.77200f);
      r >>= 20;
      g >>= 20;
      b >>= 20;
      if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
      if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
      if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }
      out[0] = (uc)r;
      out[1] = (uc)g;
      out[2] = (uc)b;
      out[3] = 255;
      out += step;
   }
}

#if defined(STBI_SSE2) || defined(STBI_NEON)
static void YCbCr_to_RGB_simd(uc *out, uc const *y, uc const *pcb, uc const *pcr, int count, int step) noexcept
{
   int i = 0;

#ifdef STBI_SSE2
   // step == 3 is pretty ugly on the final interleave, and i'm not convinced
   // it's useful in practice (you wouldn't use it for textures, for example).
   // so just accelerate step == 4 case.
   if (step == 4) {
      // this is a fairly straightforward implementation and not super-optimized.
      __m128i signflip  = _mm_set1_epi8(-0x80);
      __m128i cr_const0 = _mm_set1_epi16(   (short) ( 1.40200f*4096.0f+0.5f));
      __m128i cr_const1 = _mm_set1_epi16( - (short) ( 0.71414f*4096.0f+0.5f));
      __m128i cb_const0 = _mm_set1_epi16( - (short) ( 0.34414f*4096.0f+0.5f));
      __m128i cb_const1 = _mm_set1_epi16(   (short) ( 1.77200f*4096.0f+0.5f));
      __m128i y_bias = _mm_set1_epi8((char) (unsigned char) 128);
      __m128i xw = _mm_set1_epi16(255); // alpha channel

      for (; i+7 < count; i += 8) {
         // load
         __m128i y_bytes = _mm_loadl_epi64((__m128i *) (y+i));
         __m128i cr_bytes = _mm_loadl_epi64((__m128i *) (pcr+i));
         __m128i cb_bytes = _mm_loadl_epi64((__m128i *) (pcb+i));
         __m128i cr_biased = _mm_xor_si128(cr_bytes, signflip); // -128
         __m128i cb_biased = _mm_xor_si128(cb_bytes, signflip); // -128

         // unpack to short (and left-shift cr, cb by 8)
         __m128i yw  = _mm_unpacklo_epi8(y_bias, y_bytes);
         __m128i crw = _mm_unpacklo_epi8(_mm_setzero_si128(), cr_biased);
         __m128i cbw = _mm_unpacklo_epi8(_mm_setzero_si128(), cb_biased);

         // color transform
         __m128i yws = _mm_srli_epi16(yw, 4);
         __m128i cr0 = _mm_mulhi_epi16(cr_const0, crw);
         __m128i cb0 = _mm_mulhi_epi16(cb_const0, cbw);
         __m128i cb1 = _mm_mulhi_epi16(cbw, cb_const1);
         __m128i cr1 = _mm_mulhi_epi16(crw, cr_const1);
         __m128i rws = _mm_add_epi16(cr0, yws);
         __m128i gwt = _mm_add_epi16(cb0, yws);
         __m128i bws = _mm_add_epi16(yws, cb1);
         __m128i gws = _mm_add_epi16(gwt, cr1);

         // descale
         __m128i rw = _mm_srai_epi16(rws, 4);
         __m128i bw = _mm_srai_epi16(bws, 4);
         __m128i gw = _mm_srai_epi16(gws, 4);

         // back to byte, set up for transpose
         __m128i brb = _mm_packus_epi16(rw, bw);
         __m128i gxb = _mm_packus_epi16(gw, xw);

         // transpose to interleave channels
         __m128i t0 = _mm_unpacklo_epi8(brb, gxb);
         __m128i t1 = _mm_unpackhi_epi8(brb, gxb);
         __m128i o0 = _mm_unpacklo_epi16(t0, t1);
         __m128i o1 = _mm_unpackhi_epi16(t0, t1);

         // store
         _mm_storeu_si128((__m128i *) (out + 0), o0);
         _mm_storeu_si128((__m128i *) (out + 16), o1);
         out += 32;
      }
   }
#endif

#ifdef STBI_NEON
   // in this version, step=3 support would be easy to add. but is there demand?
   if (step == 4) {
      // this is a fairly straightforward implementation and not super-optimized.
      uint8x8_t signflip = vdup_n_u8(0x80);
      int16x8_t cr_const0 = vdupq_n_s16(   (short) ( 1.40200f*4096.0f+0.5f));
      int16x8_t cr_const1 = vdupq_n_s16( - (short) ( 0.71414f*4096.0f+0.5f));
      int16x8_t cb_const0 = vdupq_n_s16( - (short) ( 0.34414f*4096.0f+0.5f));
      int16x8_t cb_const1 = vdupq_n_s16(   (short) ( 1.77200f*4096.0f+0.5f));

      for (; i+7 < count; i += 8) {
         // load
         uint8x8_t y_bytes  = vld1_u8(y + i);
         uint8x8_t cr_bytes = vld1_u8(pcr + i);
         uint8x8_t cb_bytes = vld1_u8(pcb + i);
         int8x8_t cr_biased = vreinterpret_s8_u8(vsub_u8(cr_bytes, signflip));
         int8x8_t cb_biased = vreinterpret_s8_u8(vsub_u8(cb_bytes, signflip));

         // expand to s16
         int16x8_t yws = vreinterpretq_s16_u16(vshll_n_u8(y_bytes, 4));
         int16x8_t crw = vshll_n_s8(cr_biased, 7);
         int16x8_t cbw = vshll_n_s8(cb_biased, 7);

         // color transform
         int16x8_t cr0 = vqdmulhq_s16(crw, cr_const0);
         int16x8_t cb0 = vqdmulhq_s16(cbw, cb_const0);
         int16x8_t cr1 = vqdmulhq_s16(crw, cr_const1);
         int16x8_t cb1 = vqdmulhq_s16(cbw, cb_const1);
         int16x8_t rws = vaddq_s16(yws, cr0);
         int16x8_t gws = vaddq_s16(vaddq_s16(yws, cb0), cr1);
         int16x8_t bws = vaddq_s16(yws, cb1);

         // undo scaling, round, convert to byte
         uint8x8x4_t o;
         o.val[0] = vqrshrun_n_s16(rws, 4);
         o.val[1] = vqrshrun_n_s16(gws, 4);
         o.val[2] = vqrshrun_n_s16(bws, 4);
         o.val[3] = vdup_n_u8(255);

         // store, interleaving r/g/b/a
         vst4_u8(out, o);
         out += 8*4;
      }
   }
#endif

   for (; i < count; ++i) {
      int y_fixed = (y[i] << 20) + (1<<19); // rounding
      int r,g,b;
      int cr = pcr[i] - 128;
      int cb = pcb[i] - 128;
      r = y_fixed + cr* float2fixed(1.40200f);
      g = y_fixed + cr*-float2fixed(0.71414f) + ((cb*-float2fixed(0.34414f)) & 0xffff0000);
      b = y_fixed                                   +   cb* float2fixed(1.77200f);
      r >>= 20;
      g >>= 20;
      b >>= 20;
      if ((unsigned) r > 255) { if (r < 0) r = 0; else r = 255; }
      if ((unsigned) g > 255) { if (g < 0) g = 0; else g = 255; }
      if ((unsigned) b > 255) { if (b < 0) b = 0; else b = 255; }
      out[0] = (uc)r;
      out[1] = (uc)g;
      out[2] = (uc)b;
      out[3] = 255;
      out += step;
   }
}
#endif

// set up the kernels
static void setup_jpeg(jpeg *j) noexcept
{
   j->idct_block_kernel = idct_block;
   j->YCbCr_to_RGB_kernel = YCbCr_to_RGB_row;
   j->resample_row_hv_2_kernel = resample_row_hv_2;

#ifdef STBI_SSE2
   if (sse2_available()) {
      j->idct_block_kernel = idct_simd;
      j->YCbCr_to_RGB_kernel = YCbCr_to_RGB_simd;
      j->resample_row_hv_2_kernel = resample_row_hv_2_simd;
   }
#endif

#ifdef STBI_NEON
   j->idct_block_kernel = idct_simd;
   j->YCbCr_to_RGB_kernel = YCbCr_to_RGB_simd;
   j->resample_row_hv_2_kernel = resample_row_hv_2_simd;
#endif
}

// clean up the temporary component buffers
static void cleanup_jpeg(jpeg *j) noexcept
{
   free_jpeg_components(j, j->s->n, 0);
}

typedef struct
{
   resample_row_func resample;
   uc *line0,*line1;
   int hs,vs;   // expansion factor in each axis
   int w_lores; // horizontal pixels pre-expansion
   int ystep;   // how far through vertical expansion we are
   int ypos;    // which pre-expansion row we're on
} resample;

// fast 0..255 * 0..255 => 0..255 rounded multiplication
static uc blinn_8x8(uc x, uc y) noexcept
{
   unsigned int t = x*y + 128;
   return (uc) ((t + (t >>8)) >> 8);
}

static uc *load_jpeg_image(jpeg *z, int *out_x, int *out_y, int *comp, int req_comp) noexcept
{
   int n, decode_n, is_rgb;
   z->s->n = 0; // make cleanup_jpeg safe

   // validate req_comp
   if (req_comp < 0 || req_comp > 4) return errpuc("bad req_comp", "Internal error");

   // load a jpeg image from whichever source, but leave in YCbCr format
   if (!decode_jpeg_image(z)) { cleanup_jpeg(z); return NULL; }

   // determine actual number of components to generate
   n = req_comp ? req_comp : z->s->n >= 3 ? 3 : 1;

   is_rgb = z->s->n == 3 && (z->rgb == 3 || (z->app14_color_transform == 0 && !z->jfif));

   if (z->s->n == 3 && n < 3 && !is_rgb)
      decode_n = 1;
   else
      decode_n = z->s->n;

   // nothing to do if no components requested; check this now to avoid
   // accessing uninitialized coutput[0] later
   if (decode_n <= 0) { cleanup_jpeg(z); return NULL; }

   // resample and color-convert
   {
      int k;
      unsigned int i,j;
      uc *output;
      uc *coutput[4] = { NULL, NULL, NULL, NULL };

      resample res_comp[4];

      for (k=0; k < decode_n; ++k) {
         resample *r = &res_comp[k];

         // allocate line buffer big enough for upsampling off the edges
         // with upsample factor of 4
         z->comp[k].linebuf = (uc *) malloc(z->s->x + 3);
         if (!z->comp[k].linebuf) { cleanup_jpeg(z); return errpuc("outofmem", "Out of memory"); }

         r->hs      = z->h_max / z->comp[k].h;
         r->vs      = z->v_max / z->comp[k].v;
         r->ystep   = r->vs >> 1;
         r->w_lores = (z->s->x + r->hs-1) / r->hs;
         r->ypos    = 0;
         r->line0   = r->line1 = z->comp[k].data;

         if      (r->hs == 1 && r->vs == 1) r->resample = resample_row_1;
         else if (r->hs == 1 && r->vs == 2) r->resample = resample_row_v_2;
         else if (r->hs == 2 && r->vs == 1) r->resample = resample_row_h_2;
         else if (r->hs == 2 && r->vs == 2) r->resample = z->resample_row_hv_2_kernel;
         else                               r->resample = resample_row_generic;
      }

      // can't error after this so, this is safe
      output = (uc *) malloc_mad3(n, z->s->x, z->s->y, 1);
      if (!output) { cleanup_jpeg(z); return errpuc("outofmem", "Out of memory"); }

      // now go ahead and resample
      for (j=0; j < z->s->y; ++j) {
         uc *out = output + n * z->s->x * j;
         for (k=0; k < decode_n; ++k) {
            resample *r = &res_comp[k];
            int y_bot = r->ystep >= (r->vs >> 1);
            coutput[k] = r->resample(z->comp[k].linebuf,
                                     y_bot ? r->line1 : r->line0,
                                     y_bot ? r->line0 : r->line1,
                                     r->w_lores, r->hs);
            if (++r->ystep >= r->vs) {
               r->ystep = 0;
               r->line0 = r->line1;
               if (++r->ypos < z->comp[k].y)
                  r->line1 += z->comp[k].w2;
            }
         }
         if (n >= 3) {
            uc *y = coutput[0];
            if (z->s->n == 3) {
               if (is_rgb) {
                  for (i=0; i < z->s->x; ++i) {
                     out[0] = y[i];
                     out[1] = coutput[1][i];
                     out[2] = coutput[2][i];
                     out[3] = 255;
                     out += n;
                  }
               } else {
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->x, n);
               }
            } else if (z->s->n == 4) {
               if (z->app14_color_transform == 0) { // CMYK
                  for (i=0; i < z->s->x; ++i) {
                     uc m = coutput[3][i];
                     out[0] = blinn_8x8(coutput[0][i], m);
                     out[1] = blinn_8x8(coutput[1][i], m);
                     out[2] = blinn_8x8(coutput[2][i], m);
                     out[3] = 255;
                     out += n;
                  }
               } else if (z->app14_color_transform == 2) { // YCCK
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->x, n);
                  for (i=0; i < z->s->x; ++i) {
                     uc m = coutput[3][i];
                     out[0] = blinn_8x8(255 - out[0], m);
                     out[1] = blinn_8x8(255 - out[1], m);
                     out[2] = blinn_8x8(255 - out[2], m);
                     out += n;
                  }
               } else { // YCbCr + alpha?  Ignore the fourth channel for now
                  z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->x, n);
               }
            } else
               for (i=0; i < z->s->x; ++i) {
                  out[0] = out[1] = out[2] = y[i];
                  out[3] = 255; // not used if n==3
                  out += n;
               }
         } else {
            if (is_rgb) {
               if (n == 1)
                  for (i=0; i < z->s->x; ++i)
                     *out++ = compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
               else {
                  for (i=0; i < z->s->x; ++i, out += 2) {
                     out[0] = compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
                     out[1] = 255;
                  }
               }
            } else if (z->s->n == 4 && z->app14_color_transform == 0) {
               for (i=0; i < z->s->x; ++i) {
                  uc m = coutput[3][i];
                  uc r = blinn_8x8(coutput[0][i], m);
                  uc g = blinn_8x8(coutput[1][i], m);
                  uc b = blinn_8x8(coutput[2][i], m);
                  out[0] = compute_y(r, g, b);
                  out[1] = 255;
                  out += n;
               }
            } else if (z->s->n == 4 && z->app14_color_transform == 2) {
               for (i=0; i < z->s->x; ++i) {
                  out[0] = blinn_8x8(255 - coutput[0][i], coutput[3][i]);
                  out[1] = 255;
                  out += n;
               }
            } else {
               uc *y = coutput[0];
               if (n == 1)
                  for (i=0; i < z->s->x; ++i) out[i] = y[i];
               else
                  for (i=0; i < z->s->x; ++i) { *out++ = y[i]; *out++ = 255; }
            }
         }
      }
      cleanup_jpeg(z);
      *out_x = z->s->x;
      *out_y = z->s->y;
      if (comp) *comp = z->s->n >= 3 ? 3 : 1; // report original components, not output
      return output;
   }
}

static void *jpeg_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   unsigned char* result;
   jpeg* j = (jpeg*) malloc(sizeof(jpeg));
   if (!j) return errpuc("outofmem", "Out of memory");
   memset(j, 0, sizeof(jpeg));
   STBI_NOTUSED(ri);
   j->s = s;
   setup_jpeg(j);
   result = load_jpeg_image(j, x,y,comp,req_comp);
   free(j);
   return result;
}

static int jpeg_test(context *s) noexcept
{
   int r;
   jpeg* j = (jpeg*)malloc(sizeof(jpeg));
   if (!j) return err("outofmem", "Out of memory");
   memset(j, 0, sizeof(jpeg));
   j->s = s;
   setup_jpeg(j);
   r = decode_jpeg_header(j, STBI__SCAN_type);
   rewind(s);
   free(j);
   return r;
}

static int jpeg_info_raw(jpeg *j, int *x, int *y, int *comp) noexcept
{
   if (!decode_jpeg_header(j, STBI__SCAN_header)) {
      rewind( j->s );
      return 0;
   }
   if (x) *x = j->s->x;
   if (y) *y = j->s->y;
   if (comp) *comp = j->s->n >= 3 ? 3 : 1;
   return 1;
}

static int jpeg_info(context *s, int *x, int *y, int *comp) noexcept
{
   int result;
   jpeg* j = (jpeg*) (malloc(sizeof(jpeg)));
   if (!j) return err("outofmem", "Out of memory");
   memset(j, 0, sizeof(jpeg));
   j->s = s;
   result = jpeg_info_raw(j, x, y, comp);
   free(j);
   return result;
}

inline int JpegFormatModule::Test(context *s) noexcept
{
   return jpeg_test(s);
}

inline void *JpegFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   return jpeg_load(s, x, y, comp, req_comp, ri);
}

inline int JpegFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return jpeg_info(s, x, y, comp);
}
#endif

} // namespace core
} // namespace detail
} // namespace stbi
