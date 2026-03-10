namespace stbi { namespace detail { namespace core {

// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

#ifndef STBI_NO_PNG
typedef struct
{
   uint32 length;
   uint32 type;
} pngchunk;

static pngchunk get_chunk_header(context *s) noexcept
{
   pngchunk c;
   c.length = get32be(s);
   c.type   = get32be(s);
   return c;
}

static int check_png_header(context *s) noexcept
{
   static const uc png_sig[8] = { 137,80,78,71,13,10,26,10 };
   int i;
   for (i=0; i < 8; ++i)
      if (get8(s) != png_sig[i]) return err("bad png sig","Not a PNG");
   return 1;
}

typedef struct
{
   context *s;
   uc *idata, *expanded, *out;
   int depth;
} png;


enum {
   STBI__F_none=0,
   STBI__F_sub=1,
   STBI__F_up=2,
   STBI__F_avg=3,
   STBI__F_paeth=4,
   // synthetic filter used for first scanline to avoid needing a dummy row of 0s
   STBI__F_avg_first
};

static uc first_row_filter[5] =
{
   STBI__F_none,
   STBI__F_sub,
   STBI__F_none,
   STBI__F_avg_first,
   STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int paeth(int a, int b, int c) noexcept
{
   // This formulation looks very different from the reference in the PNG spec, but is
   // actually equivalent and has favorable data dependencies and admits straightforward
   // generation of branch-free code, which helps performance significantly.
   int thresh = c*3 - (a + b);
   int lo = a < b ? a : b;
   int hi = a < b ? b : a;
   int t0 = (hi <= thresh) ? lo : c;
   int t1 = (thresh <= lo) ? hi : t0;
   return t1;
}

static const uc depth_scale_table[9] = { 0, 0xff, 0x55, 0, 0x11, 0,0,0, 0x01 };

// adds an extra all-255 alpha channel
// dest == src is legal
// n must be 1 or 3
static void create_png_alpha_expand8(uc *dest, uc *src, uint32 x, int n) noexcept
{
   int i;
   // must process data backwards since we allow dest==src
   if (n == 1) {
      for (i=x-1; i >= 0; --i) {
         dest[i*2+1] = 255;
         dest[i*2+0] = src[i];
      }
   } else {
      STBI_ASSERT(n == 3);
      for (i=x-1; i >= 0; --i) {
         dest[i*4+3] = 255;
         dest[i*4+2] = src[i*3+2];
         dest[i*4+1] = src[i*3+1];
         dest[i*4+0] = src[i*3+0];
      }
   }
}

// create the png data from post-deflated data
static int create_png_image_raw(png *a, uc *raw, uint32 raw_len, int out_n, uint32 x, uint32 y, int depth, int color) noexcept
{
   int bytes = (depth == 16 ? 2 : 1);
   context *s = a->s;
   uint32 i,j,stride = x*out_n*bytes;
   uint32 len, width_bytes;
   uc *filter_buf;
   int all_ok = 1;
   int k;
   int n = s->n; // copy it into a local for later

   int output_bytes = out_n*bytes;
   int filter_bytes = n*bytes;
   int width = x;

   STBI_ASSERT(out_n == s->n || out_n == s->n+1);
   a->out = (uc *) malloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
   if (!a->out) return err("outofmem", "Out of memory");

   // note: error exits here don't need to clean up a->out individually,
   // do_png always does on error.
   if (!mad3sizes_valid(n, x, depth, 7)) return err("too large", "Corrupt PNG");
   width_bytes = (((n * x * depth) + 7) >> 3);
   if (!mad2sizes_valid(width_bytes, y, width_bytes)) return err("too large", "Corrupt PNG");
   len = (width_bytes + 1) * y;

   // we used to check for exact match between raw_len and len on non-interlaced PNGs,
   // but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
   // so just check for raw_len < len always.
   if (raw_len < len) return err("not enough pixels","Corrupt PNG");

   // Allocate two scan lines worth of filter workspace buffer.
   filter_buf = (uc *) malloc_mad2(width_bytes, 2, 0);
   if (!filter_buf) return err("outofmem", "Out of memory");

   // Filtering for low-bit-depth images
   if (depth < 8) {
      filter_bytes = 1;
      width = width_bytes;
   }

   for (j=0; j < y; ++j) {
      // cur/prior filter buffers alternate
      uc *cur = filter_buf + (j & 1)*width_bytes;
      uc *prior = filter_buf + (~j & 1)*width_bytes;
      uc *dest = a->out + stride*j;
      int nk = width * filter_bytes;
      int filter = *raw++;

      // check filter type
      if (filter > 4) {
         all_ok = err("invalid filter","Corrupt PNG");
         break;
      }

      // if first row, use special filter that doesn't sample previous row
      if (j == 0) filter = first_row_filter[filter];

      // perform actual filtering
      switch (filter) {
      case STBI__F_none:
         memcpy(cur, raw, nk);
         break;
      case STBI__F_sub:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
         break;
      case STBI__F_up:
         for (k = 0; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
         break;
      case STBI__F_avg:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
         break;
      case STBI__F_paeth:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == paeth(0,prior[k],0)
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
         break;
      case STBI__F_avg_first:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
         break;
      }

      raw += nk;

      // expand decoded bits in cur to dest, also adding an extra alpha channel if desired
      if (depth < 8) {
         uc scale = (color == 0) ? depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
         uc *in = cur;
         uc *out = dest;
         uc inb = 0;
         uint32 nsmp = x*n;

         // expand bits to bytes first
         if (depth == 4) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 1) == 0) inb = *in++;
               *out++ = scale * (inb >> 4);
               inb <<= 4;
            }
         } else if (depth == 2) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 3) == 0) inb = *in++;
               *out++ = scale * (inb >> 6);
               inb <<= 2;
            }
         } else {
            STBI_ASSERT(depth == 1);
            for (i=0; i < nsmp; ++i) {
               if ((i & 7) == 0) inb = *in++;
               *out++ = scale * (inb >> 7);
               inb <<= 1;
            }
         }

         // insert alpha=255 values if desired
         if (n != out_n)
            create_png_alpha_expand8(dest, dest, x, n);
      } else if (depth == 8) {
         if (n == out_n)
            memcpy(dest, cur, x*n);
         else
            create_png_alpha_expand8(dest, cur, x, n);
      } else if (depth == 16) {
         // convert the image data from big-endian to platform-native
         uint16 *dest16 = (uint16*)dest;
         uint32 nsmp = x*n;

         if (n == out_n) {
            for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
               *dest16 = (cur[0] << 8) | cur[1];
         } else {
            STBI_ASSERT(n+1 == out_n);
            if (n == 1) {
               for (i = 0; i < x; ++i, dest16 += 2, cur += 2) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = 0xffff;
               }
            } else {
               STBI_ASSERT(n == 3);
               for (i = 0; i < x; ++i, dest16 += 4, cur += 6) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = (cur[2] << 8) | cur[3];
                  dest16[2] = (cur[4] << 8) | cur[5];
                  dest16[3] = 0xffff;
               }
            }
         }
      }
   }

   free(filter_buf);
   if (!all_ok) return 0;

   return 1;
}

