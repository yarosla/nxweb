/* The MIT License

   Copyright (C) 2011 Zilong Tan (tzlloch@gmail.com)

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

/*
  -------------------------------------------------------------------------------
  lookup3.c, by Bob Jenkins, May 2006, Public Domain.

  These are functions for producing 32-bit hashes for hash table lookup.
  hashword(), hashlittle(), hashlittle2(), hashbig(), mix(), and final()
  are externally useful functions.  Routines to test the hash are included
  if SELF_TEST is defined.  You can use this free for any purpose.  It's in
  the public domain.  It has no warranty.

  You probably want to use hashlittle().  hashlittle() and hashbig()
  hash byte arrays.  hashlittle() is is faster than hashbig() on
  little-endian machines.  Intel and AMD are little-endian machines.
  On second thought, you probably want hashlittle2(), which is identical to
  hashlittle() except it returns two 32-bit hashes for the price of one.
  You could implement hashbig2() if you wanted but I haven't bothered here.

  If you want to find a hash of, say, exactly 7 integers, do
  a = i1;  b = i2;  c = i3;
  mix(a,b,c);
  a += i4; b += i5; c += i6;
  mix(a,b,c);
  a += i7;
  final(a,b,c);
  then use c as the hash value.  If you have a variable length array of
  4-byte integers to hash, use hashword().  If you have a byte array (like
  a character string), use hashlittle().  If you have several byte arrays, or
  a mix of things, see the comments above hashlittle().

  Why is this so big?  I read 12 bytes at a time into 3 4-byte integers,
  then mix those integers.  This is fast (you can do a lot more thorough
  mixing with 12*3 instructions on 3 integers than you can with 3 instructions
  on 1 byte), but shoehorning those bytes into integers efficiently is messy.
  -------------------------------------------------------------------------------
*/
/* #define SELF_TEST 1 */

#include <time.h>       /* defines time_t for timings in the test */
#include <sys/param.h>  /* attempt to define endianness */
#ifdef linux
# include <endian.h>    /* attempt to define endianness */
#endif

#include <string.h>
#include "deps/ulib/hash.h"

/*
 * My best guess at if you are big-endian or little-endian.  This may
 * need adjustment.
 */
#if (defined(__BYTE_ORDER) && defined(__LITTLE_ENDIAN) &&		\
     __BYTE_ORDER == __LITTLE_ENDIAN) ||				\
	(defined(i386) || defined(__i386__) || defined(__i486__) ||	\
	 defined(__i586__) || defined(__i686__) || defined(vax) || defined(MIPSEL))
# define HASH_LITTLE_ENDIAN 1
# define HASH_BIG_ENDIAN 0
#elif (defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) &&		\
       __BYTE_ORDER == __BIG_ENDIAN) ||					\
	(defined(sparc) || defined(POWERPC) || defined(mc68000) || defined(sel))
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 1
#else
# define HASH_LITTLE_ENDIAN 0
# define HASH_BIG_ENDIAN 0
#endif

#define hashsize(n)   (1u << (n))
#define hashmask(n)   (hashsize(n) - 1)
#define rot(x,k)      (((x) << (k)) | ((x) >> (32 - (k))))

uint32_t hash_djb2(const unsigned char *str)
{
	uint32_t hash = 5381;
	int c;

	while ((c = *str++) != '\0')
		hash = ((hash << 5) + hash) + c;	/* hash * 33 + c */

	return hash;
}

uint32_t hash_sdbm(const unsigned char *str)
{
	uint32_t hash = 0;
	int c;

	while ((c = *str++) != '\0')
		hash = c + (hash << 6) + (hash << 16) - hash;

	return hash;
}

uint32_t hash_fnv32(const unsigned char *buf, size_t len)
{
	uint32_t hash = 2166136261UL;

	while (len-- > 0) {
		hash ^= *buf++;
		hash *= 16777619;
	}

	return hash;
}

uint64_t hash_fnv64(const unsigned char *buf, size_t len)
{
	uint64_t hash = 14695981039346656037ULL;

	while (len-- > 0) {
		hash ^= *buf++;
		hash *= 1099511628211ULL;
	}

	return hash;
}

