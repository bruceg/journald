/* md4.c - Functions to compute MD4 message digest
   according to the definition of MD4 in RFC 1320 from April 1992.
   Copyright (C) 2000 Bruce Guenter

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Based on the GNU C Library MD5 source code,
   Written by Ulrich Drepper <drepper@gnu.ai.mit.edu>, 1995.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <string.h>
#include "md4.h"

# include <endian.h>
# if __BYTE_ORDER == __BIG_ENDIAN
#  define WORDS_BIGENDIAN 1
# endif

#ifdef WORDS_BIGENDIAN
# define SWAP(n)							\
    (((n) << 24) | (((n) & 0xff00) << 8) | (((n) >> 8) & 0xff00) | ((n) >> 24))
#else
# define SWAP(n) (n)
#endif


/* This array contains the bytes used to pad the buffer to the next
   64-byte boundary.  (RFC 1320, 3.1: Step 1)  */
static const unsigned char fillbuf[64] = { 0x80, 0 /* , 0, 0, ...  */ };


/* Initialize structure containing state of computation.
   (RFC 1320, 3.3: Step 3)  */
void
md4_init_ctx (ctx)
     struct md4_ctx *ctx;
{
  ctx->A = 0x67452301;
  ctx->B = 0xefcdab89;
  ctx->C = 0x98badcfe;
  ctx->D = 0x10325476;

  ctx->total[0] = ctx->total[1] = 0;
  ctx->buflen = 0;
}

/* Put result from CTX in first 16 bytes following RESBUF.  The result
   must be in little endian byte order.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
void *
md4_read_ctx (ctx, resbuf)
     const struct md4_ctx *ctx;
     void *resbuf;
{
  ((md4_uint32 *) resbuf)[0] = SWAP (ctx->A);
  ((md4_uint32 *) resbuf)[1] = SWAP (ctx->B);
  ((md4_uint32 *) resbuf)[2] = SWAP (ctx->C);
  ((md4_uint32 *) resbuf)[3] = SWAP (ctx->D);

  return resbuf;
}

/* Process the remaining bytes in the internal buffer and the usual
   prolog according to the standard and write the result to RESBUF.

   IMPORTANT: On some systems it is required that RESBUF is correctly
   aligned for a 32 bits value.  */
void *
md4_finish_ctx (ctx, resbuf)
     struct md4_ctx *ctx;
     void *resbuf;
{
  /* Take yet unprocessed bytes into account.  */
  md4_uint32 bytes = ctx->buflen;
  md4_uint32 total0;
  md4_uint32 total1;
  
  /* Now count remaining bytes.  */
  ctx->total[0] += bytes;
  if (ctx->total[0] < bytes)
    ++ctx->total[1];
  total0 = ctx->total[0];
  total1 = ctx->total[1];
  
  if (bytes >= 56) {
    memcpy(ctx->buffer+bytes, fillbuf, 64-bytes);
    md4_process_block(ctx->buffer, ctx);
    memcpy(ctx->buffer, fillbuf+8, 56);
  }
  else
    memcpy(ctx->buffer+bytes, fillbuf, 56-bytes);

  /* Put the 64-bit file length in *bits* at the end of the buffer.  */
  *(md4_uint32 *) &ctx->buffer[56] = SWAP (total0 << 3);
  *(md4_uint32 *) &ctx->buffer[60] = SWAP ((total1 << 3) |
					   (total0 >> 29));

  /* Process last bytes.  */
  md4_process_block (ctx->buffer, ctx);

  return md4_read_ctx (ctx, resbuf);
}


void
md4_process_bytes (buffer, len, ctx)
     const void *buffer;
     size_t len;
     struct md4_ctx *ctx;
{
  /* When we already have some bits in our internal buffer concatenate
     both inputs first.  */
  if (ctx->buflen != 0)
    {
      size_t left_over = ctx->buflen;
      size_t add = 128 - left_over > len ? len : 128 - left_over;

      memcpy (&ctx->buffer[left_over], buffer, add);
      ctx->buflen += add;

      if (left_over + add >= 64)
	{
	  md4_process_block (ctx->buffer, ctx);
	  /* The regions in the following copy operation cannot overlap.  */
	  memcpy (ctx->buffer, &ctx->buffer[(left_over + add) & ~63],
		  (left_over + add) & 63);
	  ctx->buflen = (left_over + add) & 63;
	}

      buffer = (const char *) buffer + add;
      len -= add;
    }