static int create_png_image(png *a, uc *image_data, uint32 image_data_len, int out_n, int depth, int color, int interlaced) noexcept
{
   int bytes = (depth == 16 ? 2 : 1);
   int out_bytes = out_n * bytes;
   uc *final;
   int p;
   if (!interlaced)
      return create_png_image_raw(a, image_data, image_data_len, out_n, a->s->x, a->s->y, depth, color);

   // de-interlacing
   final = (uc *) malloc_mad3(a->s->x, a->s->y, out_bytes, 0);
   if (!final) return err("outofmem", "Out of memory");
   for (p=0; p < 7; ++p) {
      int xorig[] = { 0,4,0,2,0,1,0 };
      int yorig[] = { 0,0,4,0,2,0,1 };
      int xspc[]  = { 8,8,4,4,2,2,1 };
      int yspc[]  = { 8,8,8,4,4,2,2 };
      int i,j,x,y;
      // pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
      x = (a->s->x - xorig[p] + xspc[p]-1) / xspc[p];
      y = (a->s->y - yorig[p] + yspc[p]-1) / yspc[p];
      if (x && y) {
         uint32 len = ((((a->s->n * x * depth) + 7) >> 3) + 1) * y;
         if (!create_png_image_raw(a, image_data, image_data_len, out_n, x, y, depth, color)) {
            free(final);
            return 0;
         }
         for (j=0; j < y; ++j) {
            for (i=0; i < x; ++i) {
               int out_y = j*yspc[p]+yorig[p];
               int out_x = i*xspc[p]+xorig[p];
               memcpy(final + out_y*a->s->x*out_bytes + out_x*out_bytes,
                      a->out + (j*x+i)*out_bytes, out_bytes);
            }
         }
         free(a->out);
         image_data += len;
         image_data_len -= len;
      }
   }
   a->out = final;

   return 1;
}

