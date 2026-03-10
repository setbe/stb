namespace stbi { namespace detail { namespace core {

// Targa Truevision - TGA
// by Jonathan Dummer
#ifndef STBI_NO_TGA
// returns STBI_rgb or whatever, 0 on error
static int tga_get_comp(int bits_per_pixel, int is_grey, int* is_rgb16) noexcept
{
   // only RGB or RGBA (incl. 16bit) or grey allowed
   if (is_rgb16) *is_rgb16 = 0;
   switch(bits_per_pixel) {
      case 8:  return STBI_grey;
      case 16: if(is_grey) return STBI_grey_alpha;
               // fallthrough
      case 15: if(is_rgb16) *is_rgb16 = 1;
               return STBI_rgb;
      case 24: // fallthrough
      case 32: return bits_per_pixel/8;
      default: return 0;
   }
}

static int tga_info(context *s, int *x, int *y, int *comp) noexcept
{
    int tga_w, tga_h, tga_comp, tga_image_type, tga_bits_per_pixel, tga_colormap_bpp;
    int sz, tga_colormap_type;
    get8(s);                   // discard Offset
    tga_colormap_type = get8(s); // colormap type
    if( tga_colormap_type > 1 ) {
        rewind(s);
        return 0;      // only RGB or indexed allowed
    }
    tga_image_type = get8(s); // image type
    if ( tga_colormap_type == 1 ) { // colormapped (paletted) image
        if (tga_image_type != 1 && tga_image_type != 9) {
            rewind(s);
            return 0;
        }
        skip(s,4);       // skip index of first colormap entry and number of entries
        sz = get8(s);    //   check bits per palette color entry
        if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) {
            rewind(s);
            return 0;
        }
        skip(s,4);       // skip image x and y origin
        tga_colormap_bpp = sz;
    } else { // "normal" image w/o colormap - only RGB or grey allowed, +/- RLE
        if ( (tga_image_type != 2) && (tga_image_type != 3) && (tga_image_type != 10) && (tga_image_type != 11) ) {
            rewind(s);
            return 0; // only RGB or grey allowed, +/- RLE
        }
        skip(s,9); // skip colormap specification and image x/y origin
        tga_colormap_bpp = 0;
    }
    tga_w = get16le(s);
    if( tga_w < 1 ) {
        rewind(s);
        return 0;   // test width
    }
    tga_h = get16le(s);
    if( tga_h < 1 ) {
        rewind(s);
        return 0;   // test height
    }
    tga_bits_per_pixel = get8(s); // bits per pixel
    get8(s); // ignore alpha bits
    if (tga_colormap_bpp != 0) {
        if((tga_bits_per_pixel != 8) && (tga_bits_per_pixel != 16)) {
            // when using a colormap, tga_bits_per_pixel is the size of the indexes
            // I don't think anything but 8 or 16bit indexes makes sense
            rewind(s);
            return 0;
        }
        tga_comp = tga_get_comp(tga_colormap_bpp, 0, NULL);
    } else {
        tga_comp = tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3) || (tga_image_type == 11), NULL);
    }
    if(!tga_comp) {
      rewind(s);
      return 0;
    }
    if (x) *x = tga_w;
    if (y) *y = tga_h;
    if (comp) *comp = tga_comp;
    return 1;                   // seems to have passed everything
}

static int tga_test(context *s) noexcept
{
   int res = 0;
   int sz, tga_color_type;
   get8(s);      //   discard Offset
   tga_color_type = get8(s);   //   color type
   if ( tga_color_type > 1 ) goto errorEnd;   //   only RGB or indexed allowed
   sz = get8(s);   //   image type
   if ( tga_color_type == 1 ) { // colormapped (paletted) image
      if (sz != 1 && sz != 9) goto errorEnd; // colortype 1 demands image type 1 or 9
      skip(s,4);       // skip index of first colormap entry and number of entries
      sz = get8(s);    //   check bits per palette color entry
      if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;
      skip(s,4);       // skip image x and y origin
   } else { // "normal" image w/o colormap
      if ( (sz != 2) && (sz != 3) && (sz != 10) && (sz != 11) ) goto errorEnd; // only RGB or grey allowed, +/- RLE
      skip(s,9); // skip colormap specification and image x/y origin
   }
   if ( get16le(s) < 1 ) goto errorEnd;      //   test width
   if ( get16le(s) < 1 ) goto errorEnd;      //   test height
   sz = get8(s);   //   bits per pixel
   if ( (tga_color_type == 1) && (sz != 8) && (sz != 16) ) goto errorEnd; // for colormapped images, bpp is size of an index
   if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;

   res = 1; // if we got this far, everything's good and we can return 1 instead of 0

errorEnd:
   rewind(s);
   return res;
}