  /* Process available complete blocks.  */
  while (len > 64)
    {
      md4_process_block (buffer, ctx);
      buffer = (const char *) buffer + 64;
      len -= 64;
    }

  /* Move remaining bytes in internal buffer.  */
  if (len > 0)
    {
      memcpy (ctx->buffer, buffer, len);
      ctx->buflen = len;
    }
}


/* Compute MD4 message digest for LEN bytes beginning at BUFFER.  The
   result is always in little endian byte order, so that a byte-wise
   output yields to the wanted ASCII representation of the message
   digest.  */
void *
md4_buffer (buffer, len, resblock)
     const char *buffer;
     size_t len;
     void *resblock;
{
  struct md4_ctx ctx;

  /* Initialize the computation context.  */
  md4_init_ctx (&ctx);

  /* Process whole buffer but last len % 64 bytes.  */
  md4_process_bytes (buffer, len, &ctx);

  /* Put result in desired memory area.  */
  return md4_finish_ctx (&ctx, resblock);
}


/* These are the four functions used in the four steps of the MD4 algorithm
   and defined in the RFC 1320.  The first function is a little bit optimized
   (as found in Colin Plumbs public domain MD5 implementation).  */
/* #define FF(X,Y,Z) ((X & Y) | (~X & Z)) */
#define FF(X,Y,Z) (Z ^ (X & (Y ^ Z)))
#define FG(X,Y,Z) ((X & Y) | (X & Z) | (Y & Z))
#define FH(X,Y,Z) (X ^ Y ^ Z)

/* Process 64 bytes of BUFFER, accumulating context into CTX. */

void
md4_process_block (buffer, ctx)
     const void *buffer;
     struct md4_ctx *ctx;
{
  md4_uint32 correct_words[16];
  const md4_uint32 *words = buffer;
  md4_uint32 A = ctx->A;
  md4_uint32 B = ctx->B;
  md4_uint32 C = ctx->C;
  md4_uint32 D = ctx->D;
  md4_uint32 *cwp = correct_words;

  /* First increment the byte count.  RFC 1320 specifies the possible
     length of the file up to 2^64 bits.  Here we only compute the
     number of bytes.  Do a double word increment.  */
  ctx->total[0] += 64;
  if (ctx->total[0] < 64)
    ++ctx->total[1];

  /* First round: using the given function, the context and a constant
     the next context is computed.  Because the algorithms processing
     unit is a 32-bit word and it is determined to work on words in
     little endian byte order we perhaps have to change the byte order
     before the computation.  To reduce the work for the next steps
     we store the swapped words in the array CORRECT_WORDS.  */

#define OP(a, b, c, d, s)						\
      do								\
        {								\
	  a += FF (b, c, d) + (*cwp++ = SWAP (*words));			\
	  ++words;							\
	  CYCLIC (a, s);						\
        }								\
      while (0)

  /* It is unfortunate that C does not provide an operator for
     cyclic rotation.  Hope the C compiler is smart enough.  */
#define CYCLIC(w, s) (w = (w << s) | (w >> (32 - s)))

  /* Round 1.  */
  OP(A,B,C,D, 3); OP(D,A,B,C, 7); OP(C,D,A,B,11); OP(B,C,D,A,19);
  OP(A,B,C,D, 3); OP(D,A,B,C, 7); OP(C,D,A,B,11); OP(B,C,D,A,19);
  OP(A,B,C,D, 3); OP(D,A,B,C, 7); OP(C,D,A,B,11); OP(B,C,D,A,19);
  OP(A,B,C,D, 3); OP(D,A,B,C, 7); OP(C,D,A,B,11); OP(B,C,D,A,19);
      
  /* For the second to fourth round we have the possibly swapped words
     in CORRECT_WORDS.  Redefine the macro to take an additional first
     argument specifying the function to use.  */
#undef OP
#define OP(f, a, b, c, d, k, s, T)					\
      do 								\
	{								\
	  a += f (b, c, d) + correct_words[k] + T;			\
	  CYCLIC (a, s);						\
	}								\
      while (0)