static int compute_transparency(png *z, uc tc[3], int out_n) noexcept
{
   context *s = z->s;
   uint32 i, pixel_count = s->x * s->y;
   uc *p = z->out;

   // compute color-based transparency, assuming we've
   // already got 255 as the alpha value in the output
   STBI_ASSERT(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i=0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 255);
         p += 2;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int compute_transparency16(png *z, uint16 tc[3], int out_n) noexcept
{
   context *s = z->s;
   uint32 i, pixel_count = s->x * s->y;
   uint16 *p = (uint16*) z->out;

   // compute color-based transparency, assuming we've
   // already got 65535 as the alpha value in the output
   STBI_ASSERT(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i = 0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 65535);
         p += 2;
      }
   } else {
      for (i = 0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

static int expand_png_palette(png *a, uc *palette, int len, int pal_img_n) noexcept
{
   uint32 i, pixel_count = a->s->x * a->s->y;
   uc *p, *temp_out, *orig = a->out;

   p = (uc *) malloc_mad2(pixel_count, pal_img_n, 0);
   if (p == NULL) return err("outofmem", "Out of memory");

   // between here and free(out) below, exitting would leak
   temp_out = p;

   if (pal_img_n == 3) {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p += 3;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p[3] = palette[n+3];
         p += 4;
      }
   }
   free(a->out);
   a->out = temp_out;

   STBI_NOTUSED(len);

   return 1;
}

static int unpremultiply_on_load_global = 0;
static int de_iphone_flag_global = 0;

STBIDEF void set_unpremultiply_on_load(int flag_true_if_should_unpremultiply) noexcept
{
   unpremultiply_on_load_global = flag_true_if_should_unpremultiply;
}

STBIDEF void convert_iphone_png_to_rgb(int flag_true_if_should_convert) noexcept
{
   de_iphone_flag_global = flag_true_if_should_convert;
}

#ifndef STBI_THREAD_LOCAL
#define unpremultiply_on_load  unpremultiply_on_load_global
#define de_iphone_flag  de_iphone_flag_global
#else
static STBI_THREAD_LOCAL int unpremultiply_on_load_local, unpremultiply_on_load_set;
static STBI_THREAD_LOCAL int de_iphone_flag_local, de_iphone_flag_set;

STBIDEF void set_unpremultiply_on_load_thread(int flag_true_if_should_unpremultiply) noexcept
{
   unpremultiply_on_load_local = flag_true_if_should_unpremultiply;
   unpremultiply_on_load_set = 1;
}

STBIDEF void convert_iphone_png_to_rgb_thread(int flag_true_if_should_convert) noexcept
{
   de_iphone_flag_local = flag_true_if_should_convert;
   de_iphone_flag_set = 1;
}

#define unpremultiply_on_load  (unpremultiply_on_load_set           \
                                       ? unpremultiply_on_load_local      \
                                       : unpremultiply_on_load_global)
#define de_iphone_flag  (de_iphone_flag_set                         \
                                ? de_iphone_flag_local                    \
                                : de_iphone_flag_global)
#endif // STBI_THREAD_LOCAL