// read 16bit value and convert to 24bit RGB
static void tga_read_rgb16(context *s, uc* out) noexcept
{
   uint16 px = (uint16)get16le(s);
   uint16 fiveBitMask = 31;
   // we have 3 channels with 5bits each
   int r = (px >> 10) & fiveBitMask;
   int g = (px >> 5) & fiveBitMask;
   int b = px & fiveBitMask;
   // Note that this saves the data in RGB(A) order, so it doesn't need to be swapped later
   out[0] = (uc)((r * 255)/31);
   out[1] = (uc)((g * 255)/31);
   out[2] = (uc)((b * 255)/31);

   // some people claim that the most significant bit might be used for alpha
   // (possibly if an alpha-bit is set in the "image descriptor byte")
   // but that only made 16bit test images completely translucent..
   // so let's treat all 15 and 16bit TGAs as RGB with no alpha.
}

static void *tga_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   //   read in the TGA header stuff
   int tga_offset = get8(s);
   int tga_indexed = get8(s);
   int tga_image_type = get8(s);
   int tga_is_RLE = 0;
   int tga_palette_start = get16le(s);
   int tga_palette_len = get16le(s);
   int tga_palette_bits = get8(s);
   int tga_x_origin = get16le(s);
   int tga_y_origin = get16le(s);
   int tga_width = get16le(s);
   int tga_height = get16le(s);
   int tga_bits_per_pixel = get8(s);
   int tga_comp, tga_rgb16=0;
   int tga_inverted = get8(s);
   // int tga_alpha_bits = tga_inverted & 15; // the 4 lowest bits - unused (useless?)
   //   image data
   unsigned char *tga_data;
   unsigned char *tga_palette = NULL;
   int i, j;
   unsigned char raw_data[4] = {0};
   int RLE_count = 0;
   int RLE_repeating = 0;
   int read_next_pixel = 1;
   STBI_NOTUSED(ri);
   STBI_NOTUSED(tga_x_origin); // @TODO
   STBI_NOTUSED(tga_y_origin); // @TODO

   if (tga_height > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");
   if (tga_width > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");

   //   do a tiny bit of precessing
   if ( tga_image_type >= 8 )
   {
      tga_image_type -= 8;
      tga_is_RLE = 1;
   }
   tga_inverted = 1 - ((tga_inverted >> 5) & 1);

   //   If I'm paletted, then I'll use the number of bits from the palette
   if ( tga_indexed ) tga_comp = tga_get_comp(tga_palette_bits, 0, &tga_rgb16);
   else tga_comp = tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3), &tga_rgb16);

   if(!tga_comp) // shouldn't really happen, tga_test() should have ensured basic consistency
      return errpuc("bad format", "Can't find out TGA pixelformat");

   //   tga info
   *x = tga_width;
   *y = tga_height;
   if (comp) *comp = tga_comp;

   if (!mad3sizes_valid(tga_width, tga_height, tga_comp, 0))
      return errpuc("too large", "Corrupt TGA");

   tga_data = (unsigned char*)malloc_mad3(tga_width, tga_height, tga_comp, 0);
   if (!tga_data) return errpuc("outofmem", "Out of memory");

   // skip to the data's starting position (offset usually = 0)
   skip(s, tga_offset );

   if ( !tga_indexed && !tga_is_RLE && !tga_rgb16 ) {
      for (i=0; i < tga_height; ++i) {
         int row = tga_inverted ? tga_height -i - 1 : i;
         uc *tga_row = tga_data + row*tga_width*tga_comp;
         getn(s, tga_row, tga_width * tga_comp);
      }
   } else  {
      //   do I need to load a palette?
      if ( tga_indexed)
      {
         if (tga_palette_len == 0) {  /* you have to have at least one entry! */
            free(tga_data);
            return errpuc("bad palette", "Corrupt TGA");
         }

         //   any data to skip? (offset usually = 0)
         skip(s, tga_palette_start );
         //   load the palette
         tga_palette = (unsigned char*)malloc_mad2(tga_palette_len, tga_comp, 0);
         if (!tga_palette) {
            free(tga_data);
            return errpuc("outofmem", "Out of memory");
         }
         if (tga_rgb16) {
            uc *pal_entry = tga_palette;
            STBI_ASSERT(tga_comp == STBI_rgb);
            for (i=0; i < tga_palette_len; ++i) {
               tga_read_rgb16(s, pal_entry);
               pal_entry += tga_comp;
            }
         } else if (!getn(s, tga_palette, tga_palette_len * tga_comp)) {
               free(tga_data);
               free(tga_palette);
               return errpuc("bad palette", "Corrupt TGA");
         }
      }
      //   load the data
      for (i=0; i < tga_width * tga_height; ++i)
      {
         //   if I'm in RLE mode, do I need to get a RLE pngchunk?
         if ( tga_is_RLE )
         {
            if ( RLE_count == 0 )
            {
               //   yep, get the next byte as a RLE command
               int RLE_cmd = get8(s);
               RLE_count = 1 + (RLE_cmd & 127);
               RLE_repeating = RLE_cmd >> 7;
               read_next_pixel = 1;
            } else if ( !RLE_repeating )
            {
               read_next_pixel = 1;
            }
         } else
         {
            read_next_pixel = 1;
         }
         //   OK, if I need to read a pixel, do it now
         if ( read_next_pixel )
         {
            //   load however much data we did have
            if ( tga_indexed )
            {
               // read in index, then perform the lookup
               int pal_idx = (tga_bits_per_pixel == 8) ? get8(s) : get16le(s);
               if ( pal_idx >= tga_palette_len ) {
                  // invalid index
                  pal_idx = 0;
               }
               pal_idx *= tga_comp;
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = tga_palette[pal_idx+j];
               }
            } else if(tga_rgb16) {
               STBI_ASSERT(tga_comp == STBI_rgb);
               tga_read_rgb16(s, raw_data);
            } else {
               //   read in the data raw
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = get8(s);
               }
            }
            //   clear the reading flag for the next pixel
            read_next_pixel = 0;
         } // end of reading a pixel

         // copy data
         for (j = 0; j < tga_comp; ++j)
           tga_data[i*tga_comp+j] = raw_data[j];

         //   in case we're in RLE mode, keep counting down
         --RLE_count;
      }
      //   do I need to invert the image?
      if ( tga_inverted )
      {
         for (j = 0; j*2 < tga_height; ++j)
         {
            int index1 = j * tga_width * tga_comp;
            int index2 = (tga_height - 1 - j) * tga_width * tga_comp;
            for (i = tga_width * tga_comp; i > 0; --i)
            {
               unsigned char temp = tga_data[index1];
               tga_data[index1] = tga_data[index2];
               tga_data[index2] = temp;
               ++index1;
               ++index2;
            }
         }
      }
      //   clear my palette, if I had one
      if ( tga_palette != NULL )
      {
         free( tga_palette );
      }
   }

   // swap RGB - if the source data was RGB16, it already is in the right order
   if (tga_comp >= 3 && !tga_rgb16)
   {
      unsigned char* tga_pixel = tga_data;
      for (i=0; i < tga_width * tga_height; ++i)
      {
         unsigned char temp = tga_pixel[0];
         tga_pixel[0] = tga_pixel[2];
         tga_pixel[2] = temp;
         tga_pixel += tga_comp;
      }
   }

   // convert to target component count
   if (req_comp && req_comp != tga_comp)
      tga_data = convert_format(tga_data, tga_comp, req_comp, tga_width, tga_height);

   //   the things I do to get rid of an error message, and yet keep
   //   Microsoft's C compilers happy... [8^(
   tga_palette_start = tga_palette_len = tga_palette_bits =
         tga_x_origin = tga_y_origin = 0;
   STBI_NOTUSED(tga_palette_start);
   //   OK, done
   return tga_data;
}

inline int TgaFormatModule::Test(context *s) noexcept
{
   return tga_test(s);
}

inline void *TgaFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   return tga_load(s, x, y, comp, req_comp, ri);
}

inline int TgaFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return tga_info(s, x, y, comp);
}
#endif

// *************************************************************************************************

} // namespace core
} // namespace detail
} // namespace stbi
