/* stb_image_anim - v0.xx - public domain image loader - http://nothings.org/stb
                                  no warranty implied; use at your own risk

   Do this:
      #define STB_IMAGE_ANIM_IMPLEMENTATION
   before you include this file in *one* C or C++ file to create the implementation.

   // i.e. it should look like this:
   #include ...
   #include ...
   #include ...
   #define STB_IMAGE_ANIM_IMPLEMENTATION
   #include "stb_image_anim.h"

   You can #define STBIA_ASSERT(x) before the #include to avoid using assert.h.
   And #define STBIA_MALLOC, STBIA_REALLOC, and STBIA_FREE to avoid using malloc,realloc,free


   QUICK NOTES:
      Primarily of interest to game developers and other people who can
          avoid problematic images and only need the trivial interface

      Animated GIF (*comp always reports as 4-channel)
      APNG

      Animated GIF still needs a proper API, but here's one way to do it:
          http://gist.github.com/urraka/685d9a6340b26b830d49

      - decode from memory or through FILE (define STBIA_NO_STDIO to remove code)
      - decode from arbitrary I/O callbacks
      - SIMD acceleration on x86/x64 (SSE2) and ARM (NEON)

   Full documentation under "DOCUMENTATION" below.



CONTRIBUTORS

REVISIONS
  0.xx (2020-09-04) first internal version; extracted from stb_image.h

LICENSE
  See end of file for license information.

TODO
  Debug stbia__load_gif_main (crashes on some AGIF input files that work with single-frame stbia__gif_load)
  Add APNG patch: https://github.com/nothings/stb/pull/625
  Consider AGIF API changes e.g. https://gist.github.com/urraka/685d9a6340b26b830d49
  Unify AGIF+APNG API
  Implement full API (stbia_load, load from file+callback, uint16/uint32/float conversions)
*/

#ifndef INCLUDE_STB_IMAGE_ANIM_H
#define INCLUDE_STB_IMAGE_ANIM_H

// DOCUMENTATION
//
// Limitations:
//    - GIF always returns *comp=4
//

#ifndef STBIA_NO_STDIO
#include <stdio.h>
#endif // STBIA_NO_STDIO

#include <stdlib.h>
typedef unsigned char stbia_uc;
typedef unsigned short stbia_us;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STBIADEF
#ifdef STB_IMAGE_STATIC
#define STBIADEF static
#else
#define STBIADEF extern
#endif
#endif

//////////////////////////////////////////////////////////////////////////////
//
// PRIMARY API - works on images of any type
//

//
// load image by filename, open file, or memory buffer
//

typedef struct
{
   int      (*read)  (void *user,char *data,int size);   // fill 'data' with 'size' bytes.  return number of bytes actually read
   void     (*skip)  (void *user,int n);                 // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
   int      (*eof)   (void *user);                       // returns nonzero if we are at end of file/data
} stbia_io_callbacks;




////////////////////////////////////
//
// 8-bits-per-channel interface
//

/*
STBIADEF stbia_uc *stbia_load_from_memory   (stbia_uc           const *buffer, int len   , int *x, int *y, int *channels_in_file, int desired_channels);
STBIADEF stbia_uc *stbia_load_from_callbacks(stbia_io_callbacks const *clbk  , void *user, int *x, int *y, int *channels_in_file, int desired_channels);

#ifndef STBIA_NO_STDIO
STBIADEF stbia_uc *stbia_load            (char const *filename, int **delays, int *x, int *y, int *z, int *channels_in_file, int desired_channels);
STBIADEF stbia_uc *stbia_load_from_file  (FILE *f, int **delays, int *x, int *y, int *z, int *channels_in_file, int desired_channels);
// for stbia_load_from_file, file pointer is left pointing immediately after image
#endif
*/

#ifndef STBIA_NO_GIF
STBIADEF stbia_uc *stbia_load_gif_from_memory(stbia_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
#endif





#ifdef __cplusplus
}
#endif

//
//
////   end header file   /////////////////////////////////////////////////////
#endif // INCLUDE_STB_IMAGE_ANIM_H

#ifdef STB_IMAGE_ANIM_IMPLEMENTATION




#include <stdarg.h>
#include <stddef.h> // ptrdiff_t on osx
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(STBIA_NO_LINEAR) || !defined(STBIA_NO_HDR)
#include <math.h>  // ldexp, pow
#endif

#ifndef STBIA_NO_STDIO
#include <stdio.h>
#endif

#ifndef STBIA_ASSERT
#include <assert.h>
#define STBIA_ASSERT(x) assert(x)
#endif

#ifdef __cplusplus
#define STBIA_EXTERN extern "C"
#else
#define STBIA_EXTERN extern
#endif


#ifndef _MSC_VER
   #ifdef __cplusplus
   #define stbia_inline inline
   #else
   #define stbia_inline
   #endif
#else
   #define stbia_inline __forceinline
#endif