  /* Round 2.  */
#define OP2(a,b,c,d,k,s) OP(FG,a,b,c,d,k,s,0x5A827999)
  OP2 (A, B, C, D,  0,  3);
  OP2 (D, A, B, C,  4,  5);
  OP2 (C, D, A, B,  8,  9);
  OP2 (B, C, D, A, 12, 13);
  OP2 (A, B, C, D,  1,  3);
  OP2 (D, A, B, C,  5,  5);
  OP2 (C, D, A, B,  9,  9);
  OP2 (B, C, D, A, 13, 13);
  OP2 (A, B, C, D,  2,  3);
  OP2 (D, A, B, C,  6,  5);
  OP2 (C, D, A, B, 10,  9);
  OP2 (B, C, D, A, 14, 13);
  OP2 (A, B, C, D,  3,  3);
  OP2 (D, A, B, C,  7,  5);
  OP2 (C, D, A, B, 11,  9);
  OP2 (B, C, D, A, 15, 13);

  /* Round 3.  */
#define OP3(a,b,c,d,k,s) OP(FH,a,b,c,d,k,s,0x6ED9EBA1)
  OP3 (A, B, C, D,  0,  3);
  OP3 (D, A, B, C,  8,  9);
  OP3 (C, D, A, B,  4, 11);
  OP3 (B, C, D, A, 12, 15);
  OP3 (A, B, C, D,  2,  3);
  OP3 (D, A, B, C, 10,  9);
  OP3 (C, D, A, B,  6, 11);
  OP3 (B, C, D, A, 14, 15);
  OP3 (A, B, C, D,  1,  3);
  OP3 (D, A, B, C,  9,  9);
  OP3 (C, D, A, B,  5, 11);
  OP3 (B, C, D, A, 13, 15);
  OP3 (A, B, C, D,  3,  3);
  OP3 (D, A, B, C, 11,  9);
  OP3 (C, D, A, B,  7, 11);
  OP3 (B, C, D, A, 15, 15);

  /* Put checksum in context given as argument.  */
  ctx->A += A;
  ctx->B += B;
  ctx->C += C;
  ctx->D += D;
}


#ifdef MD4_SELFTEST
#include <stdio.h>
void test(const char* str, unsigned char hash[16]) 
{
  MD4_CTX ctx;
  unsigned char cmp[16];
  int i;
  md4_buffer(str, strlen(str), cmp);
  for (i = 0; i < 16; i++)
    if (cmp[i] != hash[i]) {
      printf("MD4 of '%s' failed\ngood=", str);
      for(i = 0; i < 16; i++)
	printf("%02x", hash[i]);
      printf("\ncalc=");
      for(i = 0; i < 16; i++)
	printf("%02x", cmp[i]);
      printf("\n");
      exit(1);
    }
}

int main(void)
{
  test("",
       "\x31\xd6\xcf\xe0\xd1\x6a\xe9\x31\xb7\x3c\x59\xd7\xe0\xc0\x89\xc0");
  test("a",
       "\xbd\xe5\x2c\xb3\x1d\xe3\x3e\x46\x24\x5e\x05\xfb\xdb\xd6\xfb\x24");
  test("abc",
       "\xa4\x48\x01\x7a\xaf\x21\xd8\x52\x5f\xc1\x0a\xe8\x7a\xa6\x72\x9d");
  test("message digest",
       "\xd9\x13\x0a\x81\x64\x54\x9f\xe8\x18\x87\x48\x06\xe1\xc7\x01\x4b");
  test("abcdefghijklmnopqrstuvwxyz",
       "\xd7\x9e\x1c\x30\x8a\xa5\xbb\xcd\xee\xa8\xed\x63\xdf\x41\x2d\xa9");
  test("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
       "\x04\x3f\x85\x82\xf2\x41\xdb\x35\x1c\xe6\x27\xe1\x53\xe7\xf0\xe4");
  test("123456789012345678901234567890123456789012345678901234567890123456"
       "78901234567890",
       "\xe3\x3b\x4d\xdc\x9c\x38\xf2\x19\x9c\x3e\x7b\x16\x4f\xcc\x05\x36");
}
#endif
