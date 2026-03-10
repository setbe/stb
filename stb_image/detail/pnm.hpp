namespace stbi { namespace detail { namespace core {

// Portable Gray Map and Portable Pixel Map loader
// by Ken Miller
//
// PGM: http://netpbm.sourceforge.net/doc/pgm.html
// PPM: http://netpbm.sourceforge.net/doc/ppm.html
//
// Known limitations:
//    Does not support comments in the header section
//    Does not support ASCII image data (formats P2 and P3)

#ifndef STBI_NO_PNM

static int      pnm_test(context *s) noexcept
{
   char p, t;
   p = (char) get8(s);
   t = (char) get8(s);
   if (p != 'P' || (t != '5' && t != '6')) {
       rewind( s );
       return 0;
   }
   return 1;
}

static void *pnm_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   uc *out;
   STBI_NOTUSED(ri);

   ri->bits_per_channel = pnm_info(s, (int *)&s->x, (int *)&s->y, (int *)&s->n);
   if (ri->bits_per_channel == 0)
      return 0;

   if (s->y > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");
   if (s->x > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");

   *x = s->x;
   *y = s->y;
   if (comp) *comp = s->n;

   if (!mad4sizes_valid(s->n, s->x, s->y, ri->bits_per_channel / 8, 0))
      return errpuc("too large", "PNM too large");

   out = (uc *) malloc_mad4(s->n, s->x, s->y, ri->bits_per_channel / 8, 0);
   if (!out) return errpuc("outofmem", "Out of memory");
   if (!getn(s, out, s->n * s->x * s->y * (ri->bits_per_channel / 8))) {
      free(out);
      return errpuc("bad PNM", "PNM file truncated");
   }

   if (req_comp && req_comp != s->n) {
      if (ri->bits_per_channel == 16) {
         out = (uc *) convert_format16((uint16 *) out, s->n, req_comp, s->x, s->y);
      } else {
         out = convert_format(out, s->n, req_comp, s->x, s->y);
      }
      if (out == NULL) return out; // convert_format frees input on failure
   }
   return out;
}

static int      pnm_isspace(char c) noexcept
{
   return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void     pnm_skip_whitespace(context *s, char *c) noexcept
{
   for (;;) {
      while (!at_eof(s) && pnm_isspace(*c))
         *c = (char) get8(s);

      if (at_eof(s) || *c != '#')
         break;

      while (!at_eof(s) && *c != '\n' && *c != '\r' )
         *c = (char) get8(s);
   }
}

static int      pnm_isdigit(char c) noexcept
{
   return c >= '0' && c <= '9';
}

static int      pnm_getinteger(context *s, char *c) noexcept
{
   int value = 0;

   while (!at_eof(s) && pnm_isdigit(*c)) {
      value = value*10 + (*c - '0');
      *c = (char) get8(s);
      if((value > 214748364) || (value == 214748364 && *c > '7'))
          return err("integer parse overflow", "Parsing an integer in the PPM header overflowed a 32-bit int");
   }

   return value;
}

static int      pnm_info(context *s, int *x, int *y, int *comp) noexcept
{
   int maxv, dummy;
   char c, p, t;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   rewind(s);

   // Get identifier
   p = (char) get8(s);
   t = (char) get8(s);
   if (p != 'P' || (t != '5' && t != '6')) {
       rewind(s);
       return 0;
   }

   *comp = (t == '6') ? 3 : 1;  // '5' is 1-component .pgm; '6' is 3-component .ppm

   c = (char) get8(s);
   pnm_skip_whitespace(s, &c);

   *x = pnm_getinteger(s, &c); // read width
   if(*x == 0)
       return err("invalid width", "PPM image header had zero or overflowing width");
   pnm_skip_whitespace(s, &c);

   *y = pnm_getinteger(s, &c); // read height
   if (*y == 0)
       return err("invalid width", "PPM image header had zero or overflowing width");
   pnm_skip_whitespace(s, &c);

   maxv = pnm_getinteger(s, &c);  // read max value
   if (maxv > 65535)
      return err("max value > 65535", "PPM image supports only 8-bit and 16-bit images");
   else if (maxv > 255)
      return 16;
   else
      return 8;
}

static int pnm_is16(context *s) noexcept
{
   if (pnm_info(s, NULL, NULL, NULL) == 16)
	   return 1;
   return 0;
}

inline int PnmFormatModule::Test(context *s) noexcept
{
   return pnm_test(s);
}

inline void *PnmFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   return pnm_load(s, x, y, comp, req_comp, ri);
}

inline int PnmFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return pnm_info(s, x, y, comp);
}

inline int PnmFormatModule::Is16(context *s) noexcept
{
   return pnm_is16(s);
}
#endif

} // namespace core
} // namespace detail
} // namespace stbi