static void de_iphone(png *z) noexcept
{
   context *s = z->s;
   uint32 i, pixel_count = s->x * s->y;
   uc *p = z->out;

   if (s->out_n == 3) {  // convert bgr to rgb
      for (i=0; i < pixel_count; ++i) {
         uc t = p[0];
         p[0] = p[2];
         p[2] = t;
         p += 3;
      }
   } else {
      STBI_ASSERT(s->out_n == 4);
      if (unpremultiply_on_load) {
         // convert bgr to rgb and unpremultiply
         for (i=0; i < pixel_count; ++i) {
            uc a = p[3];
            uc t = p[0];
            if (a) {
               uc half = a / 2;
               p[0] = (p[2] * 255 + half) / a;
               p[1] = (p[1] * 255 + half) / a;
               p[2] = ( t   * 255 + half) / a;
            } else {
               p[0] = p[2];
               p[2] = t;
            }
            p += 4;
         }
      } else {
         // convert bgr to rgb
         for (i=0; i < pixel_count; ++i) {
            uc t = p[0];
            p[0] = p[2];
            p[2] = t;
            p += 4;
         }
      }
   }
}

#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

static int parse_png_file(png *z, int scan, int req_comp) noexcept
{
   uc palette[1024], pal_img_n=0;
   uc has_trans=0, tc[3]={0};
   uint16 tc16[3];
   uint32 ioff=0, idata_limit=0, i, pal_len=0;
   int first=1,k,interlace=0, color=0, is_iphone=0;
   context *s = z->s;

   z->expanded = NULL;
   z->idata = NULL;
   z->out = NULL;

   if (!check_png_header(s)) return 0;

   if (scan == STBI__SCAN_type) return 1;

   for (;;) {
      pngchunk c = get_chunk_header(s);
      switch (c.type) {
         case STBI__PNG_TYPE('C','g','B','I'):
            is_iphone = 1;
            skip(s, c.length);
            break;
         case STBI__PNG_TYPE('I','H','D','R'): {
            int comp,filter;
            if (!first) return err("multiple IHDR","Corrupt PNG");
            first = 0;
            if (c.length != 13) return err("bad IHDR len","Corrupt PNG");
            s->x = get32be(s);
            s->y = get32be(s);
            if (s->y > STBI_MAX_DIMENSIONS) return err("too large","Very large image (corrupt?)");
            if (s->x > STBI_MAX_DIMENSIONS) return err("too large","Very large image (corrupt?)");
            z->depth = get8(s);  if (z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)  return err("1/2/4/8/16-bit only","PNG not supported: 1/2/4/8/16-bit only");
            color = get8(s);  if (color > 6)         return err("bad ctype","Corrupt PNG");
            if (color == 3 && z->depth == 16)                  return err("bad ctype","Corrupt PNG");
            if (color == 3) pal_img_n = 3; else if (color & 1) return err("bad ctype","Corrupt PNG");
            comp  = get8(s);  if (comp) return err("bad comp method","Corrupt PNG");
            filter= get8(s);  if (filter) return err("bad filter method","Corrupt PNG");
            interlace = get8(s); if (interlace>1) return err("bad interlace method","Corrupt PNG");
            if (!s->x || !s->y) return err("0-pixel image","Corrupt PNG");
            if (!pal_img_n) {
               s->n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
               if ((1 << 30) / s->x / s->n < s->y) return err("too large", "Image too large to decode");
            } else {
               // if paletted, then pal_n is our final components, and
               // n is # components to decompress/filter.
               s->n = 1;
               if ((1 << 30) / s->x / 4 < s->y) return err("too large","Corrupt PNG");
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
         }

         case STBI__PNG_TYPE('P','L','T','E'):  {
            if (first) return err("first not IHDR", "Corrupt PNG");
            if (c.length > 256*3) return err("invalid PLTE","Corrupt PNG");
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length) return err("invalid PLTE","Corrupt PNG");
            for (i=0; i < pal_len; ++i) {
               palette[i*4+0] = get8(s);
               palette[i*4+1] = get8(s);
               palette[i*4+2] = get8(s);
               palette[i*4+3] = 255;
            }
            break;
         }

         case STBI__PNG_TYPE('t','R','N','S'): {
            if (first) return err("first not IHDR", "Corrupt PNG");
            if (z->idata) return err("tRNS after IDAT","Corrupt PNG");
            if (pal_img_n) {
               if (scan == STBI__SCAN_header) { s->n = 4; return 1; }
               if (pal_len == 0) return err("tRNS before PLTE","Corrupt PNG");
               if (c.length > pal_len) return err("bad tRNS len","Corrupt PNG");
               pal_img_n = 4;
               for (i=0; i < c.length; ++i)
                  palette[i*4+3] = get8(s);
            } else {
               if (!(s->n & 1)) return err("tRNS with alpha","Corrupt PNG");
               if (c.length != (uint32) s->n*2) return err("bad tRNS len","Corrupt PNG");
               has_trans = 1;
               // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
               if (scan == STBI__SCAN_header) { ++s->n; return 1; }
               if (z->depth == 16) {
                  for (k = 0; k < s->n && k < 3; ++k) // extra loop test to suppress false GCC warning
                     tc16[k] = (uint16)get16be(s); // copy the values as-is
               } else {
                  for (k = 0; k < s->n && k < 3; ++k)
                     tc[k] = (uc)(get16be(s) & 255) * depth_scale_table[z->depth]; // non 8-bit images will be larger
               }
            }
            break;
         }

         case STBI__PNG_TYPE('I','D','A','T'): {
            if (first) return err("first not IHDR", "Corrupt PNG");
            if (pal_img_n && !pal_len) return err("no PLTE","Corrupt PNG");
            if (scan == STBI__SCAN_header) {
               // header scan definitely stops at first IDAT
               if (pal_img_n)
                  s->n = pal_img_n;
               return 1;
            }
            if (c.length > (1u << 30)) return err("IDAT size limit", "IDAT section larger than 2^30 bytes");
            if ((int)(ioff + c.length) < (int)ioff) return 0;
            if (ioff + c.length > idata_limit) {
               uint32 idata_limit_old = idata_limit;
               uc *p;
               if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
               while (ioff + c.length > idata_limit)
                  idata_limit *= 2;
               STBI_NOTUSED(idata_limit_old);
               p = (uc *) realloc_sized(z->idata, idata_limit_old, idata_limit); if (p == NULL) return err("outofmem", "Out of memory");
               z->idata = p;
            }
            if (!getn(s, z->idata+ioff,c.length)) return err("outofdata","Corrupt PNG");
            ioff += c.length;
            break;
         }

         case STBI__PNG_TYPE('I','E','N','D'): {
            uint32 raw_len, bpl;
            if (first) return err("first not IHDR", "Corrupt PNG");
            if (scan != STBI__SCAN_load) return 1;
            if (z->idata == NULL) return err("no IDAT","Corrupt PNG");
            // initial guess for decoded data size to avoid unnecessary reallocs
            bpl = (s->x * z->depth + 7) / 8; // bytes per line, per component
            raw_len = bpl * s->y * s->n /* pixels */ + s->y /* filter mode per row */;
            z->expanded = (uc *) zlib_decode_malloc_guesssize_headerflag((char *) z->idata, ioff, raw_len, (int *) &raw_len, !is_iphone);
            if (z->expanded == NULL) return 0; // zlib should set error
            free(z->idata); z->idata = NULL;
            if ((req_comp == s->n+1 && req_comp != 3 && !pal_img_n) || has_trans)
               s->out_n = s->n+1;
            else
               s->out_n = s->n;
            if (!create_png_image(z, z->expanded, raw_len, s->out_n, z->depth, color, interlace)) return 0;
            if (has_trans) {
               if (z->depth == 16) {
                  if (!compute_transparency16(z, tc16, s->out_n)) return 0;
               } else {
                  if (!compute_transparency(z, tc, s->out_n)) return 0;
               }
            }
            if (is_iphone && de_iphone_flag && s->out_n > 2)
               de_iphone(z);
            if (pal_img_n) {
               // pal_img_n == 3 or 4
               s->n = pal_img_n; // record the actual colors we had
               s->out_n = pal_img_n;
               if (req_comp >= 3) s->out_n = req_comp;
               if (!expand_png_palette(z, palette, pal_len, s->out_n))
                  return 0;
            } else if (has_trans) {
               // non-paletted image with tRNS -> source image has (constant) alpha
               ++s->n;
            }
            free(z->expanded); z->expanded = NULL;
            // end of PNG chunk, read and skip CRC
            get32be(s);
            return 1;
         }

         default:
            // if critical, fail
            if (first) return err("first not IHDR", "Corrupt PNG");
            if ((c.type & (1 << 29)) == 0) {
               #ifndef STBI_NO_FAILURE_STRINGS
               // not threadsafe
               static char invalid_chunk[] = "XXXX PNG chunk not known";
               invalid_chunk[0] = STBI__BYTECAST(c.type >> 24);
               invalid_chunk[1] = STBI__BYTECAST(c.type >> 16);
               invalid_chunk[2] = STBI__BYTECAST(c.type >>  8);
               invalid_chunk[3] = STBI__BYTECAST(c.type >>  0);
               #endif
               return err(invalid_chunk, "PNG not supported: unknown PNG chunk type");
            }
            skip(s, c.length);
            break;
      }
      // end of PNG chunk, read and skip CRC
      get32be(s);
   }
}