uint64_t hash_murmur64(const unsigned char *buf, size_t len, uint64_t seed)
{
	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	const int r = 47;
	const uint64_t *data = (const uint64_t *)buf;
	const uint64_t *end = data + (len / 8);
	const unsigned char *data2;
	uint64_t h = seed ^ (len * m);
	uint64_t v;

	while (data != end) {
		v = *data++;
		v *= m;
		v ^= v >> r;
		v *= m;
		h ^= v;
		h *= m;
	}

	data2 = (const unsigned char*)data;

	switch (len & 7) {
	case 7: h ^= (uint64_t)data2[6] << 48;
	case 6: h ^= (uint64_t)data2[5] << 40;
	case 5: h ^= (uint64_t)data2[4] << 32;
	case 4: h ^= (uint64_t)data2[3] << 24;
	case 3: h ^= (uint64_t)data2[2] << 16;
	case 2: h ^= (uint64_t)data2[1] << 8;
	case 1: h ^= (uint64_t)data2[0];
		h *= m;
	}

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}

uint32_t hash_murmur32(const unsigned char *buf, size_t len, uint32_t seed)
{
	const uint32_t m = 0x5bd1e995;
	const int r = 24;
	const unsigned char *data = (const unsigned char *)buf;
	uint32_t h = seed ^ len;
	uint32_t v;

	while (len >= 4) {
		v = *(uint32_t *)data;
		v *= m;
		v ^= v >> r;
		v *= m;
		h *= m;
		h ^= v;
		data += 4;
		len -= 4;
	}

	switch (len) {
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
		h *= m;
	}

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

#if defined __x86_64__

uint64_t hash_crapwow64(const unsigned char *buf, uint64_t len, uint64_t seed)
{
	const uint64_t m = 0x95b47aa3355ba1a1, n = 0x8a970be7488fda55;
	uint64_t hash;
	/*
	  3 = m, 4 = n
	  r12 = h, r13 = k, ecx = seed, r12 = key
	*/
	__asm__ (
		"leaq (%%rcx,%4), %%r13\n"
		"movq %%rdx, %%r14\n"
		"movq %%rcx, %%r15\n"
		"movq %%rcx, %%r12\n"
		"addq %%rax, %%r13\n"
		"andq $0xfffffffffffffff0, %%rcx\n"
		"jz QW%=\n"
		"addq %%rcx, %%r14\n\n"
		"negq %%rcx\n"
		"XW%=:\n"
		"movq %4, %%rax\n"
		"mulq (%%r14,%%rcx)\n"
		"xorq %%rax, %%r12\n"
		"xorq %%rdx, %%r13\n"
		"movq %3, %%rax\n"
		"mulq 8(%%r14,%%rcx)\n"
		"xorq %%rdx, %%r12\n"
		"xorq %%rax, %%r13\n"
		"addq $16, %%rcx\n"
		"jnz XW%=\n"
		"QW%=:\n"
		"movq %%r15, %%rcx\n"
		"andq $8, %%r15\n"
		"jz B%=\n"
		"movq %4, %%rax\n"
		"mulq (%%r14)\n"
		"addq $8, %%r14\n"
		"xorq %%rax, %%r12\n"
		"xorq %%rdx, %%r13\n"
		"B%=:\n"
		"andq $7, %%rcx\n"
		"jz F%=\n"
		"movq $1, %%rdx\n"
		"shlq $3, %%rcx\n"
		"movq %3, %%rax\n"
		"shlq %%cl, %%rdx\n"
		"addq $-1, %%rdx\n"
		"andq (%%r14), %%rdx\n"
		"mulq %%rdx\n"
		"xorq %%rdx, %%r12\n"
		"xorq %%rax, %%r13\n"
		"F%=:\n"
		"leaq (%%r13,%4), %%rax\n"
		"xorq %%r12, %%rax\n"
		"mulq %4\n"
		"xorq %%rdx, %%rax\n"
		"xorq %%r12, %%rax\n"
		"xorq %%r13, %%rax\n"
		: "=a"(hash), "=c"(buf), "=d"(buf)
		: "r"(m), "r"(n), "a"(seed), "c"(len), "d"(buf)
		: "%r12", "%r13", "%r14", "%r15", "cc"
		);
	return hash;
}

uint32_t hash_crapwow32(const unsigned char *buf, uint32_t len, uint32_t seed)
{
	return (uint32_t) hash_crapwow64(buf, len, seed);
}

#endif  /*  __x86_64__ */

uint32_t hash_crc32(const unsigned char *buf, size_t len)
{
	uint_fast32_t crc = 0;
	size_t u;

	static uint_fast32_t const crctab[256] =
		{
			0x00000000,
			0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
			0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6,
			0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
			0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
			0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f,
			0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a,
			0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
			0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58,
			0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
			0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe,
			0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
			0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4,
			0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
			0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5,
			0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
			0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
			0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
			0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
			0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
			0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b,
			0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698,
			0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d,
			0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
			0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f,
			0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
			0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80,
			0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
			0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a,
			0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629,
			0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c,
			0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
			0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
			0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
			0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
			0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
			0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2,
			0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
			0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74,
			0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
			0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21,
			0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
			0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087,
			0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
			0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d,
			0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce,
			0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
			0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
			0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09,
			0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
			0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf,
			0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
		};

	for (u = 0; u < len; u++)
		crc = (crc << 8) ^ crctab[((crc >> 24) ^ buf[u]) & 0xFF];

	for (; len; len >>= 8)
		crc = (crc << 8) ^ crctab[((crc >> 24) ^ len) & 0xFF];

	crc = ~crc & 0xFFFFFFFF;

	return (uint32_t) crc;
}

/*
  -------------------------------------------------------------------------------
  mix -- mix 3 32-bit values reversibly.

  This is reversible, so any information in (a,b,c) before mix() is
  still in (a,b,c) after mix().

  If four pairs of (a,b,c) inputs are run through mix(), or through
  mix() in reverse, there are at least 32 bits of the output that
  are sometimes the same for one pair and different for another pair.
  This was tested for:
  * pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
  * "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
  * the base values were pseudorandom, all zero but one bit set, or
  all zero plus a counter that starts at zero.

  Some k values for my "a-=c; a^=rot(c,k); c+=b;" arrangement that
  satisfy this are
  4  6  8 16 19  4
  9 15  3 18 27 15
  14  9  3  7 17  3
  Well, "9 15 3 18 27 15" didn't quite get 32 bits diffing
  for "differ" defined as + with a one-bit base and a two-bit delta.  I
  used http://burtleburtle.net/bob/hash/avalanche.html to choose
  the operations, constants, and arrangements of the variables.

  This does not achieve avalanche.  There are input bits of (a,b,c)
  that fail to affect some output bits of (a,b,c), especially of a.  The
  most thoroughly mixed value is c, but it doesn't really even achieve
  avalanche in c.

  This allows some parallelism.  Read-after-writes are good at doubling
  the number of bits affected, so the goal of mixing pulls in the opposite
  direction as the goal of parallelism.  I did what I could.  Rotates
  seem to cost as much as shifts on every machine I could lay my hands
  on, and rotates are much kinder to the top and bottom bits, so I used
  rotates.
  -------------------------------------------------------------------------------
*/
#define mix(a,b,c)					\
	{						\
		a -= c;  a ^= rot(c, 4);  c += b;	\
		b -= a;  b ^= rot(a, 6);  a += c;	\
		c -= b;  c ^= rot(b, 8);  b += a;	\
		a -= c;  a ^= rot(c,16);  c += b;	\
		b -= a;  b ^= rot(a,19);  a += c;	\
		c -= b;  c ^= rot(b, 4);  b += a;	\
	}

/*
  -------------------------------------------------------------------------------
  final -- final mixing of 3 32-bit values (a,b,c) into c

  Pairs of (a,b,c) values differing in only a few bits will usually
  produce values of c that look totally different.  This was tested for
  * pairs that differed by one bit, by two bits, in any combination
  of top bits of (a,b,c), or in any combination of bottom bits of
  (a,b,c).
  * "differ" is defined as +, -, ^, or ~^.  For + and -, I transformed
  the output delta to a Gray code (a^(a>>1)) so a string of 1's (as
  is commonly produced by subtraction) look like a single 1-bit
  difference.
  * the base values were pseudorandom, all zero but one bit set, or
  all zero plus a counter that starts at zero.

  These constants passed:
  14 11 25 16 4 14 24
  12 14 25 16 4 14 24
  and these came close:
  4  8 15 26 3 22 24
  10  8 15 26 3 22 24
  11  8 15 26 3 22 24
  -------------------------------------------------------------------------------
*/
#define final(a,b,c)				\
	{					\
		c ^= b; c -= rot(b,14);		\
		a ^= c; a -= rot(c,11);		\
		b ^= a; b -= rot(a,25);		\
		c ^= b; c -= rot(b,16);		\
		a ^= c; a -= rot(c,4);		\
		b ^= a; b -= rot(a,14);		\
		c ^= b; c -= rot(b,24);		\
	}

/*
  --------------------------------------------------------------------
  This works on all machines.  To be useful, it requires
  -- that the key be an array of uint32_t's, and
  -- that the length be the number of uint32_t's in the key

  The function hashword() is identical to hashlittle() on little-endian
  machines, and identical to hashbig() on big-endian machines,
  except that the length has to be measured in uint32_ts rather than in
  bytes.  hashlittle() is more complicated than hashword() only because
  hashlittle() has to dance around fitting the key bytes into registers.
  --------------------------------------------------------------------
*/
uint32_t hashword(
	const uint32_t *k,                   /* the key, an array of uint32_t values */
	size_t          length,               /* the length of the key, in uint32_ts */
	uint32_t        initval)         /* the previous hash, or an arbitrary value */
{
	uint32_t a,b,c;

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + (((uint32_t)length)<<2) + initval;

	/*------------------------------------------------- handle most of the key */
	while (length > 3)
	{
		a += k[0];
		b += k[1];
		c += k[2];
		mix(a,b,c);
		length -= 3;
		k += 3;
	}

	/*------------------------------------------- handle the last 3 uint32_t's */
	switch(length)                     /* all the case statements fall through */
	{
	case 3 : c+=k[2];
	case 2 : b+=k[1];
	case 1 : a+=k[0];
		final(a,b,c);
	case 0:     /* case 0: nothing left to add */
		break;
	}
	/*------------------------------------------------------ report the result */
	return c;
}


/*
  --------------------------------------------------------------------
  hashword2() -- same as hashword(), but take two seeds and return two
  32-bit values.  pc and pb must both be nonnull, and *pc and *pb must
  both be initialized with seeds.  If you pass in (*pb)==0, the output
  (*pc) will be the same as the return value from hashword().
  --------------------------------------------------------------------
*/
void hashword2 (
	const uint32_t *k,                   /* the key, an array of uint32_t values */
	size_t          length,               /* the length of the key, in uint32_ts */
	uint32_t       *pc,                      /* IN: seed OUT: primary hash value */
	uint32_t       *pb)               /* IN: more seed OUT: secondary hash value */
{
	uint32_t a,b,c;

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + ((uint32_t)(length<<2)) + *pc;
	c += *pb;

	/*------------------------------------------------- handle most of the key */
	while (length > 3)
	{
		a += k[0];
		b += k[1];
		c += k[2];
		mix(a,b,c);
		length -= 3;
		k += 3;
	}

	/*------------------------------------------- handle the last 3 uint32_t's */
	switch(length)                     /* all the case statements fall through */
	{
	case 3 : c+=k[2];
	case 2 : b+=k[1];
	case 1 : a+=k[0];
		final(a,b,c);
	case 0:     /* case 0: nothing left to add */
		break;
	}
	/*------------------------------------------------------ report the result */
	*pc=c; *pb=b;
}


/*
  -------------------------------------------------------------------------------
  hashlittle() -- hash a variable-length key into a 32-bit value
  k       : the key (the unaligned variable-length array of bytes)
  length  : the length of the key, counting by bytes
  initval : can be any 4-byte value
  Returns a 32-bit value.  Every bit of the key affects every bit of
  the return value.  Two keys differing by one or two bits will have
  totally different hash values.

  The best hash table sizes are powers of 2.  There is no need to do
  mod a prime (mod is sooo slow!).  If you need less than 32 bits,
  use a bitmask.  For example, if you need only 10 bits, do
  h = (h & hashmask(10));
  In which case, the hash table should have hashsize(10) elements.

  If you are hashing n strings (uint8_t **)k, do it like this:
  for (i=0, h=0; i<n; ++i) h = hashlittle( k[i], len[i], h);

  By Bob Jenkins, 2006.  bob_jenkins@burtleburtle.net.  You may use this
  code any way you wish, private, educational, or commercial.  It's free.

  Use for hash table lookup, or anything where one collision in 2^^32 is
  acceptable.  Do NOT use for cryptographic purposes.
  -------------------------------------------------------------------------------
*/

uint32_t hashlittle( const void *key, size_t length, uint32_t initval)
{
	uint32_t a,b,c;                                          /* internal state */
	union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + ((uint32_t)length) + initval;

	u.ptr = key;
	if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
		const uint32_t *k = (const uint32_t *)key;         /* read 32-bit chunks */
		/* const uint8_t  *k8; */

		/*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
		while (length > 12)
		{
			a += k[0];
			b += k[1];
			c += k[2];
			mix(a,b,c);
			length -= 12;
			k += 3;
		}

		/*----------------------------- handle the last (probably partial) block */
		/*
		 * "k[2]&0xffffff" actually reads beyond the end of the string, but
		 * then masks off the part it's not allowed to read.  Because the
		 * string is aligned, the masked-off tail is in the same word as the
		 * rest of the string.  Every machine with memory protection I've seen
		 * does it on word boundaries, so is OK with this.  But VALGRIND will
		 * still catch it and complain.  The masking trick does make the hash
		 * noticably faster for short strings (like English words).
		 */
#ifndef VALGRIND

		switch(length)
		{
		case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
		case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
		case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
		case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
		case 8 : b+=k[1]; a+=k[0]; break;
		case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
		case 6 : b+=k[1]&0xffff; a+=k[0]; break;
		case 5 : b+=k[1]&0xff; a+=k[0]; break;
		case 4 : a+=k[0]; break;
		case 3 : a+=k[0]&0xffffff; break;
		case 2 : a+=k[0]&0xffff; break;
		case 1 : a+=k[0]&0xff; break;
		case 0 : return c;              /* zero length strings require no mixing */
		}

#else /* make valgrind happy */

		k8 = (const uint8_t *)k;
		switch(length)
		{
		case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
		case 11: c+=((uint32_t)k8[10])<<16;  /* fall through */
		case 10: c+=((uint32_t)k8[9])<<8;    /* fall through */
		case 9 : c+=k8[8];                   /* fall through */
		case 8 : b+=k[1]; a+=k[0]; break;
		case 7 : b+=((uint32_t)k8[6])<<16;   /* fall through */
		case 6 : b+=((uint32_t)k8[5])<<8;    /* fall through */
		case 5 : b+=k8[4];                   /* fall through */
		case 4 : a+=k[0]; break;
		case 3 : a+=((uint32_t)k8[2])<<16;   /* fall through */
		case 2 : a+=((uint32_t)k8[1])<<8;    /* fall through */
		case 1 : a+=k8[0]; break;
		case 0 : return c;
		}

#endif /* !valgrind */

	} else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
		const uint16_t *k = (const uint16_t *)key;         /* read 16-bit chunks */
		const uint8_t  *k8;

		/*--------------- all but last block: aligned reads and different mixing */
		while (length > 12)
		{
			a += k[0] + (((uint32_t)k[1])<<16);
			b += k[2] + (((uint32_t)k[3])<<16);
			c += k[4] + (((uint32_t)k[5])<<16);
			mix(a,b,c);
			length -= 12;
			k += 6;
		}

		/*----------------------------- handle the last (probably partial) block */
		k8 = (const uint8_t *)k;
		switch(length)
		{
		case 12: c+=k[4]+(((uint32_t)k[5])<<16);
			b+=k[2]+(((uint32_t)k[3])<<16);
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 11: c+=((uint32_t)k8[10])<<16;     /* fall through */
		case 10: c+=k[4];
			b+=k[2]+(((uint32_t)k[3])<<16);
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 9 : c+=k8[8];                      /* fall through */
		case 8 : b+=k[2]+(((uint32_t)k[3])<<16);
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 7 : b+=((uint32_t)k8[6])<<16;      /* fall through */
		case 6 : b+=k[2];
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 5 : b+=k8[4];                      /* fall through */
		case 4 : a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 3 : a+=((uint32_t)k8[2])<<16;      /* fall through */
		case 2 : a+=k[0];
			break;
		case 1 : a+=k8[0];
			break;
		case 0 : return c;                     /* zero length requires no mixing */
		}

	} else {                        /* need to read the key one byte at a time */
		const uint8_t *k = (const uint8_t *)key;

		/*--------------- all but the last block: affect some 32 bits of (a,b,c) */
		while (length > 12)
		{
			a += k[0];
			a += ((uint32_t)k[1])<<8;
			a += ((uint32_t)k[2])<<16;
			a += ((uint32_t)k[3])<<24;
			b += k[4];
			b += ((uint32_t)k[5])<<8;
			b += ((uint32_t)k[6])<<16;
			b += ((uint32_t)k[7])<<24;
			c += k[8];
			c += ((uint32_t)k[9])<<8;
			c += ((uint32_t)k[10])<<16;
			c += ((uint32_t)k[11])<<24;
			mix(a,b,c);
			length -= 12;
			k += 12;
		}

		/*-------------------------------- last block: affect all 32 bits of (c) */
		switch(length)                   /* all the case statements fall through */
		{
		case 12: c+=((uint32_t)k[11])<<24;
		case 11: c+=((uint32_t)k[10])<<16;
		case 10: c+=((uint32_t)k[9])<<8;
		case 9 : c+=k[8];
		case 8 : b+=((uint32_t)k[7])<<24;
		case 7 : b+=((uint32_t)k[6])<<16;
		case 6 : b+=((uint32_t)k[5])<<8;
		case 5 : b+=k[4];
		case 4 : a+=((uint32_t)k[3])<<24;
		case 3 : a+=((uint32_t)k[2])<<16;
		case 2 : a+=((uint32_t)k[1])<<8;
		case 1 : a+=k[0];
			break;
		case 0 : return c;
		}
	}

	final(a,b,c);
	return c;
}