#ifndef STBIA_NO_THREAD_LOCALS
   #if defined(__cplusplus) &&  __cplusplus >= 201103L
      #define STBIA_THREAD_LOCAL       thread_local
   #elif defined(__GNUC__) && __GNUC__ < 5
      #define STBIA_THREAD_LOCAL       __thread
   #elif defined(_MSC_VER)
      #define STBIA_THREAD_LOCAL       __declspec(thread)
   #elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
      #define STBIA_THREAD_LOCAL       _Thread_local
   #endif

   #ifndef STBIA_THREAD_LOCAL
      #if defined(__GNUC__)
        #define STBIA_THREAD_LOCAL       __thread
      #endif
   #endif
#endif

#ifdef _MSC_VER
typedef unsigned short stbia__uint16;
typedef   signed short stbia__int16;
typedef unsigned int   stbia__uint32;
typedef   signed int   stbia__int32;
#else
#include <stdint.h>
typedef uint16_t stbia__uint16;
typedef int16_t  stbia__int16;
typedef uint32_t stbia__uint32;
typedef int32_t  stbia__int32;
#endif

// should produce compiler error if size is wrong
typedef unsigned char validate_uint32[sizeof(stbia__uint32)==4 ? 1 : -1];

#ifdef _MSC_VER
#define STBIA_NOTUSED(v)  (void)(v)
#else
#define STBIA_NOTUSED(v)  (void)sizeof(v)
#endif

#ifdef _MSC_VER
#define STBIA_HAS_LROTL
#endif

#ifdef STBIA_HAS_LROTL
   #define stbia_lrot(x,y)  _lrotl(x,y)
#else
   #define stbia_lrot(x,y)  (((x) << (y)) | ((x) >> (32 - (y))))
#endif

#if defined(STBIA_MALLOC) && defined(STBIA_FREE) && (defined(STBIA_REALLOC) || defined(STBIA_REALLOC_SIZED))
// ok
#elif !defined(STBIA_MALLOC) && !defined(STBIA_FREE) && !defined(STBIA_REALLOC) && !defined(STBIA_REALLOC_SIZED)
// ok
#else
#error "Must define all or none of STBIA_MALLOC, STBIA_FREE, and STBIA_REALLOC (or STBIA_REALLOC_SIZED)."
#endif

#ifndef STBIA_MALLOC
#define STBIA_MALLOC(sz)           malloc(sz)
#define STBIA_REALLOC(p,newsz)     realloc(p,newsz)
#define STBIA_FREE(p)              free(p)
#endif

#ifndef STBIA_REALLOC_SIZED
#define STBIA_REALLOC_SIZED(p,oldsz,newsz) STBIA_REALLOC(p,newsz)
#endif




#ifndef STBIA_MAX_DIMENSIONS
#define STBIA_MAX_DIMENSIONS (1 << 24)
#endif

///////////////////////////////////////////////
//
//  stbia__context struct and start_xxx functions

// stbia__context structure is our basic context used by all images, so it
// contains all the IO context, plus some basic image information
typedef struct
{
   stbia__uint32 img_x, img_y;
   int img_n, img_out_n;

   stbia_io_callbacks io;
   void *io_user_data;

   int read_from_callbacks;
   int buflen;
   stbia_uc buffer_start[128];
   int callback_already_read;

   stbia_uc *img_buffer, *img_buffer_end;
   stbia_uc *img_buffer_original, *img_buffer_original_end;
} stbia__context;


static void stbia__refill_buffer(stbia__context *s);

// initialize a memory-decode context
static void stbia__start_mem(stbia__context *s, stbia_uc const *buffer, int len)
{
   s->io.read = NULL;
   s->read_from_callbacks = 0;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = (stbia_uc *) buffer;
   s->img_buffer_end = s->img_buffer_original_end = (stbia_uc *) buffer+len;
}

// initialize a callback-based context
static void stbia__start_callbacks(stbia__context *s, stbia_io_callbacks *c, void *user)
{
   s->io = *c;
   s->io_user_data = user;
   s->buflen = sizeof(s->buffer_start);
   s->read_from_callbacks = 1;
   s->callback_already_read = 0;
   s->img_buffer = s->img_buffer_original = s->buffer_start;
   stbia__refill_buffer(s);
   s->img_buffer_original_end = s->img_buffer_end;
}

#ifndef STBIA_NO_STDIO

static int stbia__stdio_read(void *user, char *data, int size)
{
   return (int) fread(data,1,size,(FILE*) user);
}

static void stbia__stdio_skip(void *user, int n)
{
   int ch;
   fseek((FILE*) user, n, SEEK_CUR);
   ch = fgetc((FILE*) user);  /* have to read a byte to reset feof()'s flag */
   if (ch != EOF) {
      ungetc(ch, (FILE *) user);  /* push byte back onto stream if valid. */
   }
}

static int stbia__stdio_eof(void *user)
{
   return feof((FILE*) user) || ferror((FILE *) user);
}

static stbia_io_callbacks stbia__stdio_callbacks =
{
   stbia__stdio_read,
   stbia__stdio_skip,
   stbia__stdio_eof,
};

static void stbia__start_file(stbia__context *s, FILE *f)
{
   stbia__start_callbacks(s, &stbia__stdio_callbacks, (void *) f);
}

//static void stop_file(stbia__context *s) { }

#endif // !STBIA_NO_STDIO

