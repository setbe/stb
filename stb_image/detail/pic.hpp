namespace stbi { namespace detail { namespace core {

// Softimage PIC loader
// by Tom Seddon
//
// See http://softimage.wiki.softimage.com/index.php/INFO:_PIC_file_format
// See http://ozviz.wasp.uwa.edu.au/~pbourke/dataformats/softimagepic/

#ifndef STBI_NO_PIC
static int pic_is4(context *s,const char *str) noexcept
{
   int i;
   for (i=0; i<4; ++i)
      if (get8(s) != (uc)str[i])
         return 0;

   return 1;
}

static int pic_test_core(context *s) noexcept
{
   int i;

   if (!pic_is4(s,"\x53\x80\xF6\x34"))
      return 0;

   for(i=0;i<84;++i)
      get8(s);

   if (!pic_is4(s,"PICT"))
      return 0;

   return 1;
}

typedef struct
{
   uc size,type,channel;
} pic_packet;

static uc *readval(context *s, int channel, uc *dest) noexcept
{
   int mask=0x80, i;

   for (i=0; i<4; ++i, mask>>=1) {
      if (channel & mask) {
         if (at_eof(s)) return errpuc("bad file","PIC file too short");
         dest[i]=get8(s);
      }
   }

   return dest;
}

static void copyval(int channel,uc *dest,const uc *src) noexcept
{
   int mask=0x80,i;

   for (i=0;i<4; ++i, mask>>=1)
      if (channel&mask)
         dest[i]=src[i];
}

static uc *pic_load_core(context *s,int width,int height,int *comp, uc *result) noexcept
{
   int act_comp=0,num_packets=0,y,chained;
   pic_packet packets[10];

   // this will (should...) cater for even some bizarre stuff like having data
    // for the same channel in multiple packets.
   do {
      pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return errpuc("bad format","too many packets");

      packet = &packets[num_packets++];

      chained = get8(s);
      packet->size    = get8(s);
      packet->type    = get8(s);
      packet->channel = get8(s);

      act_comp |= packet->channel;

      if (at_eof(s))          return errpuc("bad file","file too short (reading packets)");
      if (packet->size != 8)  return errpuc("bad format","packet isn't 8bpp");
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3); // has alpha channel?

   for(y=0; y<height; ++y) {
      int packet_idx;

      for(packet_idx=0; packet_idx < num_packets; ++packet_idx) {
         pic_packet *packet = &packets[packet_idx];
         uc *dest = result+y*width*4;

         switch (packet->type) {
            default:
               return errpuc("bad format","packet has bad compression type");

            case 0: {//uncompressed
               int x;

               for(x=0;x<width;++x, dest+=4)
                  if (!readval(s,packet->channel,dest))
                     return 0;
               break;
            }

            case 1://Pure RLE
               {
                  int left=width, i;

                  while (left>0) {
                     uc count,value[4];

                     count=get8(s);
                     if (at_eof(s))   return errpuc("bad file","file too short (pure read count)");

                     if (count > left)
                        count = (uc) left;

                     if (!readval(s,packet->channel,value))  return 0;

                     for(i=0; i<count; ++i,dest+=4)
                        copyval(packet->channel,dest,value);
                     left -= count;
                  }
               }
               break;

            case 2: {//Mixed RLE
               int left=width;
               while (left>0) {
                  int count = get8(s), i;
                  if (at_eof(s))  return errpuc("bad file","file too short (mixed read count)");

                  if (count >= 128) { // Repeated
                     uc value[4];

                     if (count==128)
                        count = get16be(s);
                     else
                        count -= 127;
                     if (count > left)
                        return errpuc("bad file","scanline overrun");

                     if (!readval(s,packet->channel,value))
                        return 0;

                     for(i=0;i<count;++i, dest += 4)
                        copyval(packet->channel,dest,value);
                  } else { // Raw
                     ++count;
                     if (count>left) return errpuc("bad file","scanline overrun");

                     for(i=0;i<count;++i, dest+=4)
                        if (!readval(s,packet->channel,dest))
                           return 0;
                  }
                  left-=count;
               }
               break;
            }
         }
      }
   }

   return result;
}

static void *pic_load(context *s,int *px,int *py,int *comp,int req_comp, result_info *ri) noexcept
{
   uc *result;
   int i, x,y, internal_comp;
   STBI_NOTUSED(ri);

   if (!comp) comp = &internal_comp;

   for (i=0; i<92; ++i)
      get8(s);

   x = get16be(s);
   y = get16be(s);

   if (y > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");
   if (x > STBI_MAX_DIMENSIONS) return errpuc("too large","Very large image (corrupt?)");

   if (at_eof(s))  return errpuc("bad file","file too short (pic header)");
   if (!mad3sizes_valid(x, y, 4, 0)) return errpuc("too large", "PIC image too large to decode");

   get32be(s); //skip `ratio'
   get16be(s); //skip `fields'
   get16be(s); //skip `pad'

   // intermediate buffer is RGBA
   result = (uc *) malloc_mad3(x, y, 4, 0);
   if (!result) return errpuc("outofmem", "Out of memory");
   memset(result, 0xff, x*y*4);

   if (!pic_load_core(s,x,y,comp, result)) {
      free(result);
      result=0;
   }
   *px = x;
   *py = y;
   if (req_comp == 0) req_comp = *comp;
   result=convert_format(result,4,req_comp,x,y);

   return result;
}

static int pic_test(context *s) noexcept
{
   int r = pic_test_core(s);
   rewind(s);
   return r;
}
#endif

// *************************************************************************************************


#ifndef STBI_NO_PIC
static int pic_info(context *s, int *x, int *y, int *comp) noexcept
{
   int act_comp=0,num_packets=0,chained,dummy;
   pic_packet packets[10];

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (!pic_is4(s,"\x53\x80\xF6\x34")) {
      rewind(s);
      return 0;
   }

   skip(s, 88);

   *x = get16be(s);
   *y = get16be(s);
   if (at_eof(s)) {
      rewind( s);
      return 0;
   }
   if ( (*x) != 0 && (1 << 28) / (*x) < (*y)) {
      rewind( s );
      return 0;
   }

   skip(s, 8);

   do {
      pic_packet *packet;

      if (num_packets==sizeof(packets)/sizeof(packets[0]))
         return 0;

      packet = &packets[num_packets++];
      chained = get8(s);
      packet->size    = get8(s);
      packet->type    = get8(s);
      packet->channel = get8(s);
      act_comp |= packet->channel;

      if (at_eof(s)) {
          rewind( s );
          return 0;
      }
      if (packet->size != 8) {
          rewind( s );
          return 0;
      }
   } while (chained);

   *comp = (act_comp & 0x10 ? 4 : 3);

   return 1;
}

inline int PicFormatModule::Test(context *s) noexcept
{
   return pic_test(s);
}

inline void *PicFormatModule::Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept
{
   return pic_load(s, x, y, comp, req_comp, ri);
}

inline int PicFormatModule::Info(context *s, int *x, int *y, int *comp) noexcept
{
   return pic_info(s, x, y, comp);
}
#endif

// *************************************************************************************************

} // namespace core
} // namespace detail
} // namespace stbi
