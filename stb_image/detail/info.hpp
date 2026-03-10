namespace stbi { namespace detail { namespace core {

static int info_main(context *s, int *x, int *y, int *comp) noexcept
{
   #ifndef STBI_NO_JPEG
   if (JpegFormatModule::Info(s, x, y, comp)) return 1;
   #endif

   #ifndef STBI_NO_PNG
   if (PngFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_GIF
   if (GifFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_BMP
   if (BmpFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PSD
   if (PsdFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PIC
   if (PicFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_PNM
   if (PnmFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   #ifndef STBI_NO_HDR
   if (HdrFormatModule::Info(s, x, y, comp))  return 1;
   #endif

   // test tga last because it's a crappy test!
   #ifndef STBI_NO_TGA
   if (TgaFormatModule::Info(s, x, y, comp))
       return 1;
   #endif
   return err("unknown image type", "Image not of any known type, or corrupt");
}

static int is_16_main(context *s) noexcept
{
   #ifndef STBI_NO_PNG
   if (PngFormatModule::Is16(s))  return 1;
   #endif

   #ifndef STBI_NO_PSD
   if (PsdFormatModule::Is16(s))  return 1;
   #endif

   #ifndef STBI_NO_PNM
   if (PnmFormatModule::Is16(s))  return 1;
   #endif
   return 0;
}

#ifndef STBI_NO_STDIO
STBIDEF int info(char const *filename, int *x, int *y, int *comp) noexcept
{
    FILE *f = fopen(filename, "rb");
    int result;
    if (!f) return err("can't fopen", "Unable to open file");
    result = info_from_file(f, x, y, comp);
    fclose(f);
    return result;
}

STBIDEF int info_from_file(FILE *f, int *x, int *y, int *comp) noexcept
{
   int r;
   context s;
   long pos = ftell(f);
   start_file(&s, f);
   r = info_main(&s,x,y,comp);
   fseek(f,pos,SEEK_SET);
   return r;
}

STBIDEF int is_16_bit(char const *filename) noexcept
{
    FILE *f = fopen(filename, "rb");
    int result;
    if (!f) return err("can't fopen", "Unable to open file");
    result = is_16_bit_from_file(f);
    fclose(f);
    return result;
}

STBIDEF int is_16_bit_from_file(FILE *f) noexcept
{
   int r;
   context s;
   long pos = ftell(f);
   start_file(&s, f);
   r = is_16_main(&s);
   fseek(f,pos,SEEK_SET);
   return r;
}
#endif // !STBI_NO_STDIO

STBIDEF int info_from_memory(uc const *buffer, int len, int *x, int *y, int *comp) noexcept
{
   context s;
   start_mem(&s,buffer,len);
   return info_main(&s,x,y,comp);
}

STBIDEF int info_from_callbacks(io_callbacks const *c, void *user, int *x, int *y, int *comp) noexcept
{
   context s;
   start_callbacks(&s, (io_callbacks *) c, user);
   return info_main(&s,x,y,comp);
}

STBIDEF int is_16_bit_from_memory(uc const *buffer, int len) noexcept
{
   context s;
   start_mem(&s,buffer,len);
   return is_16_main(&s);
}

STBIDEF int is_16_bit_from_callbacks(io_callbacks const *c, void *user) noexcept
{
   context s;
   start_callbacks(&s, (io_callbacks *) c, user);
   return is_16_main(&s);
}

} // namespace core
} // namespace detail
} // namespace stbi
