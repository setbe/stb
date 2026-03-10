namespace stbi { namespace detail { namespace core {

// GIF loader -- public domain by Jean-Marc Lienher -- simplified/shrunk by stb

#ifndef STBI_NO_GIF
typedef struct
{
   int16 prefix;
   uc first;
   uc suffix;
} gif_lzw;

typedef struct
{
   int w,h;
   uc *out;                 // output buffer (always 4 components)
   uc *background;          // The current "background" as far as a gif is concerned
   uc *history;
   int flags, bgindex, ratio, transparent, eflags;
   uc  pal[256][4];
   uc lpal[256][4];
   gif_lzw codes[8192];
   uc *color_table;
   int parse, step;
   int lflags;
   int start_x, start_y;
   int max_x, max_y;
   int cur_x, cur_y;
   int line_size;
   int delay;
} gif;

static int gif_test_raw(context *s) noexcept
{
   int sz;
   if (get8(s) != 'G' || get8(s) != 'I' || get8(s) != 'F' || get8(s) != '8') return 0;
   sz = get8(s);
   if (sz != '9' && sz != '7') return 0;
   if (get8(s) != 'a') return 0;
   return 1;
}

static int gif_test(context *s) noexcept
{
   int r = gif_test_raw(s);
   rewind(s);
   return r;
}

static void gif_parse_colortable(context *s, uc pal[256][4], int num_entries, int transp) noexcept
{
   int i;
   for (i=0; i < num_entries; ++i) {
      pal[i][2] = get8(s);
      pal[i][1] = get8(s);
      pal[i][0] = get8(s);
      pal[i][3] = transp == i ? 0 : 255;
   }
}

static int gif_header(context *s, gif *g, int *comp, int is_info) noexcept
{
   uc version;
   if (get8(s) != 'G' || get8(s) != 'I' || get8(s) != 'F' || get8(s) != '8')
      return err("not GIF", "Corrupt GIF");

   version = get8(s);
   if (version != '7' && version != '9')    return err("not GIF", "Corrupt GIF");
   if (get8(s) != 'a')                return err("not GIF", "Corrupt GIF");

   g_failure_reason = "";
   g->w = get16le(s);
   g->h = get16le(s);
   g->flags = get8(s);
   g->bgindex = get8(s);
   g->ratio = get8(s);
   g->transparent = -1;

   if (g->w > STBI_MAX_DIMENSIONS) return err("too large","Very large image (corrupt?)");
   if (g->h > STBI_MAX_DIMENSIONS) return err("too large","Very large image (corrupt?)");

   if (comp != 0) *comp = 4;  // can't actually tell whether it's 3 or 4 until we parse the comments

   if (is_info) return 1;

   if (g->flags & 0x80)
      gif_parse_colortable(s,g->pal, 2 << (g->flags & 7), -1);

   return 1;
}

static int gif_info_raw(context *s, int *x, int *y, int *comp) noexcept
{
   gif* g = (gif*) malloc(sizeof(gif));
   if (!g) return err("outofmem", "Out of memory");
   if (!gif_header(s, g, comp, 1)) {
      free(g);
      rewind( s );
      return 0;
   }
   if (x) *x = g->w;
   if (y) *y = g->h;
   free(g);
   return 1;
}

static void out_gif_code(gif *g, uint16 code) noexcept
{
   uc *p, *c;
   int idx;

   // recurse to decode the prefixes, since the linked-list is backwards,
   // and working backwards through an interleaved image would be nasty
   if (g->codes[code].prefix >= 0)
      out_gif_code(g, g->codes[code].prefix);

   if (g->cur_y >= g->max_y) return;

   idx = g->cur_x + g->cur_y;
   p = &g->out[idx];
   g->history[idx / 4] = 1;

   c = &g->color_table[g->codes[code].suffix * 4];
   if (c[3] > 128) { // don't render transparent pixels;
      p[0] = c[2];
      p[1] = c[1];
      p[2] = c[0];
      p[3] = c[3];
   }
   g->cur_x += 4;

   if (g->cur_x >= g->max_x) {
      g->cur_x = g->start_x;
      g->cur_y += g->step;

      while (g->cur_y >= g->max_y && g->parse > 0) {
         g->step = (1 << g->parse) * g->line_size;
         g->cur_y = g->start_y + (g->step >> 1);
         --g->parse;
      }
   }
}

static uc *process_gif_raster(context *s, gif *g) noexcept
{
   uc lzw_cs;
   int32 len, init_code;
   uint32 first;
   int32 codesize, codemask, avail, oldcode, bits, valid_bits, clear;
   gif_lzw *p;

   lzw_cs = get8(s);
   if (lzw_cs > 12) return NULL;
   clear = 1 << lzw_cs;
   first = 1;
   codesize = lzw_cs + 1;
   codemask = (1 << codesize) - 1;
   bits = 0;
   valid_bits = 0;
   for (init_code = 0; init_code < clear; init_code++) {
      g->codes[init_code].prefix = -1;
      g->codes[init_code].first = (uc) init_code;
      g->codes[init_code].suffix = (uc) init_code;
   }

   // support no starting clear code
   avail = clear+2;
   oldcode = -1;

   len = 0;
   for(;;) {
      if (valid_bits < codesize) {
         if (len == 0) {
            len = get8(s); // start new block
            if (len == 0)
               return g->out;
         }
         --len;
         bits |= (int32) get8(s) << valid_bits;
         valid_bits += 8;
      } else {
         int32 code = bits & codemask;
         bits >>= codesize;
         valid_bits -= codesize;
         // @OPTIMIZE: is there some way we can accelerate the non-clear path?
         if (code == clear) {  // clear code
            codesize = lzw_cs + 1;
            codemask = (1 << codesize) - 1;
            avail = clear + 2;
            oldcode = -1;
            first = 0;
         } else if (code == clear + 1) { // end of stream code
            skip(s, len);
            while ((len = get8(s)) > 0)
               skip(s,len);
            return g->out;
         } else if (code <= avail) {
            if (first) {
               return errpuc("no clear code", "Corrupt GIF");
            }

            if (oldcode >= 0) {
               p = &g->codes[avail++];
               if (avail > 8192) {
                  return errpuc("too many codes", "Corrupt GIF");
               }

               p->prefix = (int16) oldcode;
               p->first = g->codes[oldcode].first;
               p->suffix = (code == avail) ? p->first : g->codes[code].first;
            } else if (code == avail)
               return errpuc("illegal code in raster", "Corrupt GIF");

            out_gif_code(g, (uint16) code);

            if ((avail & codemask) == 0 && avail <= 0x0FFF) {
               codesize++;
               codemask = (1 << codesize) - 1;
            }

            oldcode = code;
         } else {
            return errpuc("illegal code in raster", "Corrupt GIF");
         }
      }
   }
}

// this function is designed to support animated gifs, although stb_image doesn't support it
// two back is the image from two frames ago, used for a very specific disposal format
static uc *gif_load_next(context *s, gif *g, int *comp, int req_comp, uc *two_back) noexcept
{
   int dispose;
   int first_frame;
   int pi;
   int pcount;
   STBI_NOTUSED(req_comp);

   // on first frame, any non-written pixels get the background colour (non-transparent)
   first_frame = 0;
   if (g->out == 0) {
      if (!gif_header(s, g, comp,0)) return 0; // g_failure_reason set by gif_header
      if (!mad3sizes_valid(4, g->w, g->h, 0))
         return errpuc("too large", "GIF image is too large");
      pcount = g->w * g->h;
      g->out = (uc *) malloc(4 * pcount);
      g->background = (uc *) malloc(4 * pcount);
      g->history = (uc *) malloc(pcount);
      if (!g->out || !g->background || !g->history)
         return errpuc("outofmem", "Out of memory");

      // image is treated as "transparent" at the start - ie, nothing overwrites the current background;
      // background colour is only used for pixels that are not rendered first frame, after that "background"
      // color refers to the color that was there the previous frame.
      memset(g->out, 0x00, 4 * pcount);
      memset(g->background, 0x00, 4 * pcount); // state of the background (starts transparent)
      memset(g->history, 0x00, pcount);        // pixels that were affected previous frame
      first_frame = 1;
   } else {
      // second frame - how do we dispose of the previous one?
      dispose = (g->eflags & 0x1C) >> 2;
      pcount = g->w * g->h;

      if ((dispose == 3) && (two_back == 0)) {
         dispose = 2; // if I don't have an image to revert back to, default to the old background
      }

      if (dispose == 3) { // use previous graphic
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history[pi]) {
               memcpy( &g->out[pi * 4], &two_back[pi * 4], 4 );
            }
         }
      } else if (dispose == 2) {
         // restore what was changed last frame to background before that frame;
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history[pi]) {
               memcpy( &g->out[pi * 4], &g->background[pi * 4], 4 );
            }
         }
      } else {
         // This is a non-disposal case eithe way, so just
         // leave the pixels as is, and they will become the new background
         // 1: do not dispose
         // 0:  not specified.
      }

      // background is what out is after the undoing of the previou frame;
      memcpy( g->background, g->out, 4 * g->w * g->h );
   }

   // clear my history;
   memset( g->history, 0x00, g->w * g->h );        // pixels that were affected previous frame

   for (;;) {
      int tag = get8(s);
      switch (tag) {
         case 0x2C: /* Image Descriptor */
         {
            int32 x, y, w, h;
            uc *o;

            x = get16le(s);
            y = get16le(s);
            w = get16le(s);
            h = get16le(s);
            if (((x + w) > (g->w)) || ((y + h) > (g->h)))
               return errpuc("bad Image Descriptor", "Corrupt GIF");

            g->line_size = g->w * 4;
            g->start_x = x * 4;
            g->start_y = y * g->line_size;
            g->max_x   = g->start_x + w * 4;
            g->max_y   = g->start_y + h * g->line_size;
            g->cur_x   = g->start_x;
            g->cur_y   = g->start_y;

            // if the width of the specified rectangle is 0, that means
            // we may not see *any* pixels or the image is malformed;
            // to make sure this is caught, move the current y down to
            // max_y (which is what out_gif_code checks).
            if (w == 0)
               g->cur_y = g->max_y;

            g->lflags = get8(s);

            if (g->lflags & 0x40) {
               g->step = 8 * g->line_size; // first interlaced spacing
               g->parse = 3;
            } else {
               g->step = g->line_size;
               g->parse = 0;
            }

            if (g->lflags & 0x80) {
               gif_parse_colortable(s,g->lpal, 2 << (g->lflags & 7), g->eflags & 0x01 ? g->transparent : -1);
               g->color_table = (uc *) g->lpal;
            } else if (g->flags & 0x80) {
               g->color_table = (uc *) g->pal;
            } else
               return errpuc("missing color table", "Corrupt GIF");

            o = process_gif_raster(s, g);
            if (!o) return NULL;

            // if this was the first frame,
            pcount = g->w * g->h;
            if (first_frame && (g->bgindex > 0)) {
               // if first frame, any pixel not drawn to gets the background color
               for (pi = 0; pi < pcount; ++pi) {
                  if (g->history[pi] == 0) {
                     g->pal[g->bgindex][3] = 255; // just in case it was made transparent, undo that; It will be reset next frame if need be;
                     memcpy( &g->out[pi * 4], &g->pal[g->bgindex], 4 );
                  }
               }
            }

            return o;
         }

         case 0x21: // Comment Extension.
         {
            int len;
            int ext = get8(s);
            if (ext == 0xF9) { // Graphic Control Extension.
               len = get8(s);
               if (len == 4) {
                  g->eflags = get8(s);
                  g->delay = 10 * get16le(s); // delay - 1/100th of a second, saving as 1/1000ths.

                  // unset old transparent
                  if (g->transparent >= 0) {
                     g->pal[g->transparent][3] = 255;
                  }
                  if (g->eflags & 0x01) {
                     g->transparent = get8(s);
                     if (g->transparent >= 0) {
                        g->pal[g->transparent][3] = 0;
                     }
                  } else {
                     // don't need transparent
                     skip(s, 1);
                     g->transparent = -1;
                  }
               } else {
                  skip(s, len);
                  break;
               }
            }
            while ((len = get8(s)) != 0) {
               skip(s, len);
            }
            break;
         }

         case 0x3B: // gif stream termination code
            return (uc *) s; // using '1' causes warning on some compilers

         default:
            return errpuc("unknown code", "Corrupt GIF");
      }
   }
}

