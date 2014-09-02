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

#define _FILE_OFFSET_BITS 64

#include "nx_buffer.h"

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>



/// The following is bases on GNU obstack implementation

/* Determine default alignment.  */
union _nxb_fooround
{
  uintmax_t i;
  long double d;
  void *p;
};
struct _nxb_fooalign
{
  char c;
  union _nxb_fooround u;
};
/* If malloc were really smart, it would round addresses to DEFAULT_ALIGNMENT.
   But in fact it might be less smart and round addresses to as much as
   DEFAULT_ROUNDING.  So we prepare for it to do that.  */
enum
  {
    NXB_DEFAULT_ALIGNMENT = offsetof(struct _nxb_fooalign, u),
    NXB_DEFAULT_ROUNDING = sizeof(union _nxb_fooround),
    NXB_DEFAULT_ALIGNMENT_MASK = NXB_DEFAULT_ALIGNMENT-1
  };

/* We need the type of a pointer subtraction. */

#define NXB_PTR_INT_TYPE ptrdiff_t

/* If B is the base of an object addressed by P, return the result of
   aligning P to the next multiple of A + 1.  B and P must be of type
   char *.  A + 1 must be a power of 2.  */

#define NXB_BPTR_ALIGN(B, P, A) ((void*)(((char*)(B)) + ((((char*)(P)) - ((char*)(B)) + (A)) & ~(A))))

#define NXB_BPTR_ALIGN_DOWN(B, P, A) ((void*)(((char*)(B)) + ((((char*)(P)) - ((char*)(B))) & ~(A))))

/* Similiar to NXB_BPTR_ALIGN (B, P, A), except optimize the common case
   where pointers can be converted to integers, aligned as integers,
   and converted back again.  If PTR_INT_TYPE is narrower than a
   pointer (e.g., the AS/400), play it safe and compute the alignment
   relative to B.  Otherwise, use the faster strategy of computing the
   alignment relative to 0.  */

#define NXB_PTR_ALIGN_A(B, P, A) \
  NXB_BPTR_ALIGN(sizeof(NXB_PTR_INT_TYPE) < sizeof(void*) ? ((char*)(B)) : (char*)0, P, A)

#define NXB_PTR_ALIGN_DOWN_A(B, P, A) \
  NXB_BPTR_ALIGN_DOWN(sizeof(NXB_PTR_INT_TYPE) < sizeof(void*) ? ((char*)(B)) : (char*)0, P, A)

#define NXB_PTR_ALIGN(B, P) \
  NXB_BPTR_ALIGN(sizeof(NXB_PTR_INT_TYPE) < sizeof(void*) ? ((char*)(B)) : (char*)0, P, NXB_DEFAULT_ALIGNMENT_MASK)

#define NXB_PTR_ALIGN_DOWN(B, P) \
  NXB_BPTR_ALIGN_DOWN(sizeof(NXB_PTR_INT_TYPE) < sizeof(void*) ? ((char*)(B)) : (char*)0, P, NXB_DEFAULT_ALIGNMENT_MASK)

/// The above is bases on GNU obstack implementation

static void nxb_init_chunk(nxb_chunk* nxc, nxb_chunk* nxc_prev, int nxc_allocated_size, int should_free) {
  nxc->end=(char*)nxc+nxc_allocated_size;
  nxc->prev=nxc_prev;
  nxc->should_free=should_free;
  nxc->dirty=0;
}

void nxb_init(nxb_buffer* nxb, int nxb_allocated_size) {
  nxb->chunk=(nxb_chunk*)NXB_PTR_ALIGN(nxb, nxb+1);
  nxb_init_chunk(nxb->chunk, 0, nxb_allocated_size-((char*)nxb->chunk - (char*)nxb), 0);
  nxb->ptr=
  nxb->base=nxb->chunk->buf;
  nxb->end=nxb->chunk->end;
}