static void stbia__rewind(stbia__context *s)
{
   // conceptually rewind SHOULD rewind to the beginning of the stream,
   // but we just rewind to the beginning of the initial buffer, because
   // we only use it after doing 'test', which only ever looks at at most 92 bytes
   s->img_buffer = s->img_buffer_original;
   s->img_buffer_end = s->img_buffer_original_end;
}

enum
{
   STBIA_ORDER_RGB,
   STBIA_ORDER_BGR
};

typedef struct
{
   int bits_per_channel;
   int num_channels;
   int channel_order;
} stbia__result_info;

#ifndef STBIA_NO_GIF
//~ static int      stbia__gif_test(stbia__context *s);
//~ static void    *stbia__gif_load(stbia__context *s, int *x, int *y, int *comp, int req_comp, stbia__result_info *ri);
static void    *stbia__load_gif_main(stbia__context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp);
//~ static int      stbia__gif_info(stbia__context *s, int *x, int *y, int *comp);
#endif

static
#ifdef STBIA_THREAD_LOCAL
STBIA_THREAD_LOCAL
#endif
const char *stbia__g_failure_reason;

STBIADEF const char *stbia_failure_reason(void)
{
   return stbia__g_failure_reason;
}

#ifndef STBIA_NO_FAILURE_STRINGS
static int stbia__err(const char *str)
{
   stbia__g_failure_reason = str;
   return 0;
}
#endif

static void *stbia__malloc(size_t size)
{
    return STBIA_MALLOC(size);
}

// stb_image uses ints pervasively, including for offset calculations.
// therefore the largest decoded image size we can support with the
// current code, even on 64-bit targets, is INT_MAX. this is not a
// significant limitation for the intended use case.
//
// we do, however, need to make sure our size calculations don't
// overflow. hence a few helper functions for size calculations that
// multiply integers together, making sure that they're non-negative
// and no overflow occurs.

// return 1 if the sum is valid, 0 on overflow.
// negative terms are considered invalid.
static int stbia__addsizes_valid(int a, int b)
{
   if (b < 0) return 0;
   // now 0 <= b <= INT_MAX, hence also
   // 0 <= INT_MAX - b <= INTMAX.
   // And "a + b <= INT_MAX" (which might overflow) is the
   // same as a <= INT_MAX - b (no overflow)
   return a <= INT_MAX - b;
}

// returns 1 if the product is valid, 0 on overflow.
// negative factors are considered invalid.
static int stbia__mul2sizes_valid(int a, int b)
{
   if (a < 0 || b < 0) return 0;
   if (b == 0) return 1; // mul-by-0 is always safe
   // portable way to check for no overflows in a*b
   return a <= INT_MAX/b;
}

#if !defined(STBIA_NO_JPEG) || !defined(STBIA_NO_PNG) || !defined(STBIA_NO_TGA) || !defined(STBIA_NO_HDR)
// returns 1 if "a*b + add" has no negative terms/factors and doesn't overflow
static int stbia__mad2sizes_valid(int a, int b, int add)
{
   return stbia__mul2sizes_valid(a, b) && stbia__addsizes_valid(a*b, add);
}
#endif

// returns 1 if "a*b*c + add" has no negative terms/factors and doesn't overflow
static int stbia__mad3sizes_valid(int a, int b, int c, int add)
{
   return stbia__mul2sizes_valid(a, b) && stbia__mul2sizes_valid(a*b, c) &&
      stbia__addsizes_valid(a*b*c, add);
}


static void *stbia__malloc_mad3(int a, int b, int c, int add)
{
   if (!stbia__mad3sizes_valid(a, b, c, add)) return NULL;
   return stbia__malloc(a*b*c + add);
}


// stbia__err - error
// stbia__errpf - error returning pointer to float
// stbia__errpuc - error returning pointer to unsigned char

#ifdef STBIA_NO_FAILURE_STRINGS
   #define stbia__err(x,y)  0
#elif defined(STBIA_FAILURE_USERMSG)
   #define stbia__err(x,y)  stbia__err(y)
#else
   #define stbia__err(x,y)  stbia__err(x)
#endif

#define stbia__errpf(x,y)   ((float *)(size_t) (stbia__err(x,y)?NULL:NULL))
#define stbia__errpuc(x,y)  ((unsigned char *)(size_t) (stbia__err(x,y)?NULL:NULL))

STBIADEF void stbia_image_free(void *retval_from_stbia_load)
{
   STBIA_FREE(retval_from_stbia_load);
}





static int stbia__vertically_flip_on_load_global = 0;

STBIADEF void stbia_set_flip_vertically_on_load(int flag_true_if_should_flip)
{
   stbia__vertically_flip_on_load_global = flag_true_if_should_flip;
}

#ifndef STBIA_THREAD_LOCAL
#define stbia__vertically_flip_on_load  stbia__vertically_flip_on_load_global
#else
static STBIA_THREAD_LOCAL int stbia__vertically_flip_on_load_local, stbia__vertically_flip_on_load_set;

STBIADEF void stbia_set_flip_vertically_on_load_thread(int flag_true_if_should_flip)
{
   stbia__vertically_flip_on_load_local = flag_true_if_should_flip;
   stbia__vertically_flip_on_load_set = 1;
}

