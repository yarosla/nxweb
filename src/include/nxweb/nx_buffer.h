/*
 * Copyright (c) 2011-2012 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXWEB.
 *
 * NXWEB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXWEB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXWEB. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NX_BUFFER_H_INCLUDED
#define NX_BUFFER_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "nx_alloc.h"
#include "misc.h"

typedef struct nxb_chunk {
  char* end;
  struct nxb_chunk* prev;
  _Bool should_free:1;
  _Bool dirty:1;
  char buf[3];
} nxb_chunk;

typedef struct nxb_buffer {
  char* base;
  char* ptr;
  char* end;
  struct nxb_chunk* chunk;
} nxb_buffer;

void nxb_init(nxb_buffer* nxb, int nxb_allocated_size);
nxb_buffer* nxb_create(int initial_chunk_size);
void nxb_empty(nxb_buffer* nxb);
void nxb_destroy(nxb_buffer* nxb);
void* nxb_alloc_obj(nxb_buffer* nxb, int size);
int nxb_printf_va(nxb_buffer* nxb, const char* fmt, va_list ap);
int nxb_printf(nxb_buffer* nxb, const char* fmt, ...) __attribute__((format (printf, 2, 3)));
int nxb_realloc_chunk(nxb_buffer* nxb, int min_room);

static inline void* nxb_calloc_obj(nxb_buffer* nxb, int size) {
  void* obj=nxb_alloc_obj(nxb, size);
  memset(obj, 0, size);
  return obj;
}

static inline void* nxb_copy_obj(nxb_buffer* nxb, const void* src, int size) {
  void* obj=nxb_alloc_obj(nxb, size);
  memcpy(obj, src, size);
  return obj;
}

static inline void* nxb_copy_str(nxb_buffer* nxb, const void* src) {
  return nxb_copy_obj(nxb, src, strlen((const char*)src)+1);
}

static inline char* nxb_finish_stream(nxb_buffer* nxb, int* size) {
  if (size) *size=nxb->ptr - nxb->base;
  char* obj=nxb->base;
  nxb->base=nxb->ptr;
  nxb->chunk->dirty=1;
  return obj;
}

static inline char* nxb_finish_partial(nxb_buffer* nxb, int size) {
  //assert(size>=0 && nxb->ptr - nxb->base >= size);
  char* obj=nxb->base;
  nxb->base+=size;
  nxb->chunk->dirty=1;
  return obj;
}

static inline void nxb_unfinish_stream(nxb_buffer* nxb) {
  nxb->ptr=nxb->base;
}

static inline void nxb_start_stream(nxb_buffer* nxb) {
  if (nxb->ptr!=nxb->base) {
    nxweb_log_warning("unfinished stream found in nxb %p", nxb);
    nxb_unfinish_stream(nxb);
  }
}

static inline void* nxb_get_room(nxb_buffer* nxb, int* room_size) {
  if (room_size) *room_size=nxb->end - nxb->ptr;
  return nxb->ptr;
}

static inline void* nxb_get_unfinished(nxb_buffer* nxb, int* size) {
  if (size) *size=nxb->ptr - nxb->base;
  return nxb->base;
}

static inline int nxb_make_room(nxb_buffer* nxb, int min_size) {
  if (nxb->end - nxb->ptr < min_size) return nxb_realloc_chunk(nxb, min_size);
  else return 0;
}

static inline void nxb_append_char(nxb_buffer* nxb, char c) {
  if (nxb->end - nxb->ptr < 1) {
    if (nxb_realloc_chunk(nxb, 1)) return;
  }
  *nxb->ptr++=c;
}

static inline void nxb_append_char_fast(nxb_buffer* nxb, char c) {
  *nxb->ptr++=c;
}

static inline void nxb_append(nxb_buffer* nxb, const void* ptr, int size) {
  if (nxb->end - nxb->ptr < size) {
    if (nxb_realloc_chunk(nxb, size)) return;
  }
  memcpy(nxb->ptr, ptr, size);
  nxb->ptr+=size;
}

static inline void nxb_append_fast(nxb_buffer* nxb, const void* ptr, int size) {
  memcpy(nxb->ptr, ptr, size);
  nxb->ptr+=size;
}

static inline void nxb_append_str(nxb_buffer* nxb, const char* str) {
  nxb_append(nxb, str, strlen(str));
}

static inline void nxb_append_str_fast(nxb_buffer* nxb, const char* str) {
  nxb_append_fast(nxb, str, strlen(str));
}

#define MAX_UINT_LEN 24

static inline void nxb_append_uint(nxb_buffer* nxb, unsigned long n) {
  if (nxb->end - nxb->ptr < MAX_UINT_LEN) {
    if (nxb_realloc_chunk(nxb, MAX_UINT_LEN)) return;
  }
  char* p=uint_to_decimal_string(n, nxb->ptr, MAX_UINT_LEN);
  int plen=strlen(p);
  memmove(nxb->ptr, p, plen);
  nxb->ptr+=plen;
}

static inline void nxb_append_uint_hex_zeropad(nxb_buffer* nxb, unsigned long n, int num_digits) {
  nxb_make_room(nxb, num_digits);
  char* p=uint_to_hex_string_zeropad(n, nxb->ptr, num_digits, 0);
  nxb->ptr+=num_digits;
}

static inline void nxb_append_uint64_hex_zeropad(nxb_buffer* nxb, uint64_t n, int num_digits) {
  nxb_make_room(nxb, num_digits);
  char* p=uint64_to_hex_string_zeropad(n, nxb->ptr, num_digits, 0);
  nxb->ptr+=num_digits;
}

static inline void nxb_blank(nxb_buffer* nxb, int size) {
  if (nxb->end - nxb->ptr < size) {
    if (nxb_realloc_chunk(nxb, size)) return;
  }
  nxb->ptr+=size;
}

static inline void nxb_blank_fast(nxb_buffer* nxb, int size) {
  nxb->ptr+=size;
}

static inline void nxb_resize_fast(nxb_buffer* nxb, int size) {
  nxb->ptr=nxb->base+size;
}


#ifdef __cplusplus
}
#endif
#endif // NX_BUFFER_H_INCLUDED