/*
 * hashlittle2: return 2 32-bit hash values
 *
 * This is identical to hashlittle(), except it returns two 32-bit hash
 * values instead of just one.  This is good enough for hash table
 * lookup with 2^^64 buckets, or if you want a second hash if you're not
 * happy with the first, or if you want a probably-unique 64-bit ID for
 * the key.  *pc is better mixed than *pb, so use *pc first.  If you want
 * a 64-bit value do something like "*pc + (((uint64_t)*pb)<<32)".
 */
void hashlittle2(
	const void *key,       /* the key to hash */
	size_t      length,    /* length of the key */
	uint32_t   *pc,        /* IN: primary initval, OUT: primary hash */
	uint32_t   *pb)        /* IN: secondary initval, OUT: secondary hash */
{
	uint32_t a,b,c;                                          /* internal state */
	union { const void *ptr; size_t i; } u;     /* needed for Mac Powerbook G4 */

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + ((uint32_t)length) + *pc;
	c += *pb;

	u.ptr = key;
	if (HASH_LITTLE_ENDIAN && ((u.i & 0x3) == 0)) {
		const uint32_t *k = (const uint32_t *)key;         /* read 32-bit chunks */
		/* const uint8_t  *k8; */

		/*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
		while (length > 12)
		{
			a += k[0];
			b += k[1];
			c += k[2];
			mix(a,b,c);
			length -= 12;
			k += 3;
		}

		/*----------------------------- handle the last (probably partial) block */
		/*
		 * "k[2]&0xffffff" actually reads beyond the end of the string, but
		 * then masks off the part it's not allowed to read.  Because the
		 * string is aligned, the masked-off tail is in the same word as the
		 * rest of the string.  Every machine with memory protection I've seen
		 * does it on word boundaries, so is OK with this.  But VALGRIND will
		 * still catch it and complain.  The masking trick does make the hash
		 * noticably faster for short strings (like English words).
		 */
#ifndef VALGRIND

		switch(length)
		{
		case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
		case 11: c+=k[2]&0xffffff; b+=k[1]; a+=k[0]; break;
		case 10: c+=k[2]&0xffff; b+=k[1]; a+=k[0]; break;
		case 9 : c+=k[2]&0xff; b+=k[1]; a+=k[0]; break;
		case 8 : b+=k[1]; a+=k[0]; break;
		case 7 : b+=k[1]&0xffffff; a+=k[0]; break;
		case 6 : b+=k[1]&0xffff; a+=k[0]; break;
		case 5 : b+=k[1]&0xff; a+=k[0]; break;
		case 4 : a+=k[0]; break;
		case 3 : a+=k[0]&0xffffff; break;
		case 2 : a+=k[0]&0xffff; break;
		case 1 : a+=k[0]&0xff; break;
		case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
		}

#else /* make valgrind happy */

		k8 = (const uint8_t *)k;
		switch(length)
		{
		case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
		case 11: c+=((uint32_t)k8[10])<<16;  /* fall through */
		case 10: c+=((uint32_t)k8[9])<<8;    /* fall through */
		case 9 : c+=k8[8];                   /* fall through */
		case 8 : b+=k[1]; a+=k[0]; break;
		case 7 : b+=((uint32_t)k8[6])<<16;   /* fall through */
		case 6 : b+=((uint32_t)k8[5])<<8;    /* fall through */
		case 5 : b+=k8[4];                   /* fall through */
		case 4 : a+=k[0]; break;
		case 3 : a+=((uint32_t)k8[2])<<16;   /* fall through */
		case 2 : a+=((uint32_t)k8[1])<<8;    /* fall through */
		case 1 : a+=k8[0]; break;
		case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
		}

#endif /* !valgrind */

	} else if (HASH_LITTLE_ENDIAN && ((u.i & 0x1) == 0)) {
		const uint16_t *k = (const uint16_t *)key;         /* read 16-bit chunks */
		const uint8_t  *k8;

		/*--------------- all but last block: aligned reads and different mixing */
		while (length > 12)
		{
			a += k[0] + (((uint32_t)k[1])<<16);
			b += k[2] + (((uint32_t)k[3])<<16);
			c += k[4] + (((uint32_t)k[5])<<16);
			mix(a,b,c);
			length -= 12;
			k += 6;
		}

		/*----------------------------- handle the last (probably partial) block */
		k8 = (const uint8_t *)k;
		switch(length)
		{
		case 12: c+=k[4]+(((uint32_t)k[5])<<16);
			b+=k[2]+(((uint32_t)k[3])<<16);
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 11: c+=((uint32_t)k8[10])<<16;     /* fall through */
		case 10: c+=k[4];
			b+=k[2]+(((uint32_t)k[3])<<16);
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 9 : c+=k8[8];                      /* fall through */
		case 8 : b+=k[2]+(((uint32_t)k[3])<<16);
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 7 : b+=((uint32_t)k8[6])<<16;      /* fall through */
		case 6 : b+=k[2];
			a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 5 : b+=k8[4];                      /* fall through */
		case 4 : a+=k[0]+(((uint32_t)k[1])<<16);
			break;
		case 3 : a+=((uint32_t)k8[2])<<16;      /* fall through */
		case 2 : a+=k[0];
			break;
		case 1 : a+=k8[0];
			break;
		case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
		}

	} else {                        /* need to read the key one byte at a time */
		const uint8_t *k = (const uint8_t *)key;

		/*--------------- all but the last block: affect some 32 bits of (a,b,c) */
		while (length > 12)
		{
			a += k[0];
			a += ((uint32_t)k[1])<<8;
			a += ((uint32_t)k[2])<<16;
			a += ((uint32_t)k[3])<<24;
			b += k[4];
			b += ((uint32_t)k[5])<<8;
			b += ((uint32_t)k[6])<<16;
			b += ((uint32_t)k[7])<<24;
			c += k[8];
			c += ((uint32_t)k[9])<<8;
			c += ((uint32_t)k[10])<<16;
			c += ((uint32_t)k[11])<<24;
			mix(a,b,c);
			length -= 12;
			k += 12;
		}

		/*-------------------------------- last block: affect all 32 bits of (c) */
		switch(length)                   /* all the case statements fall through */
		{
		case 12: c+=((uint32_t)k[11])<<24;
		case 11: c+=((uint32_t)k[10])<<16;
		case 10: c+=((uint32_t)k[9])<<8;
		case 9 : c+=k[8];
		case 8 : b+=((uint32_t)k[7])<<24;
		case 7 : b+=((uint32_t)k[6])<<16;
		case 6 : b+=((uint32_t)k[5])<<8;
		case 5 : b+=k[4];
		case 4 : a+=((uint32_t)k[3])<<24;
		case 3 : a+=((uint32_t)k[2])<<16;
		case 2 : a+=((uint32_t)k[1])<<8;
		case 1 : a+=k[0];
			break;
		case 0 : *pc=c; *pb=b; return;  /* zero length strings require no mixing */
		}
	}

	final(a,b,c);
	*pc=c; *pb=b;
}