#define stbia__vertically_flip_on_load  (stbia__vertically_flip_on_load_set       \
                                         ? stbia__vertically_flip_on_load_local  \
                                         : stbia__vertically_flip_on_load_global)
#endif // STBIA_THREAD_LOCAL




//////////////////////////////////////////////////////////////////////////////
//
// Common code used by all image loaders
//

enum
{
   STBIA__SCAN_load=0,
   STBIA__SCAN_type,
   STBIA__SCAN_header
};

static void stbia__refill_buffer(stbia__context *s)
{
   int n = (s->io.read)(s->io_user_data,(char*)s->buffer_start,s->buflen);
   s->callback_already_read += (int) (s->img_buffer - s->img_buffer_original);
   if (n == 0) {
      // at end of file, treat same as if from memory, but need to handle case
      // where s->img_buffer isn't pointing to safe memory, e.g. 0-byte file
      s->read_from_callbacks = 0;
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start+1;
      *s->img_buffer = 0;
   } else {
      s->img_buffer = s->buffer_start;
      s->img_buffer_end = s->buffer_start + n;
   }
}

stbia_inline static stbia_uc stbia__get8(stbia__context *s)
{
   if (s->img_buffer < s->img_buffer_end)
      return *s->img_buffer++;
   if (s->read_from_callbacks) {
      stbia__refill_buffer(s);
      return *s->img_buffer++;
   }
   return 0;
}

#if defined(STBIA_NO_JPEG) && defined(STBIA_NO_HDR) && defined(STBIA_NO_PIC) && defined(STBIA_NO_PNM)
// nothing
#else
stbia_inline static int stbia__at_eof(stbia__context *s)
{
   if (s->io.read) {
      if (!(s->io.eof)(s->io_user_data)) return 0;
      // if feof() is true, check if buffer = end
      // special case: we've only got the special 0 character at the end
      if (s->read_from_callbacks == 0) return 1;
   }

   return s->img_buffer >= s->img_buffer_end;
}
#endif

#if defined(STBIA_NO_JPEG) && defined(STBIA_NO_PNG) && defined(STBIA_NO_BMP) && defined(STBIA_NO_PSD) && defined(STBIA_NO_TGA) && defined(STBIA_NO_GIF) && defined(STBIA_NO_PIC)
// nothing
#else
static void stbia__skip(stbia__context *s, int n)
{
   if (n == 0) return;  // already there!
   if (n < 0) {
      s->img_buffer = s->img_buffer_end;
      return;
   }
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         s->img_buffer = s->img_buffer_end;
         (s->io.skip)(s->io_user_data, n - blen);
         return;
      }
   }
   s->img_buffer += n;
}
#endif

#if defined(STBIA_NO_PNG) && defined(STBIA_NO_TGA) && defined(STBIA_NO_HDR) && defined(STBIA_NO_PNM)
// nothing
#else
static int stbia__getn(stbia__context *s, stbia_uc *buffer, int n)
{
   if (s->io.read) {
      int blen = (int) (s->img_buffer_end - s->img_buffer);
      if (blen < n) {
         int res, count;

         memcpy(buffer, s->img_buffer, blen);

         count = (s->io.read)(s->io_user_data, (char*) buffer + blen, n - blen);
         res = (count == (n-blen));
         s->img_buffer = s->img_buffer_end;
         return res;
      }
   }

   if (s->img_buffer+n <= s->img_buffer_end) {
      memcpy(buffer, s->img_buffer, n);
      s->img_buffer += n;
      return 1;
   } else
      return 0;
}
#endif

#if defined(STBIA_NO_JPEG) && defined(STBIA_NO_PNG) && defined(STBIA_NO_PSD) && defined(STBIA_NO_PIC)
// nothing
#else
static int stbia__get16be(stbia__context *s)
{
   int z = stbia__get8(s);
   return (z << 8) + stbia__get8(s);
}
#endif

#if defined(STBIA_NO_PNG) && defined(STBIA_NO_PSD) && defined(STBIA_NO_PIC)
// nothing
#else
static stbia__uint32 stbia__get32be(stbia__context *s)
{
   stbia__uint32 z = stbia__get16be(s);
   return (z << 16) + stbia__get16be(s);
}
#endif

#if defined(STBIA_NO_BMP) && defined(STBIA_NO_TGA) && defined(STBIA_NO_GIF)
// nothing
#else
static int stbia__get16le(stbia__context *s)
{
   int z = stbia__get8(s);
   return z + (stbia__get8(s) << 8);
}
#endif

#ifndef STBIA_NO_BMP
static stbia__uint32 stbia__get32le(stbia__context *s)
{
   stbia__uint32 z = stbia__get16le(s);
   return z + (stbia__get16le(s) << 16);
}
#endif

#define STBIA__BYTECAST(x)  ((stbia_uc) ((x) & 255))  // truncate int to byte without warnings

#if defined(STBIA_NO_JPEG) && defined(STBIA_NO_PNG) && defined(STBIA_NO_BMP) && defined(STBIA_NO_PSD) && defined(STBIA_NO_TGA) && defined(STBIA_NO_GIF) && defined(STBIA_NO_PIC) && defined(STBIA_NO_PNM)
// nothing
#else
//////////////////////////////////////////////////////////////////////////////
//
//  generic converter from built-in img_n to req_comp
//    individual types do this automatically as much as possible (e.g. jpeg
//    does all cases internally since it needs to colorspace convert anyway,
//    and it never has alpha, so very few cases ). png can automatically
//    interleave an alpha=255 channel, but falls back to this for other cases
//
//  assume data buffer is malloced, so malloc a new one and free that one
//  only failure mode is malloc failing

