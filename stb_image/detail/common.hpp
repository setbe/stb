
namespace stbi { 
namespace detail { 
namespace core {
#if defined(STBI_ONLY_JPEG) || defined(STBI_ONLY_PNG) || defined(STBI_ONLY_BMP)  || defined(STBI_ONLY_TGA) || defined(STBI_ONLY_GIF) || defined(STBI_ONLY_PSD)  || defined(STBI_ONLY_HDR) || defined(STBI_ONLY_PIC) || defined(STBI_ONLY_PNM)  || defined(STBI_ONLY_ZLIB)   
#ifndef STBI_ONLY_JPEG   
#define STBI_NO_JPEG   
#endif   
#ifndef STBI_ONLY_PNG   
#define STBI_NO_PNG   
#endif   
#ifndef STBI_ONLY_BMP   
#define STBI_NO_BMP   
#endif   
#ifndef STBI_ONLY_PSD   
#define STBI_NO_PSD   
#endif   
#ifndef STBI_ONLY_TGA   
#define STBI_NO_TGA   
#endif   
#ifndef STBI_ONLY_GIF   
#define STBI_NO_GIF   
#endif   
#ifndef STBI_ONLY_HDR   
#define STBI_NO_HDR   
#endif   
#ifndef STBI_ONLY_PIC   
#define STBI_NO_PIC   
#endif   
#ifndef STBI_ONLY_PNM   
#define STBI_NO_PNM   
#endif
#endif
#if defined(STBI_NO_PNG) && !defined(STBI_SUPPORT_ZLIB) && !defined(STBI_NO_ZLIB)
#define STBI_NO_ZLIB
#endif
#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR)
#include <math.h>  // ldexp, pow
#endif
#ifndef STBI_NO_STDIO
#include <stdio.h>
#endif
#ifndef STBI_ASSERT
#include <assert.h>
#define STBI_ASSERT(x) assert(x)
#endif
#define STBI_EXTERN 
extern
#ifndef _MSC_VER   
#ifdef __cplusplus   
#define inline 
inline   
#else   
#define inline   
#endif
#else   
#define inline __forceinline
#endif
#ifndef STBI_NO_THREAD_LOCALS   
#if defined(__cplusplus) &&  __cplusplus >= 201103L      
#define STBI_THREAD_LOCAL       thread_local   
#elif defined(__GNUC__) && __GNUC__ < 5      
#define STBI_THREAD_LOCAL       __thread   
#elif defined(_MSC_VER)      
#define STBI_THREAD_LOCAL       __declspec(thread)   
#elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)      
#define STBI_THREAD_LOCAL       _Thread_local   
#endif   
#ifndef STBI_THREAD_LOCAL      
#if defined(__GNUC__)        
#define STBI_THREAD_LOCAL       __thread      
#endif   
#endif
#endif
#if defined(_MSC_VER) || defined(__SYMBIAN32__)
typedef unsigned short uint16;
typedef   signed short int16;
typedef unsigned int   uint32;
typedef   signed int   int32;
#else
#include <stdint.h>
typedef uint16_t uint16;
typedef int16_t  int16;
typedef uint32_t uint32;
typedef int32_t  int32;

// should produce compiler error if size is wrongtypedef unsigned char validate_uint32[sizeof(uint32)==4 ? 1 : -1];
#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif
#define lrot(x,y)  (((x) << (y)) | ((x) >> (-(y) & 31)))
static 
inline void *malloc(size_t size) noexcept ;
static 
inline void *realloc_sized(void *ptr, size_t old_size, size_t new_size) noexcept ;
static 
inline void  free(void *ptr) noexcept ;// x86/x64 detection
#if defined(__x86_64__) || defined(_M_X64)
#define STBI__X64_TARGET
#elif defined(__i386) || defined(_M_IX86)
#define STBI__X86_TARGET
#endif
#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)// gcc doesn't support sse2 intrinsics unless you compile with -msse2,// which in turn means it gets to use SSE2 everywhere. This is unfortunate,// but previous attempts to provide the SSE2 functions with runtime// detection caused numerous issues. The way architecture extensions are// exposed in GCC/Clang is, sadly, not really suited for one-file libs.// New behavior: if compiled with -msse2, we use SSE2 without any// detection; if not, we don't use it at all.
#define STBI_NO_SIMD
#endif
#if defined(__MINGW32__) && defined(STBI__X86_TARGET) && !defined(STBI_MINGW_ENABLE_SSE2) && !defined(STBI_NO_SIMD)// Note that __MINGW32__ doesn't actually mean 32-bit, so we have to avoid STBI__X64_TARGET//// 32-bit MinGW wants ESP to be 16-byte aligned, but this is not in the// Windows ABI and VC++ as well as Windows DLLs don't maintain that invariant.// As a result, enabling SSE2 on 32-bit MinGW is dangerous when not// simultaneously enabling "-mstackrealign".//// See https://github.com/nothings/stb/issues/81 for more information.//// So default to no SSE2 on 32-bit MinGW. If you've read this far and added// -mstackrealign to your build settings, feel free to 
#define STBI_MINGW_ENABLE_SSE2.
#define STBI_NO_SIMD
#endif
#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>
#ifdef _MSC_VER
#if _MSC_VER >= 1400  // not VC6
#include <intrin.h> // __cpuidstatic int cpuid3(void){   int info[4];   __cpuid(info,1);   return info[3];}

static int cpuid3(void){   int res;   __asm {      mov  eax,1      cpuid      mov  res,edx   }   return res;}
#endif
#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name
#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int sse2_available(void){   int info3 = cpuid3();   return ((info3 >> 26) & 1) != 0;}
#endif
#else // assume GCC-style if not VC++
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
static int sse2_available(void){   // If we're even attempting to compile this on GCC/Clang, that means   // -msse2 is on, which means the compiler is allowed to use SSE2   // instructions at will, and so are we.   return 1;}
#endif
#endif

// ARM NEON
#if defined(STBI_NO_SIMD) && defined(STBI_NEON)
#undef STBI_NEON
#endif
#ifdef STBI_NEON
#include <arm_neon.h>
#ifdef _MSC_VER
#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name
#else
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))
#endif
#endif
#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif
#ifndef STBI_MAX_DIMENSIONS
#define STBI_MAX_DIMENSIONS (1 << 24)

///////////////////////////////////////////////////  context 
struct and start_xxx functions// context structure is our basic context used by all images, so it// contains all the IO context, plus some basic image informationtypedef 
struct{   uint32 x, y;   int n, out_n;   io_callbacks io;   void *io_user_data;   int read_from_callbacks;   int buflen;   uc buffer_start[128];   int callback_already_read;   uc *buffer, *buffer_end;   uc *buffer_original, *buffer_original_end;} context;
static void refill_buffer(context *s) noexcept ;// initialize a memory-decode contextstatic void start_mem(context *s, uc const *buffer, int len){   s->io.read = NULL;   s->read_from_callbacks = 0;   s->callback_already_read = 0;   s->buffer = s->buffer_original = (uc *) buffer;   s->buffer_end = s->buffer_original_end = (uc *) buffer+len;}// initialize a callback-based contextstatic void start_callbacks(context *s, io_callbacks *c, void *user){   s->io = *c;   s->io_user_data = user;   s->buflen = sizeof(s->buffer_start);   s->read_from_callbacks = 1;   s->callback_already_read = 0;   s->buffer = s->buffer_original = s->buffer_start;   refill_buffer(s);   s->buffer_original_end = s->buffer_end;}
#ifndef STBI_NO_STDIOstatic int stdio_read(void *user, char *data, int size){   return (int) fread(data,1,size,(FILE*) user);}
static void stdio_skip(void *user, int n){   int ch;   fseek((FILE*) user, n, SEEK_CUR);   ch = fgetc((FILE*) user);  /* have to read a byte to reset feof()'s flag */   if (ch != EOF) {      ungetc(ch, (FILE *) user);  /* push byte back onto stream if valid. */   }}
static int stdio_eof(void *user){   return feof((FILE*) user) || ferror((FILE *) user);}
static io_callbacks stdio_callbacks ={   stdio_read,   stdio_skip,   stdio_eof,};
static void start_file(context *s, FILE *f){   start_callbacks(s, &stdio_callbacks, (void *) f);}//
static void stop_file(context *s) { }
#endif // !STBI_NO_STDIOstatic void rewind(context *s){   // conceptually rewind SHOULD rewind to the beginning of the stream,   // but we just rewind to the beginning of the initial buffer, because   // we only use it after doing 'test', which only ever looks at at most 92 bytes   s->buffer = s->buffer_original;   s->buffer_end = s->buffer_original_end;}
enum{   STBI_ORDER_RGB,   STBI_ORDER_BGR};
typedef 
struct{   int bits_per_channel;   int num_channels;   int channel_order;} result_info;
#ifndef STBI_NO_JPEGstatic int      jpeg_test(context *s) noexcept ;
static void    *jpeg_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      jpeg_info(context *s, int *x, int *y, int *comp) noexcept ;
#endif
#ifndef STBI_NO_PNGstatic int      png_test(context *s) noexcept ;
static void    *png_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      png_info(context *s, int *x, int *y, int *comp) noexcept ;
static int      png_is16(context *s) noexcept ;
#endif
#ifndef STBI_NO_BMPstatic int      bmp_test(context *s) noexcept ;
static void    *bmp_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      bmp_info(context *s, int *x, int *y, int *comp) noexcept ;
#endif
#ifndef STBI_NO_TGAstatic int      tga_test(context *s) noexcept ;
static void    *tga_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      tga_info(context *s, int *x, int *y, int *comp) noexcept ;
#endif
#ifndef STBI_NO_PSDstatic int      psd_test(context *s) noexcept ;
static void    *psd_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc) noexcept ;
static int      psd_info(context *s, int *x, int *y, int *comp) noexcept ;
static int      psd_is16(context *s) noexcept ;
#endif
#ifndef STBI_NO_HDRstatic int      hdr_test(context *s) noexcept ;
static float   *hdr_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      hdr_info(context *s, int *x, int *y, int *comp) noexcept ;
#endif
#ifndef STBI_NO_PICstatic int      pic_test(context *s) noexcept ;
static void    *pic_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      pic_info(context *s, int *x, int *y, int *comp) noexcept ;
#endif
#ifndef STBI_NO_GIFstatic int      gif_test(context *s) noexcept ;
static void    *gif_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static void    *load_gif_main(context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp) noexcept ;
static int      gif_info(context *s, int *x, int *y, int *comp) noexcept ;
#endif
#ifndef STBI_NO_PNMstatic int      pnm_test(context *s) noexcept ;
static void    *pnm_load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;
static int      pnm_info(context *s, int *x, int *y, int *comp) noexcept ;
static int      pnm_is16(context *s) noexcept ;
#endifstatic
#ifdef STBI_THREAD_LOCALSTBI_THREAD_LOCAL
#endifconst char *g_failure_reason;
STBIDEF const char *failure_reason(void){   return g_failure_reason;}
#ifndef STBI_NO_FAILURE_STRINGSstatic int err(const char *str){   g_failure_reason = str;   return 0;}
#endifstatic void *malloc(size_t size){    return malloc(size);}// stb_image uses ints pervasively, including for offset calculations.// therefore the largest decoded image size we can support with the// current code, even on 64-bit targets, is INT_MAX. this is not a// significant limitation for the intended use case.//// we do, however, need to make sure our size calculations don't// overflow. hence a few helper functions for size calculations that// multiply integers together, making sure that they're non-negative// and no overflow occurs.// return 1 if the sum is valid, 0 on overflow.// negative terms are considered invalid.
static int addsizes_valid(int a, int b){   if (b < 0) return 0;   // now 0 <= b <= INT_MAX, hence also   // 0 <= INT_MAX - b <= INTMAX.   // And "a + b <= INT_MAX" (which might overflow) is the   // same as a <= INT_MAX - b (no overflow)   return a <= INT_MAX - b;}// returns 1 if the product is valid, 0 on overflow.// negative factors are considered invalid.
static int mul2sizes_valid(int a, int b){   if (a < 0 || b < 0) return 0;   if (b == 0) return 1; // mul-by-0 is always safe   // portable way to check for no overflows in a*b   return a <= INT_MAX/b;}
#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflowstatic int mad2sizes_valid(int a, int b, int add){   return mul2sizes_valid(a, b) && addsizes_valid(a*b, add);}

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflowstatic int mad3sizes_valid(int a, int b, int c, int add){   return mul2sizes_valid(a, b) && mul2sizes_valid(a*b, c) &&      addsizes_valid(a*b*c, add);}// returns 1 if "a*b*c*d + add" has no negative terms/factors and doesn't overflow
#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static int mad4sizes_valid(int a, int b, int c, int d, int add){   return mul2sizes_valid(a, b) && mul2sizes_valid(a*b, c) &&      mul2sizes_valid(a*b*c, d) && addsizes_valid(a*b*c*d, add);}
#endif
#if !defined(STBI_NO_JPEG) || !defined(STBI_NO_PNG) || !defined(STBI_NO_TGA) || !defined(STBI_NO_HDR)// mallocs with size overflow checkingstatic void *malloc_mad2(int a, int b, int add){   if (!mad2sizes_valid(a, b, add)) return NULL;   return malloc(a*b + add);}
#endifstatic void *malloc_mad3(int a, int b, int c, int add){   if (!mad3sizes_valid(a, b, c, add)) return NULL;   return malloc(a*b*c + add);}
#if !defined(STBI_NO_LINEAR) || !defined(STBI_NO_HDR) || !defined(STBI_NO_PNM)
static void *malloc_mad4(int a, int b, int c, int d, int add){   if (!mad4sizes_valid(a, b, c, d, add)) return NULL;   return malloc(a*b*c*d + add);}

// returns 1 if the sum of two signed ints is valid (between -2^31 and 2^31-1 inclusive), 0 on overflow.
static int addints_valid(int a, int b){   if ((a >= 0) != (b >= 0)) return 1; // a and b have different signs, so no overflow   if (a < 0 && b < 0) return a >= INT_MIN - b; // same as a + b >= INT_MIN; INT_MIN - b cannot overflow since b < 0.   return a <= INT_MAX - b;}// returns 1 if the product of two ints fits in a signed short, 0 on overflow.
static int mul2shorts_valid(int a, int b){   if (b == 0 || b == -1) return 1; // multiplication by 0 is always 0; check for -1 so SHRT_MIN/b doesn't overflow   if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX/b; // product is positive, so similar to mul2sizes_valid   if (b < 0) return a <= SHRT_MIN / b; // same as a * b >= SHRT_MIN   return a >= SHRT_MIN / b;}// err - error// errpf - error returning pointer to float// errpuc - error returning pointer to unsigned char
#ifdef STBI_NO_FAILURE_STRINGS   
#define err(x,y)  0
#elif defined(STBI_FAILURE_USERMSG)   
#define err(x,y)  err(y)
#else   
#define err(x,y)  err(x)
#endif
#define errpf(x,y)   ((float *)(size_t) (err(x,y)?NULL:NULL))
#define errpuc(x,y)  ((unsigned char *)(size_t) (err(x,y)?NULL:NULL))
STBIDEF void image_free(void *retval_from_stbi_load){   free(retval_from_stbi_load);}
#ifndef STBI_NO_LINEARstatic float   *ldr_to_hdr(uc *data, int x, int y, int comp) noexcept ;
#endif
#ifndef STBI_NO_HDRstatic uc *hdr_to_ldr(float   *data, int x, int y, int comp) noexcept ;
#endifstatic int vertically_flip_on_load_global = 0;
STBIDEF void set_flip_vertically_on_load(int flag_true_if_should_flip){   vertically_flip_on_load_global = flag_true_if_should_flip;}
#ifndef STBI_THREAD_LOCAL
#define vertically_flip_on_load  vertically_flip_on_load_global

static STBI_THREAD_LOCAL int vertically_flip_on_load_local, vertically_flip_on_load_set;
STBIDEF void set_flip_vertically_on_load_thread(int flag_true_if_should_flip){   vertically_flip_on_load_local = flag_true_if_should_flip;   vertically_flip_on_load_set = 1;}
#define vertically_flip_on_load  (vertically_flip_on_load_set       \                                         ? vertically_flip_on_load_local  \                                         : vertically_flip_on_load_global)
#endif // STBI_THREAD_LOCAL// OOP dispatch layer for per-format routing in load/info/is16 entry points.
#ifndef STBI_NO_PNGstruct PngFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;   
static int Is16(context *s) noexcept ;};
#endif
#ifndef STBI_NO_BMPstruct BmpFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;};
#endif
#ifndef STBI_NO_GIFstruct GifFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;};
#endif
#ifndef STBI_NO_PSDstruct PsdFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;   
static int Is16(context *s) noexcept ;};
#endif
#ifndef STBI_NO_PICstruct PicFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;};
#endif
#ifndef STBI_NO_JPEGstruct JpegFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;};
#endif
#ifndef STBI_NO_PNMstruct PnmFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;   
static int Is16(context *s) noexcept ;};
#endif
#ifndef STBI_NO_HDRstruct HdrFormatModule {   
static int Test(context *s) noexcept ;   
static void *LoadAsLdr(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;};
#endif
#ifndef STBI_NO_TGAstruct TgaFormatModule {   
static int Test(context *s) noexcept ;   
static void *Load(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri) noexcept ;   
static int Info(context *s, int *x, int *y, int *comp) noexcept ;};
#endifstatic void *load_main(context *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc){   memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields   ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed   ri->channel_order = STBI_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order   ri->num_channels = 0;   // test the formats with a very explicit header first (at least a FOURCC   // or distinctive magic number first)   
#ifndef STBI_NO_PNG   if (PngFormatModule::Test(s))  return PngFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   
#ifndef STBI_NO_BMP   if (BmpFormatModule::Test(s))  return BmpFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   
#ifndef STBI_NO_GIF   if (GifFormatModule::Test(s))  return GifFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   
#ifndef STBI_NO_PSD   if (PsdFormatModule::Test(s))  return PsdFormatModule::Load(s, x, y, comp, req_comp, ri, bpc);   
#else   STBI_NOTUSED(bpc);   
#endif   
#ifndef STBI_NO_PIC   if (PicFormatModule::Test(s))  return PicFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   // then the formats that can end up attempting to load with just 1 or 2   // bytes matching expectations; these are prone to false positives, so   // try them later   
#ifndef STBI_NO_JPEG   if (JpegFormatModule::Test(s)) return JpegFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   
#ifndef STBI_NO_PNM   if (PnmFormatModule::Test(s))  return PnmFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   
#ifndef STBI_NO_HDR   if (HdrFormatModule::Test(s)) return HdrFormatModule::LoadAsLdr(s, x, y, comp, req_comp, ri);   
#endif   
#ifndef STBI_NO_TGA   // test tga last because it's a crappy test!   if (TgaFormatModule::Test(s))      return TgaFormatModule::Load(s, x, y, comp, req_comp, ri);   
#endif   return errpuc("unknown image type", "Image not of any known type, or corrupt");}
static uc *convert_16_to_8(uint16 *orig, int w, int h, int channels){   int i;   int len = w * h * channels;   uc *reduced;   reduced = (uc *) malloc(len);   if (reduced == NULL) return errpuc("outofmem", "Out of memory");   for (i = 0; i < len; ++i)      reduced[i] = (uc)((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling   free(orig);   return reduced;}
static uint16 *convert_8_to_16(uc *orig, int w, int h, int channels){   int i;   int len = w * h * channels;   uint16 *enlarged;   enlarged = (uint16 *) malloc(len*2);   if (enlarged == NULL) return (uint16 *) errpuc("outofmem", "Out of memory");   for (i = 0; i < len; ++i)      enlarged[i] = (uint16)((orig[i] << 8) + orig[i]); // replicate to high and low byte, maps 0->0, 255->0xffff   free(orig);   return enlarged;}
static void vertical_flip(void *image, int w, int h, int bytes_per_pixel){   int row;   size_t bytes_per_row = (size_t)w * bytes_per_pixel;   uc temp[2048];   uc *bytes = (uc *)image;   for (row = 0; row < (h>>1); row++) {      uc *row0 = bytes + row*bytes_per_row;      uc *row1 = bytes + (h - row - 1)*bytes_per_row;      // swap row0 with row1      size_t bytes_left = bytes_per_row;      while (bytes_left) {         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);         memcpy(temp, row0, bytes_copy);         memcpy(row0, row1, bytes_copy);         memcpy(row1, temp, bytes_copy);         row0 += bytes_copy;         row1 += bytes_copy;         bytes_left -= bytes_copy;      }   }}
#ifndef STBI_NO_GIFstatic void vertical_flip_slices(void *image, int w, int h, int z, int bytes_per_pixel){   int slice;   int slice_size = w * h * bytes_per_pixel;   uc *bytes = (uc *)image;   for (slice = 0; slice < z; ++slice) {      vertical_flip(bytes, w, h, bytes_per_pixel);      bytes += slice_size;   }}
#endifstatic unsigned char *load_and_postprocess_8bit(context *s, int *x, int *y, int *comp, int req_comp){   result_info ri;   void *result = load_main(s, x, y, comp, req_comp, &ri, 8);   if (result == NULL)      return NULL;   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.   STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);   if (ri.bits_per_channel != 8) {      result = convert_16_to_8((uint16 *) result, *x, *y, req_comp == 0 ? *comp : req_comp);      ri.bits_per_channel = 8;   }   // @TODO: move convert_format to here   if (vertically_flip_on_load) {      int channels = req_comp ? req_comp : *comp;      vertical_flip(result, *x, *y, channels * sizeof(uc));   }   return (unsigned char *) result;}
static uint16 *load_and_postprocess_16bit(context *s, int *x, int *y, int *comp, int req_comp){   result_info ri;   void *result = load_main(s, x, y, comp, req_comp, &ri, 16);   if (result == NULL)      return NULL;   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.   STBI_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);   if (ri.bits_per_channel != 16) {      result = convert_8_to_16((uc *) result, *x, *y, req_comp == 0 ? *comp : req_comp);      ri.bits_per_channel = 16;   }   // @TODO: move convert_format16 to here   // @TODO: special case RGB-to-Y (and RGBA-to-YA) for 8-bit-to-16-bit case to keep more precision   if (vertically_flip_on_load) {      int channels = req_comp ? req_comp : *comp;      vertical_flip(result, *x, *y, channels * sizeof(uint16));   }   return (uint16 *) result;}
#if !defined(STBI_NO_HDR) && !defined(STBI_NO_LINEAR)
static void float_postprocess(float *result, int *x, int *y, int *comp, int req_comp){   if (vertically_flip_on_load && result != NULL) {      int channels = req_comp ? req_comp : *comp;      vertical_flip(result, *x, *y, channels * sizeof(float));   }}
#endif
#ifndef STBI_NO_STDIO
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)STBI_EXTERN __declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int cp, unsigned long flags, const char *str, int cbmb, wchar_t *widestr, int cchwide);STBI_EXTERN __declspec(dllimport) int __stdcall WideCharToMultiByte(unsigned int cp, unsigned long flags, const wchar_t *widestr, int cchwide, char *str, int cbmb, const char *defchar, int *used_default);
#endif
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)
STBIDEF int convert_wchar_to_utf8(char *buffer, size_t bufferlen, const wchar_t* input){	return WideCharToMultiByte(65001 /* UTF8 */, 0, input, -1, buffer, (int) bufferlen, NULL, NULL);}
#endifstatic FILE *fopen(char const *filename, char const *mode){   FILE *f;
#if defined(_WIN32) && defined(STBI_WINDOWS_UTF8)   wchar_t wMode[64];   wchar_t wFilename[1024];	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, filename, -1, wFilename, sizeof(wFilename)/sizeof(*wFilename)))      return 0;	if (0 == MultiByteToWideChar(65001 /* UTF8 */, 0, mode, -1, wMode, sizeof(wMode)/sizeof(*wMode)))      return 0;
#if defined(_MSC_VER) && _MSC_VER >= 1400	if (0 != _wfopen_s(&f, wFilename, wMode))		f = 0;
#else   f = _wfopen(wFilename, wMode);
#endif
#elif defined(_MSC_VER) && _MSC_VER >= 1400   if (0 != fopen_s(&f, filename, mode))      f=0;
#else   f = fopen(filename, mode);
#endif   return f;}
STBIDEF uc *load(char const *filename, int *x, int *y, int *comp, int req_comp){   FILE *f = fopen(filename, "rb");   unsigned char *result;   if (!f) return errpuc("can't fopen", "Unable to open file");   result = load_from_file(f,x,y,comp,req_comp);   fclose(f);   return result;}
STBIDEF uc *load_from_file(FILE *f, int *x, int *y, int *comp, int req_comp){   unsigned char *result;   context s;   start_file(&s,f);   result = load_and_postprocess_8bit(&s,x,y,comp,req_comp);   if (result) {      // need to 'unget' all the characters in the IO buffer      fseek(f, - (int) (s.buffer_end - s.buffer), SEEK_CUR);   }   return result;}
STBIDEF uint16 *load_from_file_16(FILE *f, int *x, int *y, int *comp, int req_comp){   uint16 *result;   context s;   start_file(&s,f);   result = load_and_postprocess_16bit(&s,x,y,comp,req_comp);   if (result) {      // need to 'unget' all the characters in the IO buffer      fseek(f, - (int) (s.buffer_end - s.buffer), SEEK_CUR);   }   return result;}
STBIDEF us *load_16(char const *filename, int *x, int *y, int *comp, int req_comp){   FILE *f = fopen(filename, "rb");   uint16 *result;   if (!f) return (us *) errpuc("can't fopen", "Unable to open file");   result = load_from_file_16(f,x,y,comp,req_comp);   fclose(f);   return result;}
#endif //!STBI_NO_STDIOSTBIDEF us *load_16_from_memory(uc const *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels){   context s;   start_mem(&s,buffer,len);   return load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);}
STBIDEF us *load_16_from_callbacks(io_callbacks const *clbk, void *user, int *x, int *y, int *channels_in_file, int desired_channels){   context s;   start_callbacks(&s, (io_callbacks *)clbk, user);   return load_and_postprocess_16bit(&s,x,y,channels_in_file,desired_channels);}
STBIDEF uc *load_from_memory(uc const *buffer, int len, int *x, int *y, int *comp, int req_comp){   context s;   start_mem(&s,buffer,len);   return load_and_postprocess_8bit(&s,x,y,comp,req_comp);}
STBIDEF uc *load_from_callbacks(io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp){   context s;   start_callbacks(&s, (io_callbacks *) clbk, user);   return load_and_postprocess_8bit(&s,x,y,comp,req_comp);}
#ifndef STBI_NO_GIFSTBIDEF uc *load_gif_from_memory(uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp){   unsigned char *result;   context s;   start_mem(&s,buffer,len);   result = (unsigned char*) load_gif_main(&s, delays, x, y, z, comp, req_comp);   if (vertically_flip_on_load) {      vertical_flip_slices( result, *x, *y, *z, *comp );   }   return result;}
#endif
#ifndef STBI_NO_LINEARstatic float *loadf_main(context *s, int *x, int *y, int *comp, int req_comp){   unsigned char *data;   
#ifndef STBI_NO_HDR   if (hdr_test(s)) {      result_info ri;      float *hdr_data = hdr_load(s,x,y,comp,req_comp, &ri);      if (hdr_data)         float_postprocess(hdr_data,x,y,comp,req_comp);      return hdr_data;   }   
#endif   data = load_and_postprocess_8bit(s, x, y, comp, req_comp);   if (data)      return ldr_to_hdr(data, *x, *y, req_comp ? req_comp : *comp);   return errpf("unknown image type", "Image not of any known type, or corrupt");}
STBIDEF float *loadf_from_memory(uc const *buffer, int len, int *x, int *y, int *comp, int req_comp){   context s;   start_mem(&s,buffer,len);   return loadf_main(&s,x,y,comp,req_comp);}
STBIDEF float *loadf_from_callbacks(io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp){   context s;   start_callbacks(&s, (io_callbacks *) clbk, user);   return loadf_main(&s,x,y,comp,req_comp);}
#ifndef STBI_NO_STDIOSTBIDEF float *loadf(char const *filename, int *x, int *y, int *comp, int req_comp){   float *result;   FILE *f = fopen(filename, "rb");   if (!f) return errpf("can't fopen", "Unable to open file");   result = loadf_from_file(f,x,y,comp,req_comp);   fclose(f);   return result;}
STBIDEF float *loadf_from_file(FILE *f, int *x, int *y, int *comp, int req_comp){   context s;   start_file(&s,f);   return loadf_main(&s,x,y,comp,req_comp);}
#endif // !STBI_NO_STDIO
#endif // !STBI_NO_LINEAR// these is-hdr-or-not is defined independent of whether STBI_NO_LINEAR is// defined, for API simplicity; if STBI_NO_LINEAR is defined, it always// reports false!
STBIDEF int is_hdr_from_memory(uc const *buffer, int len){   
#ifndef STBI_NO_HDR   context s;   start_mem(&s,buffer,len);   return hdr_test(&s);   
#else   STBI_NOTUSED(buffer);   STBI_NOTUSED(len);   return 0;   
#endif}
#ifndef STBI_NO_STDIOSTBIDEF int      is_hdr          (char const *filename){   FILE *f = fopen(filename, "rb");   int result=0;   if (f) {      result = is_hdr_from_file(f);      fclose(f);   }   return result;}
STBIDEF int is_hdr_from_file(FILE *f){   
#ifndef STBI_NO_HDR   long pos = ftell(f);   int res;   context s;   start_file(&s,f);   res = hdr_test(&s);   fseek(f, pos, SEEK_SET);   return res;   
#else   STBI_NOTUSED(f);   return 0;   
#endif}
#endif // !STBI_NO_STDIOSTBIDEF int      is_hdr_from_callbacks(io_callbacks const *clbk, void *user){   
#ifndef STBI_NO_HDR   context s;   start_callbacks(&s, (io_callbacks *) clbk, user);   return hdr_test(&s);   
#else   STBI_NOTUSED(clbk);   STBI_NOTUSED(user);   return 0;   
#endif}
#ifndef STBI_NO_LINEARstatic float l2h_gamma=2.2f, l2h_scale=1.0f;
STBIDEF void   ldr_to_hdr_gamma(float gamma) { l2h_gamma = gamma; }
STBIDEF void   ldr_to_hdr_scale(float scale) { l2h_scale = scale; }
#endifstatic float h2l_gamma_i=1.0f/2.2f, h2l_scale_i=1.0f;
STBIDEF void   hdr_to_ldr_gamma(float gamma) { h2l_gamma_i = 1/gamma; }
STBIDEF void   hdr_to_ldr_scale(float scale) { h2l_scale_i = 1/scale; }////////////////////////////////////////////////////////////////////////////////// Common code used by all image loaders//
enum{   STBI__SCAN_load=0,   STBI__SCAN_type,   STBI__SCAN_header};
static void refill_buffer(context *s){   int n = (s->io.read)(s->io_user_data,(char*)s->buffer_start,s->buflen);   s->callback_already_read += (int) (s->buffer - s->buffer_original);   if (n == 0) {      // at end of file, treat same as if from memory, but need to handle case      // where s->buffer isn't pointing to safe memory, e.g. 0-byte file      s->read_from_callbacks = 0;      s->buffer = s->buffer_start;      s->buffer_end = s->buffer_start+1;      *s->buffer = 0;   } else {      s->buffer = s->buffer_start;      s->buffer_end = s->buffer_start + n;   }}inline 
static uc get8(context *s){   if (s->buffer < s->buffer_end)      return *s->buffer++;   if (s->read_from_callbacks) {      refill_buffer(s);      return *s->buffer++;   }   return 0;}
#if defined(STBI_NO_JPEG) && defined(STBI_NO_HDR) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)// nothing

inline 
static int at_eof(context *s){   if (s->io.read) {      if (!(s->io.eof)(s->io_user_data)) return 0;      // if feof() is true, check if buffer = end      // special case: we've only got the special 0 character at the end      if (s->read_from_callbacks == 0) return 1;   }   return s->buffer >= s->buffer_end;}
#endif
#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC)// nothing

static void skip(context *s, int n){   if (n == 0) return;  // already there!   if (n < 0) {      s->buffer = s->buffer_end;      return;   }   if (s->io.read) {      int blen = (int) (s->buffer_end - s->buffer);      if (blen < n) {         s->buffer = s->buffer_end;         (s->io.skip)(s->io_user_data, n - blen);         return;      }   }   s->buffer += n;}
#endif
#if defined(STBI_NO_PNG) && defined(STBI_NO_TGA) && defined(STBI_NO_HDR) && defined(STBI_NO_PNM)// nothing

static int getn(context *s, uc *buffer, int n){   if (s->io.read) {      int blen = (int) (s->buffer_end - s->buffer);      if (blen < n) {         int res, count;         memcpy(buffer, s->buffer, blen);         count = (s->io.read)(s->io_user_data, (char*) buffer + blen, n - blen);         res = (count == (n-blen));         s->buffer = s->buffer_end;         return res;      }   }   if (s->buffer+n <= s->buffer_end) {      memcpy(buffer, s->buffer, n);      s->buffer += n;      return 1;   } else      return 0;}
#endif
#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_PSD) && defined(STBI_NO_PIC)// nothing

static int get16be(context *s){   int z = get8(s);   return (z << 8) + get8(s);}
#endif
#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD) && defined(STBI_NO_PIC)// nothing

static uint32 get32be(context *s){   uint32 z = get16be(s);   return (z << 16) + get16be(s);}
#endif
#if defined(STBI_NO_BMP) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF)// nothing

static int get16le(context *s){   int z = get8(s);   return z + (get8(s) << 8);}
#endif
#ifndef STBI_NO_BMPstatic uint32 get32le(context *s){   uint32 z = get16le(s);   z += (uint32)get16le(s) << 16;   return z;}
#endif
#define STBI__BYTECAST(x)  ((uc) ((x) & 255))  // truncate int to byte without warnings
#if defined(STBI_NO_JPEG) && defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)// nothing

//////////////////////////////////////////////////////////////////////////////////  generic converter from built-in n to req_comp//    individual types do this automatically as much as possible (e.g. jpeg//    does all cases internally since it needs to colorspace convert anyway,//    and it never has alpha, so very few cases ). png can automatically//    interleave an alpha=255 channel, but falls back to this for other cases////  assume data buffer is malloced, so malloc a new one and free that one//  only failure mode is malloc failingstatic uc compute_y(int r, int g, int b){   return (uc) (((r*77) + (g*150) +  (29*b)) >> 8);}
#endif
#if defined(STBI_NO_PNG) && defined(STBI_NO_BMP) && defined(STBI_NO_PSD) && defined(STBI_NO_TGA) && defined(STBI_NO_GIF) && defined(STBI_NO_PIC) && defined(STBI_NO_PNM)// nothing

static unsigned char *convert_format(unsigned char *data, int n, int req_comp, unsigned int x, unsigned int y){   int i,j;   unsigned char *good;   if (req_comp == n) return data;   STBI_ASSERT(req_comp >= 1 && req_comp <= 4);   good = (unsigned char *) malloc_mad3(req_comp, x, y, 0);   if (good == NULL) {      free(data);      return errpuc("outofmem", "Out of memory");   }   for (j=0; j < (int) y; ++j) {      unsigned char *src  = data + j * x * n   ;      unsigned char *dest = good + j * x * req_comp;      
#define STBI__COMBO(a,b)  ((a)*8+(b))      
#define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)      // convert source image with n components to one with req_comp components;      // avoid switch per pixel, so use switch per scanline and massive macros      switch (STBI__COMBO(n, req_comp)) {         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;         STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;         STBI__CASE(3,1) { dest[0]=compute_y(src[0],src[1],src[2]);                   } break;         STBI__CASE(3,2) { dest[0]=compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;         STBI__CASE(4,1) { dest[0]=compute_y(src[0],src[1],src[2]);                   } break;         STBI__CASE(4,2) { dest[0]=compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;         default: STBI_ASSERT(0); free(data); free(good); return errpuc("unsupported", "Unsupported format conversion");      }      
#undef STBI__CASE   }   free(data);   return good;}
#endif
#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD)// nothing

static uint16 compute_y_16(int r, int g, int b){   return (uint16) (((r*77) + (g*150) +  (29*b)) >> 8);}
#endif
#if defined(STBI_NO_PNG) && defined(STBI_NO_PSD)// nothing

static uint16 *convert_format16(uint16 *data, int n, int req_comp, unsigned int x, unsigned int y){   int i,j;   uint16 *good;   if (req_comp == n) return data;   STBI_ASSERT(req_comp >= 1 && req_comp <= 4);   good = (uint16 *) malloc(req_comp * x * y * 2);   if (good == NULL) {      free(data);      return (uint16 *) errpuc("outofmem", "Out of memory");   }   for (j=0; j < (int) y; ++j) {      uint16 *src  = data + j * x * n   ;      uint16 *dest = good + j * x * req_comp;      
#define STBI__COMBO(a,b)  ((a)*8+(b))      
#define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)      // convert source image with n components to one with req_comp components;      // avoid switch per pixel, so use switch per scanline and massive macros      switch (STBI__COMBO(n, req_comp)) {         STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=0xffff;                                     } break;         STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;         STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=0xffff;                     } break;         STBI__CASE(2,1) { dest[0]=src[0];                                                     } break;         STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;         STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                     } break;         STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=0xffff;        } break;         STBI__CASE(3,1) { dest[0]=compute_y_16(src[0],src[1],src[2]);                   } break;         STBI__CASE(3,2) { dest[0]=compute_y_16(src[0],src[1],src[2]); dest[1] = 0xffff; } break;         STBI__CASE(4,1) { dest[0]=compute_y_16(src[0],src[1],src[2]);                   } break;         STBI__CASE(4,2) { dest[0]=compute_y_16(src[0],src[1],src[2]); dest[1] = src[3]; } break;         STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                       } break;         default: STBI_ASSERT(0); free(data); free(good); return (uint16*) errpuc("unsupported", "Unsupported format conversion");      }      
#undef STBI__CASE   }   free(data);   return good;}
#endif
#ifndef STBI_NO_LINEARstatic float   *ldr_to_hdr(uc *data, int x, int y, int comp){   int i,k,n;   float *output;   if (!data) return NULL;   output = (float *) malloc_mad4(x, y, comp, sizeof(float), 0);   if (output == NULL) { free(data); return errpf("outofmem", "Out of memory"); }   // compute number of non-alpha components   if (comp & 1) n = comp; else n = comp-1;   for (i=0; i < x*y; ++i) {      for (k=0; k < n; ++k) {         output[i*comp + k] = (float) (pow(data[i*comp+k]/255.0f, l2h_gamma) * l2h_scale);      }   }   if (n < comp) {      for (i=0; i < x*y; ++i) {         output[i*comp + n] = data[i*comp + n]/255.0f;      }   }   free(data);   return output;}
#endif
#ifndef STBI_NO_HDR
#define float2int(x)   ((int) (x))
static uc *hdr_to_ldr(float   *data, int x, int y, int comp){   int i,k,n;   uc *output;   if (!data) return NULL;   output = (uc *) malloc_mad3(x, y, comp, 0);   if (output == NULL) { free(data); return errpuc("outofmem", "Out of memory"); }   // compute number of non-alpha components   if (comp & 1) n = comp; else n = comp-1;   for (i=0; i < x*y; ++i) {      for (k=0; k < n; ++k) {         float z = (float) pow(data[i*comp+k]*h2l_scale_i, h2l_gamma_i) * 255 + 0.5f;         if (z < 0) z = 0;         if (z > 255) z = 255;         output[i*comp + k] = (uc) float2int(z);      }      if (k < comp) {         float z = data[i*comp+k] * 255 + 0.5f;         if (z < 0) z = 0;         if (z > 255) z = 255;         output[i*comp + k] = (uc) float2int(z);      }   }   free(data);   return output;}

////////////////////////////////////////////////////////////////////////////////} // 
namespace core} // 
namespace detail} // 
namespace stbi