static void *load_gif_main_outofmem(gif *g, uc *out, int **delays) noexcept
{
   free(g->out);
   free(g->history);
   free(g->background);

   if (out) free(out);
   if (delays && *delays) free(*delays);
   return errpuc("outofmem", "Out of memory");
}

static void *load_gif_main(context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp) noexcept
{
   if (gif_test(s)) {
      int layers = 0;
      uc *u = 0;
      uc *out = 0;
      uc *two_back = 0;
      gif g;
      int stride;
      int out_size = 0;
      int delays_size = 0;

      STBI_NOTUSED(out_size);
      STBI_NOTUSED(delays_size);

      memset(&g, 0, sizeof(g));
      if (delays) {
         *delays = 0;
      }

      do {
         u = gif_load_next(s, &g, comp, req_comp, two_back);
         if (u == (uc *) s) u = 0;  // end of animated gif marker

         if (u) {
            *x = g.w;
            *y = g.h;
            ++layers;
            stride = g.w * g.h * 4;

            if (out) {
               void *tmp = (uc*) realloc_sized( out, out_size, layers * stride );
               if (!tmp)
                  return load_gif_main_outofmem(&g, out, delays);
               else {
                   out = (uc*) tmp;
                   out_size = layers * stride;
               }

               if (delays) {
                  int *new_delays = (int*) realloc_sized( *delays, delays_size, sizeof(int) * layers );
                  if (!new_delays)
                     return load_gif_main_outofmem(&g, out, delays);
                  *delays = new_delays;
                  delays_size = layers * sizeof(int);
               }
            } else {
               out = (uc*)malloc( layers * stride );
               if (!out)
                  return load_gif_main_outofmem(&g, out, delays);
               out_size = layers * stride;
               if (delays) {
                  *delays = (int*) malloc( layers * sizeof(int) );
                  if (!*delays)
                     return load_gif_main_outofmem(&g, out, delays);
                  delays_size = layers * sizeof(int);
               }
            }
            memcpy( out + ((layers - 1) * stride), u, stride );
            if (layers >= 2) {
               two_back = out - 2 * stride;
            }

            if (delays) {
               (*delays)[layers - 1U] = g.delay;
            }
         }
      } while (u != 0);

      // free temp buffer;
      free(g.out);
      free(g.history);
      free(g.background);

      // do the final conversion after loading everything;
      if (req_comp && req_comp != 4)
         out = convert_format(out, 4, req_comp, layers * g.w, g.h);

      *z = layers;
      return out;
   } else {
      return errpuc("not GIF", "Image was not as a gif type.");
   }
}

static void *gif_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   uc *u = 0;
   gif g;
   memset(&g, 0, sizeof(g));
   STBI_NOTUSED(ri);

   u = gif_load_next(s, &g, comp, req_comp, 0);
   if (u == (uc *) s) u = 0;  // end of animated gif marker
   if (u) {
      *x = g.w;
      *y = g.h;

      // moved conversion to after successful load so that the same
      // can be done for multiple frames.
      if (req_comp && req_comp != 4)
         u = convert_format(u, 4, req_comp, g.w, g.h);
   } else if (g.out) {
      // if there was an error and we allocated an image buffer, free it!
      free(g.out);
   }

   // free buffers needed for multiple frame loading;
   free(g.history);
   free(g.background);

   return u;
}

static int gif_info(context *s, int *x, int *y, int *comp) noexcept
{
   return gif_info_raw(s,x,y,comp);
}

inline int GifFormatModule::Test(context *s) noexcept
{
   return gif_test(s);
}

inline void *GifFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   return gif_load(s, x, y, comp, req_comp, ri);
}

inline int GifFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return gif_info(s, x, y, comp);
}
#endif

// *************************************************************************************************

} // namespace core
} // namespace detail
} // namespace stbi