static stbia_uc stbia__compute_y(int r, int g, int b)
{
   return (stbia_uc) (((r*77) + (g*150) +  (29*b)) >> 8);
}
#endif

#if defined(STBIA_NO_PNG) && defined(STBIA_NO_BMP) && defined(STBIA_NO_PSD) && defined(STBIA_NO_TGA) && defined(STBIA_NO_GIF) && defined(STBIA_NO_PIC) && defined(STBIA_NO_PNM)
// nothing
#else
static unsigned char *stbia__convert_format(unsigned char *data, int img_n, int req_comp, unsigned int x, unsigned int y)
{
   int i,j;
   unsigned char *good;

   if (req_comp == img_n) return data;
   STBIA_ASSERT(req_comp >= 1 && req_comp <= 4);

   good = (unsigned char *) stbia__malloc_mad3(req_comp, x, y, 0);
   if (good == NULL) {
      STBIA_FREE(data);
      return stbia__errpuc("outofmem", "Out of memory");
   }

   for (j=0; j < (int) y; ++j) {
      unsigned char *src  = data + j * x * img_n   ;
      unsigned char *dest = good + j * x * req_comp;

      #define STBIA__COMBO(a,b)  ((a)*8+(b))
      #define STBIA__CASE(a,b)   case STBIA__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
      // convert source image with img_n components to one with req_comp components;
      // avoid switch per pixel, so use switch per scanline and massive macros
      switch (STBIA__COMBO(img_n, req_comp)) {
         STBIA__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
         STBIA__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBIA__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
         STBIA__CASE(2,1) { dest[0]=src[0];                                                  } break;
         STBIA__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
         STBIA__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
         STBIA__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
         STBIA__CASE(3,1) { dest[0]=stbia__compute_y(src[0],src[1],src[2]);                   } break;
         STBIA__CASE(3,2) { dest[0]=stbia__compute_y(src[0],src[1],src[2]); dest[1] = 255;    } break;
         STBIA__CASE(4,1) { dest[0]=stbia__compute_y(src[0],src[1],src[2]);                   } break;
         STBIA__CASE(4,2) { dest[0]=stbia__compute_y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
         STBIA__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
         default: STBIA_ASSERT(0); STBIA_FREE(data); STBIA_FREE(good); return stbia__errpuc("unsupported", "Unsupported format conversion");
      }
      #undef STBIA__CASE
   }

   STBIA_FREE(data);
   return good;
}
#endif




/*
static void *stbia__load_main(stbia__context *s, int *x, int *y, int *comp, int req_comp, stbia__result_info *ri, int bpc)
{
   memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
   ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
   ri->channel_order = STBIA_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order
   ri->num_channels = 0;

   //~ #ifndef STBIA_NO_PNG
   //~ if (stbia__png_test(s))  return stbia__png_load(s,x,y,comp,req_comp, ri);
   //~ #endif
   #ifndef STBIA_NO_GIF
   if (stbia__gif_test(s))  return stbia__gif_load(s,x,y,comp,req_comp, ri);
   #endif

   return stbia__errpuc("unknown anim image type", "Image not of any known type, or corrupt");
}
*/




static void stbia__vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
   int row;
   size_t bytes_per_row = (size_t)w * bytes_per_pixel;
   stbia_uc temp[2048];
   stbia_uc *bytes = (stbia_uc *)image;

   for (row = 0; row < (h>>1); row++) {
      stbia_uc *row0 = bytes + row*bytes_per_row;
      stbia_uc *row1 = bytes + (h - row - 1)*bytes_per_row;
      // swap row0 with row1
      size_t bytes_left = bytes_per_row;
      while (bytes_left) {
         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
         memcpy(temp, row0, bytes_copy);
         memcpy(row0, row1, bytes_copy);
         memcpy(row1, temp, bytes_copy);
         row0 += bytes_copy;
         row1 += bytes_copy;
         bytes_left -= bytes_copy;
      }
   }
}

#ifndef STBIA_NO_GIF
static void stbia__vertical_flip_slices(void *image, int w, int h, int z, int bytes_per_pixel)
{
   int slice;
   int slice_size = w * h * bytes_per_pixel;

   stbia_uc *bytes = (stbia_uc *)image;
   for (slice = 0; slice < z; ++slice) {
      stbia__vertical_flip(bytes, w, h, bytes_per_pixel);
      bytes += slice_size;
   }
}
#endif




