/* The MIT License

   Copyright (C) 2011 Zilong Tan (tzlloch@gmail.com)
   Copyright (c) 2008, 2009, 2011 by Attractive Chaos <attractor@live.co.uk>

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

#ifndef __ULIB_ALIGN_HASHING_H
#define __ULIB_ALIGN_HASHING_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#ifdef AH_64BIT  /* specify if you are handling >4G keys */

typedef uint64_t ah_iter_t;
typedef uint64_t ah_size_t;

#define AH_ISDEL(flag, i)        ( ((flag)[(i) >> 5] >> (((i) & 0x1fU) << 1)) & 1      )
#define AH_ISEMPTY(flag, i)      ( ((flag)[(i) >> 5] >> (((i) & 0x1fU) << 1)) & 2      )
#define AH_ISEITHER(flag, i)     ( ((flag)[(i) >> 5] >> (((i) & 0x1fU) << 1)) & 3      )
#define AH_CLEAR_DEL(flag, i)    (  (flag)[(i) >> 5] &= ~(1ul << (((i) & 0x1fU) << 1)) )
#define AH_CLEAR_EMPTY(flag, i)  (  (flag)[(i) >> 5] &= ~(2ul << (((i) & 0x1fU) << 1)) )
#define AH_CLEAR_BOTH(flag, i)   (  (flag)[(i) >> 5] &= ~(3ul << (((i) & 0x1fU) << 1)) )
#define AH_SET_DEL(flag, i)      (  (flag)[(i) >> 5] |=  (1ul << (((i) & 0x1fU) << 1)) )

#define AH_FLAGS_BYTE(nb)        ( (nb) < 32? 8: (nb) >> 2 )

#else  /* by default, the 32-bit version is used */

typedef uint32_t ah_iter_t;
typedef uint32_t ah_size_t;

#define AH_ISDEL(flag, i)        ( ((flag)[(i) >> 4] >> (((i) & 0xfU) << 1)) & 1      )
#define AH_ISEMPTY(flag, i)      ( ((flag)[(i) >> 4] >> (((i) & 0xfU) << 1)) & 2      )
#define AH_ISEITHER(flag, i)     ( ((flag)[(i) >> 4] >> (((i) & 0xfU) << 1)) & 3      )
#define AH_CLEAR_DEL(flag, i)    (  (flag)[(i) >> 4] &= ~(1ul << (((i) & 0xfU) << 1)) )
#define AH_CLEAR_EMPTY(flag, i)  (  (flag)[(i) >> 4] &= ~(2ul << (((i) & 0xfU) << 1)) )
#define AH_CLEAR_BOTH(flag, i)   (  (flag)[(i) >> 4] &= ~(3ul << (((i) & 0xfU) << 1)) )
#define AH_SET_DEL(flag, i)      (  (flag)[(i) >> 4] |=  (1ul << (((i) & 0xfU) << 1)) )

#define AH_FLAGS_BYTE(nb)        ( (nb) < 16? 4: (nb) >> 2 )

#endif

/* return codes for alignhash_set() */
enum {
	AH_INS_ERR = 0,  /**< insertion failed, the element to insert exists */
	AH_INS_NEW = 1,  /**< inserted element is placed at a new bucket */
	AH_INS_DEL = 2   /**< inserted element is placed at a deleted bucket */
};

/* Two probing methods are available, tier probing and linear
 * probing. In general, tier probing has more stable lookup
 * performance than linear probing, due to the enhanced collision
 * resolution mechanism. However, linear probing yieds faster lookups
 * for relatively random keys. You can specify AH_TIER_PROBING to
 * enable tier probing, otherwise linear probing is used by default. */
#ifdef AH_TIER_PROBING
/* tier probing step, eligible for memory-efficient cases */
#define AH_PROBE_STEP(h)         ( ((h) ^ (h) >> 13) | 1 )
#define AH_LOAD_FACTOR           0.80
#else
/* linear probing, for fast lookups */
#define AH_PROBE_STEP(h)         ( 1 )
#define AH_LOAD_FACTOR           0.50
#endif