static void *do_png(png *p, int *x, int *y, int *n, int req_comp, result_info *ri) noexcept
{
   void *result=NULL;
   if (req_comp < 0 || req_comp > 4) return errpuc("bad req_comp", "Internal error");
   if (parse_png_file(p, STBI__SCAN_load, req_comp)) {
      if (p->depth <= 8)
         ri->bits_per_channel = 8;
      else if (p->depth == 16)
         ri->bits_per_channel = 16;
      else
         return errpuc("bad bits_per_channel", "PNG not supported: unsupported color depth");
      result = p->out;
      p->out = NULL;
      if (req_comp && req_comp != p->s->out_n) {
         if (ri->bits_per_channel == 8)
            result = convert_format((unsigned char *) result, p->s->out_n, req_comp, p->s->x, p->s->y);
         else
            result = convert_format16((uint16 *) result, p->s->out_n, req_comp, p->s->x, p->s->y);
         p->s->out_n = req_comp;
         if (result == NULL) return result;
      }
      *x = p->s->x;
      *y = p->s->y;
      if (n) *n = p->s->n;
   }
   free(p->out);      p->out      = NULL;
   free(p->expanded); p->expanded = NULL;
   free(p->idata);    p->idata    = NULL;

   return result;
}

static void *png_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   png p;
   p.s = s;
   return do_png(&p, x,y,comp,req_comp, ri);
}