/*
static unsigned char *stbia__load_and_postprocess_8bit(stbia__context *s, int *x, int *y, int *comp, int req_comp)
{
   stbia__result_info ri;
   void *result = stbia__anim_load_main(s, x, y, comp, req_comp, &ri, 8);

   if (result == NULL)
      return NULL;

   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
   STBIA_ASSERT(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

   if (ri.bits_per_channel != 8) {
      result = stbia__convert_16_to_8((stbia__uint16 *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
      ri.bits_per_channel = 8;
   }

   // @TODO: move stbia__convert_format to here

   if (stbia__vertically_flip_on_load) {
      int channels = req_comp ? req_comp : *comp;
      stbia__vertical_flip(result, *x, *y, channels * sizeof(stbia_uc));
   }

   return (unsigned char *) result;
}




STBIADEF stbia_uc *stbia_load(char const *filename, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   FILE *f = stbia__fopen(filename, "rb");
   unsigned char *result;
   if (!f) return stbia__errpuc("can't fopen", "Unable to open file");
   result = stbia_load_from_file(f,delays,x,y,z,comp,req_comp);
   fclose(f);
   return result;
}

STBIADEF stbia_uc *stbia_load_from_file(FILE *f, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   unsigned char *result;
   stbia__context s;
   stbia__start_file(&s,f);
   //~ result = stbia__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
   result = (unsigned char*) stbia__load_gif_main(&s, delays, x, y, z, comp, req_comp);
   if (result) {
      // need to 'unget' all the characters in the IO buffer
      fseek(f, - (int) (s.img_buffer_end - s.img_buffer), SEEK_CUR);
   }
   if (stbia__vertically_flip_on_load) {
      stbia__vertical_flip_slices( result, *x, *y, *z, *comp );
   }
   return result;
}



STBIADEF stbia_uc *stbia_load_from_memory(stbia_uc const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stbia__context s;
   stbia__start_mem(&s,buffer,len);
   return stbia__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}

STBIADEF stbia_uc *stbia_load_from_callbacks(stbia_io_callbacks const *clbk, void *user, int *x, int *y, int *comp, int req_comp)
{
   stbia__context s;
   stbia__start_callbacks(&s, (stbia_io_callbacks *) clbk, user);
   return stbia__load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}
*/

#ifndef STBIA_NO_GIF
STBIADEF stbia_uc *stbia_load_gif_from_memory(stbia_uc const *buffer, int len, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   unsigned char *result;
   stbia__context s;
   stbia__start_mem(&s,buffer,len);

   result = (unsigned char*) stbia__load_gif_main(&s, delays, x, y, z, comp, req_comp);
   if (stbia__vertically_flip_on_load) {
      stbia__vertical_flip_slices( result, *x, *y, *z, *comp );
   }

   return result;
}
#endif







// *************************************************************************************************
// ANIMATED GIF loader -- public domain by Jean-Marc Lienher -- simplified/shrunk by stb

#ifndef STBIA_NO_GIF
typedef struct
{
   stbia__int16 prefix;
   stbia_uc first;
   stbia_uc suffix;
} stbia__gif_lzw;

typedef struct
{
   int w,h;
   stbia_uc *out;                 // output buffer (always 4 components)
   stbia_uc *background;          // The current "background" as far as a gif is concerned
   stbia_uc *history;
   int flags, bgindex, ratio, transparent, eflags;
   stbia_uc  pal[256][4];
   stbia_uc lpal[256][4];
   stbia__gif_lzw codes[8192];
   stbia_uc *color_table;
   int parse, step;
   int lflags;
   int start_x, start_y;
   int max_x, max_y;
   int cur_x, cur_y;
   int line_size;
   int delay;
} stbia__gif;

static int stbia__gif_test_raw(stbia__context *s)
{
   int sz;
   if (stbia__get8(s) != 'G' || stbia__get8(s) != 'I' || stbia__get8(s) != 'F' || stbia__get8(s) != '8') return 0;
   sz = stbia__get8(s);
   if (sz != '9' && sz != '7') return 0;
   if (stbia__get8(s) != 'a') return 0;
   return 1;
}

static int stbia__gif_test(stbia__context *s)
{
   int r = stbia__gif_test_raw(s);
   stbia__rewind(s);
   return r;
}

static void stbia__gif_parse_colortable(stbia__context *s, stbia_uc pal[256][4], int num_entries, int transp)
{
   int i;
   for (i=0; i < num_entries; ++i) {
      pal[i][2] = stbia__get8(s);
      pal[i][1] = stbia__get8(s);
      pal[i][0] = stbia__get8(s);
      pal[i][3] = transp == i ? 0 : 255;
   }
}

static int stbia__gif_header(stbia__context *s, stbia__gif *g, int *comp, int is_info)
{
   stbia_uc version;
   if (stbia__get8(s) != 'G' || stbia__get8(s) != 'I' || stbia__get8(s) != 'F' || stbia__get8(s) != '8')
      return stbia__err("not GIF", "Corrupt GIF");

   version = stbia__get8(s);
   if (version != '7' && version != '9')    return stbia__err("not GIF", "Corrupt GIF");
   if (stbia__get8(s) != 'a')                return stbia__err("not GIF", "Corrupt GIF");

   stbia__g_failure_reason = "";
   g->w = stbia__get16le(s);
   g->h = stbia__get16le(s);
   g->flags = stbia__get8(s);
   g->bgindex = stbia__get8(s);
   g->ratio = stbia__get8(s);
   g->transparent = -1;

   if (g->w > STBIA_MAX_DIMENSIONS) return stbia__err("too large","Very large image (corrupt?)");
   if (g->h > STBIA_MAX_DIMENSIONS) return stbia__err("too large","Very large image (corrupt?)");

   if (comp != 0) *comp = 4;  // can't actually tell whether it's 3 or 4 until we parse the comments

   if (is_info) return 1;

   if (g->flags & 0x80)
      stbia__gif_parse_colortable(s,g->pal, 2 << (g->flags & 7), -1);

   return 1;
}

