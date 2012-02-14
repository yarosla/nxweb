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

#ifndef NX_POOL_H_INCLUDED
#define NX_POOL_H_INCLUDED

#include <stdint.h>

#include "nx_alloc.h"

typedef int32_t nxp_bool_t;
typedef uint32_t nxp_chunk_id_t;

// poolable object header
typedef struct nxp_object {
  nxp_bool_t in_use;
  nxp_chunk_id_t chunk_id;
  struct nxp_object* next;
  struct nxp_object* prev;
} nxp_object;

typedef struct nxp_chunk {
  int nitems;
  nxp_chunk_id_t id;
  struct nxp_chunk* prev;
  nxp_object pool[];
} nxp_chunk;

typedef struct nxp_pool {
  nxp_chunk* chunk;
  nxp_chunk* initial_chunk;
  nxp_object* free_first;
  nxp_object* free_last;
  int object_size;
} nxp_pool;

nxp_pool* nxp_create(int object_size, int initial_chunk_size);
void nxp_destroy(nxp_pool* pool);
void nxp_init(nxp_pool* pool, int object_size, nxp_chunk* initial_chunk, int chunk_allocated_size);
void nxp_finalize(nxp_pool* pool);
void* nxp_alloc(nxp_pool* pool);
void nxp_free(nxp_pool* pool, void* ptr);
void nxp_gc(nxp_pool* pool);

#endif // NX_POOL_H_INCLUDED