nxb_buffer* nxb_create(int initial_chunk_size) {
  initial_chunk_size=(initial_chunk_size + NXB_DEFAULT_ALIGNMENT_MASK) & ~NXB_DEFAULT_ALIGNMENT_MASK;
  int alloc_size=sizeof(nxb_buffer) + sizeof(nxb_chunk) + initial_chunk_size;
  nxb_buffer* nxb=nx_alloc(alloc_size);
  if (!nxb) {
    fprintf(stderr, "nxb_create(%d) failed\n", alloc_size);
    return 0;
  }
  nxb_init(nxb, alloc_size);
  return nxb;
}

void nxb_empty(nxb_buffer* nxb) {
  nxb_chunk* nxc=nxb->chunk;
  nxb_chunk* nxcp=nxc->prev;
  while (nxcp) {
    if (nxc->should_free) nx_free(nxc);
    nxc=nxcp;
    nxcp=nxc->prev;
  }
  nxb->chunk=nxc;
  nxb->ptr=
  nxb->base=nxb->chunk->buf;
  nxb->end=nxb->chunk->end;
}

void nxb_destroy(nxb_buffer* nxb) {
  nxb_empty(nxb);
  if (nxb->chunk->should_free) nx_free(nxb->chunk);
  nx_free(nxb);
}

int nxb_realloc_chunk(nxb_buffer* nxb, int min_room) {
  int new_size=(nxb->chunk->end - nxb->chunk->buf)*2; // double previous chunk size
  int base_size=nxb->ptr - nxb->base;
  min_room+=base_size; // add what we already got unfinished
  min_room+=min_room>>3; // add bit more (+1/8)
  if (new_size<min_room) new_size=min_room;
  //if (new_size<1024) new_size=1024;
  new_size=(new_size + NXB_DEFAULT_ALIGNMENT_MASK) & ~NXB_DEFAULT_ALIGNMENT_MASK;
  int alloc_size=sizeof(nxb_chunk) + new_size;
  nxb_chunk* nxc=nx_alloc(alloc_size);
  if (!nxc) {
    fprintf(stderr, "nxb_realloc_chunk(%d) failed\n", alloc_size);
    return -1;
  }
  nxb_init_chunk(nxc, nxb->chunk, alloc_size, 1);
  if (base_size) {
    memmove(nxc->buf, nxb->base, base_size);
  }
  nxb->base=nxc->buf;
  nxb->ptr=nxb->base+base_size;
  nxb->end=nxc->end;
  if (!nxb->chunk->dirty && nxb->chunk->should_free) {
    nxc->prev=nxb->chunk->prev;
    nx_free(nxb->chunk);
  }
  nxb->chunk=nxc;
  return 0;
}

void* nxb_alloc_obj(nxb_buffer* nxb, int size) {
  // allocate objects down from the end of chunk
  char* obj=NXB_PTR_ALIGN_DOWN(nxb->chunk, nxb->end-size);
  if (obj<nxb->ptr) {
    // no room
    nxb_realloc_chunk(nxb, size);
    obj=NXB_PTR_ALIGN_DOWN(nxb->chunk, nxb->end-size);
  }
  nxb->end=obj;
  nxb->chunk->dirty=1;
  return obj;
}


int nxb_printf_va(nxb_buffer* nxb, const char* fmt, va_list ap) {
  va_list ap_copy;
  int room_size=nxb->end - nxb->ptr;
  va_copy(ap_copy, ap); // preserve original va_list
  int len=vsnprintf(nxb->ptr, room_size, fmt, ap_copy);
  va_end(ap_copy);
  if (len<0) return 0; // output error
  if (len>=room_size) {
    if (nxb_realloc_chunk(nxb, len+1)) return 0; // need space for null-terminator
    room_size=nxb->end - nxb->ptr;
    va_copy(ap_copy, ap); // preserve original va_list
    int len2=vsnprintf(nxb->ptr, room_size, fmt, ap_copy);
    va_end(ap_copy);
    assert(len2==len);
  }
  nxb->ptr+=len;
  return len;
}

int nxb_printf(nxb_buffer* nxb, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r=nxb_printf_va(nxb, fmt, ap);
  va_end(ap);
  return r;
}