static int stbia__gif_info_raw(stbia__context *s, int *x, int *y, int *comp)
{
   stbia__gif* g = (stbia__gif*) stbia__malloc(sizeof(stbia__gif));
   if (!stbia__gif_header(s, g, comp, 1)) {
      STBIA_FREE(g);
      stbia__rewind( s );
      return 0;
   }
   if (x) *x = g->w;
   if (y) *y = g->h;
   STBIA_FREE(g);
   return 1;
}

static void stbia__out_gif_code(stbia__gif *g, stbia__uint16 code)
{
   stbia_uc *p, *c;
   int idx;

   // recurse to decode the prefixes, since the linked-list is backwards,
   // and working backwards through an interleaved image would be nasty
   if (g->codes[code].prefix >= 0)
      stbia__out_gif_code(g, g->codes[code].prefix);

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

static stbia_uc *stbia__process_gif_raster(stbia__context *s, stbia__gif *g)
{
   stbia_uc lzw_cs;
   stbia__int32 len, init_code;
   stbia__uint32 first;
   stbia__int32 codesize, codemask, avail, oldcode, bits, valid_bits, clear;
   stbia__gif_lzw *p;

   lzw_cs = stbia__get8(s);
   if (lzw_cs > 12) return NULL;
   clear = 1 << lzw_cs;
   first = 1;
   codesize = lzw_cs + 1;
   codemask = (1 << codesize) - 1;
   bits = 0;
   valid_bits = 0;
   for (init_code = 0; init_code < clear; init_code++) {
      g->codes[init_code].prefix = -1;
      g->codes[init_code].first = (stbia_uc) init_code;
      g->codes[init_code].suffix = (stbia_uc) init_code;
   }

   // support no starting clear code
   avail = clear+2;
   oldcode = -1;

   len = 0;
   for(;;) {
      if (valid_bits < codesize) {
         if (len == 0) {
            len = stbia__get8(s); // start new block
            if (len == 0)
               return g->out;
         }
         --len;
         bits |= (stbia__int32) stbia__get8(s) << valid_bits;
         valid_bits += 8;
      } else {
         stbia__int32 code = bits & codemask;
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
            stbia__skip(s, len);
            while ((len = stbia__get8(s)) > 0)
               stbia__skip(s,len);
            return g->out;
         } else if (code <= avail) {
            if (first) {
               return stbia__errpuc("no clear code", "Corrupt GIF");
            }

            if (oldcode >= 0) {
               p = &g->codes[avail++];
               if (avail > 8192) {
                  return stbia__errpuc("too many codes", "Corrupt GIF");
               }

               p->prefix = (stbia__int16) oldcode;
               p->first = g->codes[oldcode].first;
               p->suffix = (code == avail) ? p->first : g->codes[code].first;
            } else if (code == avail)
               return stbia__errpuc("illegal code in raster", "Corrupt GIF");

            stbia__out_gif_code(g, (stbia__uint16) code);

            if ((avail & codemask) == 0 && avail <= 0x0FFF) {
               codesize++;
               codemask = (1 << codesize) - 1;
            }

            oldcode = code;
         } else {
            return stbia__errpuc("illegal code in raster", "Corrupt GIF");
         }
      }
   }
}