#define DECLARE_ALIGNHASH(_name, _key_t, _val_t, _ismap, _hashfn, _hasheq) \
	typedef struct {						\
		ah_size_t nbucket;					\
		ah_size_t size;     /* number of elements */		\
		ah_size_t nused;    /* number of bucket used */		\
		ah_size_t sup;      /* upper bound */			\
		ah_size_t *flags;					\
		_key_t    *keys;					\
		_val_t    *vals;					\
	} alignhash_##_name##_t;					\
                                                                        \
	static inline alignhash_##_name##_t *alignhash_init_##_name() {	\
		return (alignhash_##_name##_t*)				\
			calloc(1, sizeof(alignhash_##_name##_t));	\
	}								\
                                                                        \
	static inline void alignhash_destroy_##_name(alignhash_##_name##_t *h) \
	{								\
		if (h) {						\
			free(h->flags);					\
			free(h->keys);					\
			free(h->vals);					\
			free(h);					\
		}							\
	}								\
                                                                        \
	static inline void alignhash_clear_##_name(alignhash_##_name##_t *h) \
	{								\
		if (h && h->flags) {					\
			memset(h->flags, 0xaa, AH_FLAGS_BYTE(h->nbucket)); \
			h->size  = 0;					\
			h->nused = 0;					\
		}							\
	}								\
                                                                        \
	static inline ah_iter_t						\
	alignhash_get_##_name(const alignhash_##_name##_t *h, _key_t key) \
	{								\
		if (h->nbucket) {					\
			register ah_size_t i, step;			\
			ah_size_t mask = h->nbucket - 1;		\
			ah_size_t k, last;				\
			k = _hashfn(key);				\
			i = k & mask;					\
			step = AH_PROBE_STEP(k);			\
			last = i;					\
			while (!AH_ISEMPTY(h->flags, i) &&		\
			       (AH_ISDEL(h->flags, i) || !_hasheq(h->keys[i], key))) { \
				i = (i + step) & mask;			\
				if (i == last)				\
					return h->nbucket;		\
			}						\
			return AH_ISEMPTY(h->flags, i)? h->nbucket : i;	\
		} else							\
			return 0;					\
	}								\
                                                                        \
	static inline int						\
	alignhash_resize_##_name(alignhash_##_name##_t *h, ah_size_t new_nbucket) \
	{								\
		ah_size_t *new_flags = 0;				\
		_key_t    *new_keys  = 0;				\
		_val_t    *new_vals  = 0;				\
		ah_size_t  new_mask  = new_nbucket - 1;			\
		ah_size_t  j, flaglen;					\
		if (h->size >= (ah_size_t)(new_nbucket * AH_LOAD_FACTOR + 0.5))	\
			return -1;					\
		flaglen = AH_FLAGS_BYTE(new_nbucket);			\
		new_flags = (ah_size_t *) malloc(flaglen);		\
		if (new_flags == 0)					\
			return -1;					\
		memset(new_flags, 0xaa, flaglen);			\
		if (h->nbucket < new_nbucket) {				\
			new_keys = (_key_t*)				\
				realloc(h->keys, new_nbucket * sizeof(_key_t));	\
			if (new_keys == 0) {				\
				free(new_flags);			\
				return -1;				\
			}						\
			h->keys = new_keys;				\
			if (_ismap) {					\
				new_vals = (_val_t*)			\
					realloc(h->vals, new_nbucket * sizeof(_val_t)); \
				if (new_vals == 0) {			\
					free(new_flags);		\
					return -1;			\
				}					\
				h->vals = new_vals;			\
			}						\
		}							\
		for (j = 0; j != h->nbucket; ++j) {			\
			if (AH_ISEITHER(h->flags, j) == 0) {		\
				_key_t key = h->keys[j];		\
				_val_t val;				\
				if (_ismap)				\
					val = h->vals[j];		\
				AH_SET_DEL(h->flags, j);		\
				for (;;) {				\
					register ah_size_t i, step;	\
					ah_size_t k;			\
					k = _hashfn(key);		\
					i = k & new_mask;		\
					step = AH_PROBE_STEP(k);	\
					while (!AH_ISEMPTY(new_flags, i)) \
						i = (i + step) & new_mask; \
					AH_CLEAR_EMPTY(new_flags, i);	\
					if (i < h->nbucket && AH_ISEITHER(h->flags, i) == 0) { \
						swap(h->keys[i], key); \
						if (_ismap)		\
							swap(h->vals[i], val); \
						AH_SET_DEL(h->flags, i); \
					} else {			\
						h->keys[i] = key;	\
						if (_ismap)		\
							h->vals[i] = val; \
						break;			\
					}				\
				}					\
			}						\
		}							\
		if (h->nbucket > new_nbucket) {				\
			new_keys = (_key_t*)				\
				realloc(h->keys, new_nbucket * sizeof(_key_t));	\
			if (new_keys)					\
				h->keys = new_keys;			\
			if (_ismap) {					\
				new_vals = (_val_t*)			\
					realloc(h->vals, new_nbucket * sizeof(_val_t)); \
				if (new_vals)				\
					h->vals = new_vals;		\
			}						\
		}							\
		free(h->flags);						\
		h->flags = new_flags;					\
		h->nbucket = new_nbucket;				\
		h->nused = h->size;					\
		h->sup = (ah_size_t)(h->nbucket * AH_LOAD_FACTOR + 0.5); \
		return 0;						\
	}								\
                                                                        \
	static inline ah_iter_t						\
	alignhash_set_##_name(alignhash_##_name##_t *h, _key_t key, int *ret) \
	{								\
		register ah_size_t i, step;				\
		ah_size_t x, k, mask, site, last;			\
		if (h->nused >= h->sup) {				\
			if (h->nbucket) {				\
				if (alignhash_resize_##_name(h, h->nbucket * 2)) \
					return h->nbucket;		\
			} else {					\
				if (alignhash_resize_##_name(h, 2))	\
					return h->nbucket;		\
			}						\
		}							\
		site = h->nbucket;					\
		mask = h->nbucket - 1;					\
		x = site;						\
		k = _hashfn(key);					\
		i = k & mask;						\
		if (AH_ISEMPTY(h->flags, i))				\
			x = i;						\
		else {							\
			step = AH_PROBE_STEP(k);			\
			last = i;					\
			while (!AH_ISEMPTY(h->flags, i) &&		\
			       (AH_ISDEL(h->flags, i) || !_hasheq(h->keys[i], key))) { \
				if (AH_ISDEL(h->flags, i))		\
					site = i;			\
				i = (i + step) & mask;			\
				if (i == last) {			\
					x = site;			\
					break;				\
				}					\
			}						\
			if (x == h->nbucket) {				\
				if (AH_ISEMPTY(h->flags, i) && site != h->nbucket) \
					x = site;			\
				else					\
					x = i;				\
			}						\
		}							\
		if (AH_ISEMPTY(h->flags, x)) {				\
			h->keys[x] = key;				\
			AH_CLEAR_BOTH(h->flags, x);			\
			++h->size;					\
			++h->nused;					\
			*ret = AH_INS_NEW;				\
		} else if (AH_ISDEL(h->flags, x)) {			\
			h->keys[x] = key;				\
			AH_CLEAR_BOTH(h->flags, x);			\
			++h->size;					\
			*ret = AH_INS_DEL;				\
		} else							\
			*ret = AH_INS_ERR;				\
		return x;						\
	}								\
                                                                        \
	static inline void						\
	alignhash_del_##_name(alignhash_##_name##_t *h, ah_iter_t x)	\
	{								\
		if (x != h->nbucket && !AH_ISEITHER(h->flags, x)) {	\
			AH_SET_DEL(h->flags, x);			\
			--h->size;					\
		}							\
	}


/*------------------------- Human Interfaces -------------------------*/


/**
 * alignhash_hashfn - naive hash function
 * NOTE: does no mixing of bits
 */
#define alignhash_hashfn(key) (ah_size_t)(key)

/**
 * alignhash_equalfn - naive equality test function
 */
#define alignhash_equalfn(a, b) ((a) == (b))

/**
 * alignhash_t - aligned hash type
 */
#define alignhash_t(name) alignhash_##name##_t

/**
 * alignhash_key - retrieves the key of an iterator
 * @h: pointer to aligned hash
 * @x: the iterator
 */
#define alignhash_key(h, x) ((h)->keys[x])

/**
 * alignhash_value - retrieves the value of an iterator
 * @h: pointer to aligned hash
 * @x: the iterator
 */
#define alignhash_value(h, x) ((h)->vals[x])

/**
 * alignhash_init - initializes an aligned hash
 * @name:   name of the aligned hash
 * @return: returns allocated aligned hash
 */
#define alignhash_init(name) alignhash_init_##name()

/**
 * alignhash_destroy - destroys an aligned hash
 * @name: name of the aligned hash
 * @h:    pointer to allocated aligned hash
 */
#define alignhash_destroy(name, h) alignhash_destroy_##name(h)

/**
 * alignhash_clear - clears an aligned hash without memory remapping
 * @name: name of the aligned hash
 * @h:    pointer to allocated aligned hash
 */
#define alignhash_clear(name, h) alignhash_clear_##name(h)

/**
 * alignhash_resize - resizes an aligned hash
 * @name: name of the aligned hash
 * @h:    pointer to allocated aligned hash
 * @s:    new number of buckets
 * @r:    ln(new_nbucket/2)/ln2
 * NOTE:  bucket size should be in power of 2
 *        In general, this function should never be called outside
 */
#define alignhash_resize(name, h, s, r) alignhash_resize_##name(h, s, r)

/**
 * alignhash_set - inserts an element
 * @name:  name of the aligned hash
 * @h:     pointer to allocated aligned hash
 * @k:     key of the element to insert
 * @r:     where to store the insertion result
 * NOTE:   the insertion result, which is defined as AH_INS_*, will be
 * returned through @r. This function does not displace an existing
 * element. Displacement can be implemented using 'get' operation.
 * @return:returnns an iterator to the new element
 */
#define alignhash_set(name, h, k, r) alignhash_set_##name(h, k, r)

/**
 * alignhash_get - retrieves the iterator of an element
 * @name:  name of the aligned hash
 * @h:     pointer to allocated aligned hash
 * @k:     key of the element to retrieve
 * @return:returnns an iterator to the specified element
 */
#define alignhash_get(name, h, k) alignhash_get_##name(h, k)

/**
 * alignhash_del - deletes an element via its iterator
 * @name:  name of the aligned hash
 * @h:     pointer to allocated aligned hash
 * @x:     iterator of the element to delete
 */
#define alignhash_del(name, h, x) alignhash_del_##name(h, x)

/**
 * alignhash_exist - tests if an iterator contains data
 * @h: pointer to allocated aligned hash
 * @x: iterator to the bucket
 */
#define alignhash_exist(h, x) (!AH_ISEITHER((h)->flags, (x)))

/**
 * alignhash_exist - gets the start iterator
 * @h: pointer to allocated aligned hash
 */
#define alignhash_begin(h) (ah_iter_t)(0)

/**
 * alignhash_end - returns the sentinel/invalid iterator
 * @h: pointer to allocated aligned hash
 */
#define alignhash_end(h) ((h)->nbucket)

/**
 * alignhash_size - retrieves the size of an aligned hash
 * @h: pointer to allocated aligned hash
 */
#define alignhash_size(h) ((h)->size)

/**
 * alignhash_nbucket - retrieves the number of buckets
 * @h: pointer to allocated aligned hash
 */
#define alignhash_nbucket(h) ((h)->nbucket)

#endif  /* __ULIB_ALIGN_HASHING_H */
