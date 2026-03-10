namespace stbi { namespace detail { namespace core {

// Radiance RGBE HDR loader
// originally by Nicolas Schulz
#ifndef STBI_NO_HDR
static int hdr_test_core(context *s, const char *signature) noexcept
{
   int i;
   for (i=0; signature[i]; ++i)
      if (get8(s) != signature[i])
          return 0;
   rewind(s);
   return 1;
}

static int hdr_test(context* s) noexcept
{
   int r = hdr_test_core(s, "#?RADIANCE\n");
   rewind(s);
   if(!r) {
       r = hdr_test_core(s, "#?RGBE\n");
       rewind(s);
   }
   return r;
}

#define STBI__HDR_BUFLEN  1024
static char *hdr_gettoken(context *z, char *buffer) noexcept
{
   int len=0;
   char c = '\0';

   c = (char) get8(z);

   while (!at_eof(z) && c != '\n') {
      buffer[len++] = c;
      if (len == STBI__HDR_BUFLEN-1) {
         // flush to end of line
         while (!at_eof(z) && get8(z) != '\n')
            ;
         break;
      }
      c = (char) get8(z);
   }

   buffer[len] = 0;
   return buffer;
}

static void hdr_convert(float *output, uc *input, int req_comp) noexcept
{
   if ( input[3] != 0 ) {
      float f1;
      // Exponent
      f1 = (float) ldexp(1.0f, input[3] - (int)(128 + 8));
      if (req_comp <= 2)
         output[0] = (input[0] + input[1] + input[2]) * f1 / 3;
      else {
         output[0] = input[0] * f1;
         output[1] = input[1] * f1;
         output[2] = input[2] * f1;
      }
      if (req_comp == 2) output[1] = 1;
      if (req_comp == 4) output[3] = 1;
   } else {
      switch (req_comp) {
         case 4: output[3] = 1; /* fallthrough */
         case 3: output[0] = output[1] = output[2] = 0;
                 break;
         case 2: output[1] = 1; /* fallthrough */
         case 1: output[0] = 0;
                 break;
      }
   }
}

static float *hdr_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   char buffer[STBI__HDR_BUFLEN];
   char *token;
   int valid = 0;
   int width, height;
   uc *scanline;
   float *hdr_data;
   int len;
   unsigned char count, value;
   int i, j, k, c1,c2, z;
   const char *headerToken;
   STBI_NOTUSED(ri);

   // Check identifier
   headerToken = hdr_gettoken(s,buffer);
   if (strcmp(headerToken, "#?RADIANCE") != 0 && strcmp(headerToken, "#?RGBE") != 0)
      return errpf("not HDR", "Corrupt HDR image");

   // Parse header
   for(;;) {
      token = hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid)    return errpf("unsupported format", "Unsupported HDR format");

   // Parse width and height
   // can't use sscanf() if we're not using stdio!
   token = hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3))  return errpf("unsupported data layout", "Unsupported HDR format");
   token += 3;
   height = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3))  return errpf("unsupported data layout", "Unsupported HDR format");
   token += 3;
   width = (int) strtol(token, NULL, 10);

   if (height > STBI_MAX_DIMENSIONS) return errpf("too large","Very large image (corrupt?)");
   if (width > STBI_MAX_DIMENSIONS) return errpf("too large","Very large image (corrupt?)");

   *x = width;
   *y = height;

   if (comp) *comp = 3;
   if (req_comp == 0) req_comp = 3;

   if (!mad4sizes_valid(width, height, req_comp, sizeof(float), 0))
      return errpf("too large", "HDR image is too large");

   // Read data
   hdr_data = (float *) malloc_mad4(width, height, req_comp, sizeof(float), 0);
   if (!hdr_data)
      return errpf("outofmem", "Out of memory");

   // Load image data
   // image data is stored as some number of sca
   if ( width < 8 || width >= 32768) {
      // Read flat data
      for (j=0; j < height; ++j) {
         for (i=0; i < width; ++i) {
            uc rgbe[4];
           main_decode_loop:
            getn(s, rgbe, 4);
            hdr_convert(hdr_data + j * width * req_comp + i * req_comp, rgbe, req_comp);
         }
      }
   } else {
      // Read RLE-encoded data
      scanline = NULL;

      for (j = 0; j < height; ++j) {
         c1 = get8(s);
         c2 = get8(s);
         len = get8(s);
         if (c1 != 2 || c2 != 2 || (len & 0x80)) {
            // not run-length encoded, so we have to actually use THIS data as a decoded
            // pixel (note this can't be a valid pixel--one of RGB must be >= 128)
            uc rgbe[4];
            rgbe[0] = (uc) c1;
            rgbe[1] = (uc) c2;
            rgbe[2] = (uc) len;
            rgbe[3] = (uc) get8(s);
            hdr_convert(hdr_data, rgbe, req_comp);
            i = 1;
            j = 0;
            free(scanline);
            goto main_decode_loop; // yes, this makes no sense
         }
         len <<= 8;
         len |= get8(s);
         if (len != width) { free(hdr_data); free(scanline); return errpf("invalid decoded scanline length", "corrupt HDR"); }
         if (scanline == NULL) {
            scanline = (uc *) malloc_mad2(width, 4, 0);
            if (!scanline) {
               free(hdr_data);
               return errpf("outofmem", "Out of memory");
            }
         }

         for (k = 0; k < 4; ++k) {
            int nleft;
            i = 0;
            while ((nleft = width - i) > 0) {
               count = get8(s);
               if (count > 128) {
                  // Run
                  value = get8(s);
                  count -= 128;
                  if ((count == 0) || (count > nleft)) { free(hdr_data); free(scanline); return errpf("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = value;
               } else {
                  // Dump
                  if ((count == 0) || (count > nleft)) { free(hdr_data); free(scanline); return errpf("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = get8(s);
               }
            }
         }
         for (i=0; i < width; ++i)
            hdr_convert(hdr_data+(j*width + i)*req_comp, scanline + i*4, req_comp);
      }
      if (scanline)
         free(scanline);
   }

   return hdr_data;
}

static int hdr_info(context *s, int *x, int *y, int *comp) noexcept
{
   char buffer[STBI__HDR_BUFLEN];
   char *token;
   int valid = 0;
   int dummy;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (hdr_test(s) == 0) {
       rewind( s );
       return 0;
   }

   for(;;) {
      token = hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid) {
       rewind( s );
       return 0;
   }
   token = hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3)) {
       rewind( s );
       return 0;
   }
   token += 3;
   *y = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3)) {
       rewind( s );
       return 0;
   }
   token += 3;
   *x = (int) strtol(token, NULL, 10);
   *comp = 3;
   return 1;
}

inline int HdrFormatModule::Test(context *s) noexcept
{
   return hdr_test(s);
}

inline void *HdrFormatModule::LoadAsLdr(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   float *hdr = hdr_load(s, x, y, comp, req_comp, ri);
   return hdr_to_ldr(hdr, *x, *y, req_comp ? req_comp : *comp);
}

inline int HdrFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return hdr_info(s, x, y, comp);
}
#endif // STBI_NO_HDR

} // namespace core
} // namespace detail
} // namespace stbi