/*
 * hashbig():
 * This is the same as hashword() on big-endian machines.  It is different
 * from hashlittle() on all machines.  hashbig() takes advantage of
 * big-endian byte ordering.
 */
uint32_t hashbig( const void *key, size_t length, uint32_t initval)
{
	uint32_t a,b,c;
	union { const void *ptr; size_t i; } u; /* to cast key to (size_t) happily */

	/* Set up the internal state */
	a = b = c = 0xdeadbeef + ((uint32_t)length) + initval;

	u.ptr = key;
	if (HASH_BIG_ENDIAN && ((u.i & 0x3) == 0)) {
		const uint32_t *k = (const uint32_t *)key;         /* read 32-bit chunks */
		/* const uint8_t  *k8; */

		/*------ all but last block: aligned reads and affect 32 bits of (a,b,c) */
		while (length > 12)
		{
			a += k[0];
			b += k[1];
			c += k[2];
			mix(a,b,c);
			length -= 12;
			k += 3;
		}

		/*----------------------------- handle the last (probably partial) block */
		/*
		 * "k[2]<<8" actually reads beyond the end of the string, but
		 * then shifts out the part it's not allowed to read.  Because the
		 * string is aligned, the illegal read is in the same word as the
		 * rest of the string.  Every machine with memory protection I've seen
		 * does it on word boundaries, so is OK with this.  But VALGRIND will
		 * still catch it and complain.  The masking trick does make the hash
		 * noticably faster for short strings (like English words).
		 */
#ifndef VALGRIND

		switch(length)
		{
		case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
		case 11: c+=k[2]&0xffffff00; b+=k[1]; a+=k[0]; break;
		case 10: c+=k[2]&0xffff0000; b+=k[1]; a+=k[0]; break;
		case 9 : c+=k[2]&0xff000000; b+=k[1]; a+=k[0]; break;
		case 8 : b+=k[1]; a+=k[0]; break;
		case 7 : b+=k[1]&0xffffff00; a+=k[0]; break;
		case 6 : b+=k[1]&0xffff0000; a+=k[0]; break;
		case 5 : b+=k[1]&0xff000000; a+=k[0]; break;
		case 4 : a+=k[0]; break;
		case 3 : a+=k[0]&0xffffff00; break;
		case 2 : a+=k[0]&0xffff0000; break;
		case 1 : a+=k[0]&0xff000000; break;
		case 0 : return c;              /* zero length strings require no mixing */
		}

#else  /* make valgrind happy */

		k8 = (const uint8_t *)k;
		switch(length)                   /* all the case statements fall through */
		{
		case 12: c+=k[2]; b+=k[1]; a+=k[0]; break;
		case 11: c+=((uint32_t)k8[10])<<8;  /* fall through */
		case 10: c+=((uint32_t)k8[9])<<16;  /* fall through */
		case 9 : c+=((uint32_t)k8[8])<<24;  /* fall through */
		case 8 : b+=k[1]; a+=k[0]; break;
		case 7 : b+=((uint32_t)k8[6])<<8;   /* fall through */
		case 6 : b+=((uint32_t)k8[5])<<16;  /* fall through */
		case 5 : b+=((uint32_t)k8[4])<<24;  /* fall through */
		case 4 : a+=k[0]; break;
		case 3 : a+=((uint32_t)k8[2])<<8;   /* fall through */
		case 2 : a+=((uint32_t)k8[1])<<16;  /* fall through */
		case 1 : a+=((uint32_t)k8[0])<<24; break;
		case 0 : return c;
		}

#endif /* !VALGRIND */

	} else {                        /* need to read the key one byte at a time */
		const uint8_t *k = (const uint8_t *)key;

		/*--------------- all but the last block: affect some 32 bits of (a,b,c) */
		while (length > 12)
		{
			a += ((uint32_t)k[0])<<24;
			a += ((uint32_t)k[1])<<16;
			a += ((uint32_t)k[2])<<8;
			a += ((uint32_t)k[3]);
			b += ((uint32_t)k[4])<<24;
			b += ((uint32_t)k[5])<<16;
			b += ((uint32_t)k[6])<<8;
			b += ((uint32_t)k[7]);
			c += ((uint32_t)k[8])<<24;
			c += ((uint32_t)k[9])<<16;
			c += ((uint32_t)k[10])<<8;
			c += ((uint32_t)k[11]);
			mix(a,b,c);
			length -= 12;
			k += 12;
		}

		/*-------------------------------- last block: affect all 32 bits of (c) */
		switch(length)                   /* all the case statements fall through */
		{
		case 12: c+=k[11];
		case 11: c+=((uint32_t)k[10])<<8;
		case 10: c+=((uint32_t)k[9])<<16;
		case 9 : c+=((uint32_t)k[8])<<24;
		case 8 : b+=k[7];
		case 7 : b+=((uint32_t)k[6])<<8;
		case 6 : b+=((uint32_t)k[5])<<16;
		case 5 : b+=((uint32_t)k[4])<<24;
		case 4 : a+=k[3];
		case 3 : a+=((uint32_t)k[2])<<8;
		case 2 : a+=((uint32_t)k[1])<<16;
		case 1 : a+=((uint32_t)k[0])<<24;
			break;
		case 0 : return c;
		}
	}

	final(a,b,c);
	return c;
}

