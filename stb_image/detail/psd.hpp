namespace stbi { namespace detail { namespace core {

// Photoshop PSD loader -- PD by Thatcher Ulrich, integration by Nicolas Schulz, tweaked by STB

#ifndef STBI_NO_PSD
static int psd_test(context *s) noexcept
{
   int r = (get32be(s) == 0x38425053);
   rewind(s);
   return r;
}

static int psd_decode_rle(context *s, uc *p, int pixelCount) noexcept
{
   int count, nleft, len;

   count = 0;
   while ((nleft = pixelCount - count) > 0) {
      len = get8(s);
      if (len == 128) {
         // No-op.
      } else if (len < 128) {
         // Copy next len+1 bytes literally.
         len++;
         if (len > nleft) return 0; // corrupt data
         count += len;
         while (len) {
            *p = get8(s);
            p += 4;
            len--;
         }
      } else if (len > 128) {
         uc   val;
         // Next -len+1 bytes in the dest are replicated from next source byte.
         // (Interpret len as a negative 8-bit int.)
         len = 257 - len;
         if (len > nleft) return 0; // corrupt data
         val = get8(s);
         count += len;
         while (len) {
            *p = val;
            p += 4;
            len--;
         }
      }
   }

   return 1;
}

static void *psd_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc) noexcept
{
   int pixelCount;
   int channelCount, compression;
   int channel, i;
   int bitdepth;
   int w,h;
   uc *out;
   STBI_NOTUSED(ri);

   // Check identifier
   if (get32be(s) != 0x38425053)   // "8BPS"
      return errpuc("not PSD", "Corrupt PSD image");

   // Check file type version.
   if (get16be(s) != 1)
      return errpuc("wrong version", "Unsupported version of PSD image");

   // Skip 6 reserved bytes.
   skip(s, 6 );

   // Read the number of channels (R, G, B, A, etc).
   channelCount = get16be(s);
   if (channelCount < 0 || channelCount > 16)
      return errpuc("wrong channel count", "Unsupported number of channels in PSD image");

   // Read the rows and columns of the image.
   h = get32be(s);
   w = get32be(s);

   if (h > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");
   if (w > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");

   // Make sure the depth is 8 bits.
   bitdepth = get16be(s);
   if (bitdepth != 8 && bitdepth != 16)
      return errpuc("unsupported bit depth", "PSD bit depth is not 8 or 16 bit");

   // Make sure the color mode is RGB.
   // Valid options are:
   //   0: Bitmap
   //   1: Grayscale
   //   2: Indexed color
   //   3: RGB color
   //   4: CMYK color
   //   7: Multichannel
   //   8: Duotone
   //   9: Lab color
   if (get16be(s) != 3)
      return errpuc("wrong color format", "PSD is not in RGB color format");

   // Skip the Mode Data.  (It's the palette for indexed color; other info for other modes.)
   skip(s,get32be(s) );

   // Skip the image resources.  (resolution, pen tool paths, etc)
   skip(s, get32be(s) );

   // Skip the reserved data.
   skip(s, get32be(s) );

   // Find out if the data is compressed.
   // Known values:
   //   0: no compression
   //   1: RLE compressed
   compression = get16be(s);
   if (compression > 1)
      return errpuc("bad compression", "PSD has an unknown compression format");

   // Check size
   if (!mad3sizes_valid(4, w, h, 0))
      return errpuc("too large", "Corrupt PSD");

   // Create the destination image.

   if (!compression && bitdepth == 16 && bpc == 16) {
      out = (uc *) malloc_mad3(8, w, h, 0);
      ri->bits_per_channel = 16;
   } else
      out = (uc *) malloc(4 * w*h);

   if (!out) return errpuc("outofmem", "Out of memory");
   pixelCount = w*h;

   // Initialize the data to zero.
   //memset( out, 0, pixelCount * 4 );

   // Finally, the image data.
   if (compression) {
      // RLE as used by .PSD and .TIFF
      // Loop until you get the number of unpacked bytes you are expecting:
      //     Read the next source byte into n.
      //     If n is between 0 and 127 inclusive, copy the next n+1 bytes literally.
      //     Else if n is between -127 and -1 inclusive, copy the next byte -n+1 times.
      //     Else if n is 128, noop.
      // Endloop

      // The RLE-compressed data is preceded by a 2-byte data count for each row in the data,
      // which we're going to just skip.
      skip(s, h * channelCount * 2 );

      // Read the RLE data by channel.
      for (channel = 0; channel < 4; channel++) {
         uc *p;

         p = out+channel;
         if (channel >= channelCount) {
            // Fill this channel with default data.
            for (i = 0; i < pixelCount; i++, p += 4)
               *p = (channel == 3 ? 255 : 0);
         } else {
            // Read the RLE data.
            if (!psd_decode_rle(s, p, pixelCount)) {
               free(out);
               return errpuc("corrupt", "bad RLE data");
            }
         }
      }

   } else {
      // We're at the raw image data.  It's each channel in order (Red, Green, Blue, Alpha, ...)
      // where each channel consists of an 8-bit (or 16-bit) value for each pixel in the image.

      // Read the data by channel.
      for (channel = 0; channel < 4; channel++) {
         if (channel >= channelCount) {
            // Fill this channel with default data.
            if (bitdepth == 16 && bpc == 16) {
               uint16 *q = ((uint16 *) out) + channel;
               uint16 val = channel == 3 ? 65535 : 0;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = val;
            } else {
               uc *p = out+channel;
               uc val = channel == 3 ? 255 : 0;
               for (i = 0; i < pixelCount; i++, p += 4)
                  *p = val;
            }
         } else {
            if (ri->bits_per_channel == 16) {    // output bpc
               uint16 *q = ((uint16 *) out) + channel;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = (uint16) get16be(s);
            } else {
               uc *p = out+channel;
               if (bitdepth == 16) {  // input bpc
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = (uc) (get16be(s) >> 8);
               } else {
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = get8(s);
               }
            }
         }
      }
   }

   // remove weird white matte from PSD
   if (channelCount >= 4) {
      if (ri->bits_per_channel == 16) {
         for (i=0; i < w*h; ++i) {
            uint16 *pixel = (uint16 *) out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 65535) {
               float a = pixel[3] / 65535.0f;
               float ra = 1.0f / a;
               float inv_a = 65535.0f * (1 - ra);
               pixel[0] = (uint16) (pixel[0]*ra + inv_a);
               pixel[1] = (uint16) (pixel[1]*ra + inv_a);
               pixel[2] = (uint16) (pixel[2]*ra + inv_a);
            }
         }
      } else {
         for (i=0; i < w*h; ++i) {
            unsigned char *pixel = out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 255) {
               float a = pixel[3] / 255.0f;
               float ra = 1.0f / a;
               float inv_a = 255.0f * (1 - ra);
               pixel[0] = (unsigned char) (pixel[0]*ra + inv_a);
               pixel[1] = (unsigned char) (pixel[1]*ra + inv_a);
               pixel[2] = (unsigned char) (pixel[2]*ra + inv_a);
            }
         }
      }
   }

   // convert to desired output format
   if (req_comp && req_comp != 4) {
      if (ri->bits_per_channel == 16)
         out = (uc *) convert_format16((uint16 *) out, 4, req_comp, w, h);
      else
         out = convert_format(out, 4, req_comp, w, h);
      if (out == NULL) return out; // convert_format frees input on failure
   }

   if (comp) *comp = 4;
   *y = h;
   *x = w;

   return out;
}
#endif

// *************************************************************************************************


#ifndef STBI_NO_PSD
static int psd_info(context *s, int *x, int *y, int *comp) noexcept
{
   int channelCount, dummy, depth;
   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;
   if (get32be(s) != 0x38425053) {
       rewind( s );
       return 0;
   }
   if (get16be(s) != 1) {
       rewind( s );
       return 0;
   }
   skip(s, 6);
   channelCount = get16be(s);
   if (channelCount < 0 || channelCount > 16) {
       rewind( s );
       return 0;
   }
   *y = get32be(s);
   *x = get32be(s);
   depth = get16be(s);
   if (depth != 8 && depth != 16) {
       rewind( s );
       return 0;
   }
   if (get16be(s) != 3) {
       rewind( s );
       return 0;
   }
   *comp = 4;
   return 1;
}

static int psd_is16(context *s) noexcept
{
   int channelCount, depth;
   if (get32be(s) != 0x38425053) {
       rewind( s );
       return 0;
   }
   if (get16be(s) != 1) {
       rewind( s );
       return 0;
   }
   skip(s, 6);
   channelCount = get16be(s);
   if (channelCount < 0 || channelCount > 16) {
       rewind( s );
       return 0;
   }
   STBI_NOTUSED(get32be(s));
   STBI_NOTUSED(get32be(s));
   depth = get16be(s);
   if (depth != 16) {
       rewind( s );
       return 0;
   }
   return 1;
}

inline int PsdFormatModule::Test(context *s) noexcept
{
   return psd_test(s);
}

inline void *PsdFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc) noexcept
{
   return psd_load(s, x, y, comp, req_comp, ri, bpc);
}

inline int PsdFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return psd_info(s, x, y, comp);
}

inline int PsdFormatModule::Is16(context *s) noexcept
{
   return psd_is16(s);
}
#endif

} // namespace core
} // namespace detail
} // namespace stbi
