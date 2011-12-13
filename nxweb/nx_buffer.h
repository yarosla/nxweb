/*
 * Copyright (c) 2011 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef NX_BUFFER_H_INCLUDED
#define NX_BUFFER_H_INCLUDED

#include <string.h>
#include <stdarg.h>

typedef struct nxb_chunk {
  char* end;
  struct nxb_chunk* prev;
  unsigned should_free:1;
  unsigned dirty:1;
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

static inline char* nxb_finish_stream(nxb_buffer* nxb) {
  char* obj=nxb->base;
  nxb->base=nxb->ptr;
  nxb->chunk->dirty=1;
  return obj;
}

static inline void nxb_unfinish_stream(nxb_buffer* nxb) {
  nxb->ptr=nxb->base;
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


#endif // NX_BUFFER_H_INCLUDED