static int png_test(context *s) noexcept
{
   int r;
   r = check_png_header(s);
   rewind(s);
   return r;
}

static int png_info_raw(png *p, int *x, int *y, int *comp) noexcept
{
   if (!parse_png_file(p, STBI__SCAN_header, 0)) {
      rewind( p->s );
      return 0;
   }
   if (x) *x = p->s->x;
   if (y) *y = p->s->y;
   if (comp) *comp = p->s->n;
   return 1;
}

static int png_info(context *s, int *x, int *y, int *comp) noexcept
{
   png p;
   p.s = s;
   return png_info_raw(&p, x, y, comp);
}

static int png_is16(context *s) noexcept
{
   png p;
   p.s = s;
   if (!png_info_raw(&p, NULL, NULL, NULL))
	   return 0;
   if (p.depth != 16) {
      rewind(p.s);
      return 0;
   }
   return 1;
}

inline int PngFormatModule::Test(context *s) noexcept
{
   return png_test(s);
}

inline void *PngFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   return png_load(s, x, y, comp, req_comp, ri);
}

inline int PngFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return png_info(s, x, y, comp);
}

inline int PngFormatModule::Is16(context *s) noexcept
{
   return png_is16(s);
}
#endif

} // namespace core
} // namespace detail
} // namespace stbi