// this function is designed to support animated gifs, although stb_image doesn't support it
// two back is the image from two frames ago, used for a very specific disposal format
static stbia_uc *stbia__gif_load_next(stbia__context *s, stbia__gif *g, int *comp, int req_comp, stbia_uc *two_back)
{
   int dispose;
   int first_frame;
   int pi;
   int pcount;
   STBIA_NOTUSED(req_comp);

   // on first frame, any non-written pixels get the background colour (non-transparent)
   first_frame = 0;
   if (g->out == 0) {
      if (!stbia__gif_header(s, g, comp,0)) return 0; // stbia__g_failure_reason set by stbia__gif_header
      if (!stbia__mad3sizes_valid(4, g->w, g->h, 0))
         return stbia__errpuc("too large", "GIF image is too large");
      pcount = g->w * g->h;
      g->out = (stbia_uc *) stbia__malloc(4 * pcount);
      g->background = (stbia_uc *) stbia__malloc(4 * pcount);
      g->history = (stbia_uc *) stbia__malloc(pcount);
      if (!g->out || !g->background || !g->history)
         return stbia__errpuc("outofmem", "Out of memory");

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
      int tag = stbia__get8(s);
      switch (tag) {
         case 0x2C: /* Image Descriptor */
         {
            stbia__int32 x, y, w, h;
            stbia_uc *o;

            x = stbia__get16le(s);
            y = stbia__get16le(s);
            w = stbia__get16le(s);
            h = stbia__get16le(s);
            if (((x + w) > (g->w)) || ((y + h) > (g->h)))
               return stbia__errpuc("bad Image Descriptor", "Corrupt GIF");

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

            g->lflags = stbia__get8(s);

            if (g->lflags & 0x40) {
               g->step = 8 * g->line_size; // first interlaced spacing
               g->parse = 3;
            } else {
               g->step = g->line_size;
               g->parse = 0;
            }

            if (g->lflags & 0x80) {
               stbia__gif_parse_colortable(s,g->lpal, 2 << (g->lflags & 7), g->eflags & 0x01 ? g->transparent : -1);
               g->color_table = (stbia_uc *) g->lpal;
            } else if (g->flags & 0x80) {
               g->color_table = (stbia_uc *) g->pal;
            } else
               return stbia__errpuc("missing color table", "Corrupt GIF");

            o = stbia__process_gif_raster(s, g);
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
            int ext = stbia__get8(s);
            if (ext == 0xF9) { // Graphic Control Extension.
               len = stbia__get8(s);
               if (len == 4) {
                  g->eflags = stbia__get8(s);
                  g->delay = 10 * stbia__get16le(s); // delay - 1/100th of a second, saving as 1/1000ths.

                  // unset old transparent
                  if (g->transparent >= 0) {
                     g->pal[g->transparent][3] = 255;
                  }
                  if (g->eflags & 0x01) {
                     g->transparent = stbia__get8(s);
                     if (g->transparent >= 0) {
                        g->pal[g->transparent][3] = 0;
                     }
                  } else {
                     // don't need transparent
                     stbia__skip(s, 1);
                     g->transparent = -1;
                  }
               } else {
                  stbia__skip(s, len);
                  break;
               }
            }
            while ((len = stbia__get8(s)) != 0) {
               stbia__skip(s, len);
            }
            break;
         }

         case 0x3B: // gif stream termination code
            return (stbia_uc *) s; // using '1' causes warning on some compilers

         default:
            return stbia__errpuc("unknown code", "Corrupt GIF");
      }
   }
}

static void *stbia__load_gif_main(stbia__context *s, int **delays, int *x, int *y, int *z, int *comp, int req_comp)
{
   if (stbia__gif_test(s)) {
      int layers = 0;
      stbia_uc *u = 0;
      stbia_uc *out = 0;
      stbia_uc *two_back = 0;
      stbia__gif g;
      int stride;
      int out_size = 0;
      int delays_size = 0;
      memset(&g, 0, sizeof(g));
      if (delays) {
         *delays = 0;
      }

      do {
         u = stbia__gif_load_next(s, &g, comp, req_comp, two_back);
         if (u == (stbia_uc *) s) u = 0;  // end of animated gif marker

         if (u) {
            *x = g.w;
            *y = g.h;
            ++layers;
            stride = g.w * g.h * 4;

            if (out) {
               void *tmp = (stbia_uc*) STBIA_REALLOC_SIZED( out, out_size, layers * stride );
               if (NULL == tmp) {
                  STBIA_FREE(g.out);
                  STBIA_FREE(g.history);
                  STBIA_FREE(g.background);
                  return stbia__errpuc("outofmem", "Out of memory");
               }
               else {
                   out = (stbia_uc*) tmp;
                   out_size = layers * stride;
               }

               if (delays) {
                  *delays = (int*) STBIA_REALLOC_SIZED( *delays, delays_size, sizeof(int) * layers );
                  delays_size = layers * sizeof(int);
               }
            } else {
               out = (stbia_uc*)stbia__malloc( layers * stride );
               out_size = layers * stride;
               if (delays) {
                  *delays = (int*) stbia__malloc( layers * sizeof(int) );
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
      STBIA_FREE(g.out);
      STBIA_FREE(g.history);
      STBIA_FREE(g.background);

      // do the final conversion after loading everything;
      if (req_comp && req_comp != 4)
         out = stbia__convert_format(out, 4, req_comp, layers * g.w, g.h);

      *z = layers;
      return out;
   } else {
      return stbia__errpuc("not GIF", "Image was not as a gif type.");
   }
}

static void *stbia__gif_load(stbia__context *s, int *x, int *y, int *comp, int req_comp, stbia__result_info *ri)
{
   stbia_uc *u = 0;
   stbia__gif g;
   memset(&g, 0, sizeof(g));
   STBIA_NOTUSED(ri);

   u = stbia__gif_load_next(s, &g, comp, req_comp, 0);
   if (u == (stbia_uc *) s) u = 0;  // end of animated gif marker
   if (u) {
      *x = g.w;
      *y = g.h;

      // moved conversion to after successful load so that the same
      // can be done for multiple frames.
      if (req_comp && req_comp != 4)
         u = stbia__convert_format(u, 4, req_comp, g.w, g.h);
   } else if (g.out) {
      // if there was an error and we allocated an image buffer, free it!
      STBIA_FREE(g.out);
   }

   // free buffers needed for multiple frame loading;
   STBIA_FREE(g.history);
   STBIA_FREE(g.background);

   return u;
}

static int stbia__gif_info(stbia__context *s, int *x, int *y, int *comp)
{
   return stbia__gif_info_raw(s,x,y,comp);
}
#endif






#endif // STB_IMAGE_ANIM_IMPLEMENTATION

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
