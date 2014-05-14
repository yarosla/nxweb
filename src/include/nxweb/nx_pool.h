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

typedef struct nxp_pool_iterator {
  nxp_pool* pool;
  nxp_chunk* chunk;
  int i;
} nxp_pool_iterator;

nxp_pool* nxp_create(int object_size, int initial_chunk_size);
void nxp_destroy(nxp_pool* pool);
void nxp_init(nxp_pool* pool, int object_size, nxp_chunk* initial_chunk, int chunk_allocated_size);
void nxp_finalize(nxp_pool* pool);
void* nxp_alloc(nxp_pool* pool);
void nxp_free(nxp_pool* pool, void* ptr);
void nxp_gc(nxp_pool* pool);
void* nxp_iterate_allocated_objects(nxp_pool* pool, nxp_pool_iterator* itr);

#endif // NX_POOL_H_INCLUDED
