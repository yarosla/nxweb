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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nx_pool.h"
#include "misc.h"

static void nxp_init_chunk(nxp_pool* pool) {
  int i;
  nxp_object *obj, *prev;
  nxp_chunk* chunk=pool->chunk;
  nxp_chunk_id_t chunk_id=chunk->id;
  prev=pool->free_last;
  int object_size=pool->object_size;
  for (i=chunk->nitems, obj=chunk->pool; i>0; i--, obj=(nxp_object*)((char*)obj+object_size)) {
    obj->in_use=0;
    obj->chunk_id=chunk_id;
    obj->prev=prev;
    if (prev) prev->next=obj;
    prev=obj;
  }
  pool->free_last=prev;
  prev->next=0;
  if (!pool->free_first) pool->free_first=chunk->pool;
}

void nxp_init(nxp_pool* pool, int object_size, nxp_chunk* initial_chunk, int chunk_allocated_size) {
  object_size+=sizeof(nxp_object);
  pool->chunk=
  pool->initial_chunk=initial_chunk;
  pool->chunk->id=1;
  pool->chunk->nitems=(chunk_allocated_size-offsetof(nxp_chunk, pool))/object_size;
  pool->chunk->prev=0;
  pool->free_first=
  pool->free_last=0;
  pool->object_size=object_size;
  nxp_init_chunk(pool);
}

void nxp_finalize(nxp_pool* pool) {
  nxp_chunk* c=pool->chunk;
  nxp_chunk* cp;
  while (c!=pool->initial_chunk) {
    cp=c->prev;
    nx_free(c);
    c=cp;
  }
}

nxp_pool* nxp_create(int object_size, int initial_chunk_size) {
  object_size=(object_size+7)&~0x7; // align to 8 bytes
  int alloc_size=(sizeof(nxp_object)+object_size)*initial_chunk_size;
  nxp_pool* pool=nx_alloc(sizeof(nxp_pool)+sizeof(nxp_chunk)+alloc_size);
  nxp_init(pool, object_size, (void*)(pool+1), sizeof(nxp_chunk)+alloc_size);
  return pool;
}

void nxp_destroy(nxp_pool* pool) {
  nxp_finalize(pool);
  nx_free(pool);
}

void* nxp_alloc(nxp_pool* pool) {
  nxp_object* obj=pool->free_first;
  if (!obj) {
    int nitems=pool->chunk->nitems*2;
    if (nitems>1024) nitems=1024;
    nxp_chunk* chunk=nx_alloc(offsetof(nxp_chunk, pool)+nitems*pool->object_size);
    if (!chunk) {
      nxweb_log_error("nx_pool: alloc obj[%d] chunk failed", nitems);
      return 0;
    }
    chunk->nitems=nitems;
    chunk->prev=pool->chunk;
    chunk->id=chunk->prev? chunk->prev->id+1 : 1;
    pool->chunk=chunk;
    nxp_init_chunk(pool);
    obj=pool->free_first;
  }
  if (obj->next) {
    pool->free_first=obj->next;
    obj->next->prev=0;
  }
  else {
    pool->free_first=
    pool->free_last=0;
  }

  obj->in_use=1;
  obj->prev=0;
  obj->next=0;

  return obj+1;
}

void nxp_free(nxp_pool* pool, void* ptr) {
  nxp_object* obj=(nxp_object*)((char*)ptr-sizeof(nxp_object));
  assert(obj->in_use==1);
  obj->in_use=0;
  if (obj->chunk_id==pool->chunk->id) {
    // belongs to last chunk => put at the end of free list
    if (pool->free_last) {
      pool->free_last->next=obj;
      obj->prev=pool->free_last;
      pool->free_last=obj;
      obj->next=0;
    }
    else {
      pool->free_first=pool->free_last=obj;
      obj->next=0;
      obj->prev=0;
    }
  }
  else {
    assert(obj->chunk_id < pool->chunk->id);
    // put at the beginning of free list
    if (pool->free_first) {
      pool->free_first->prev=obj;
      obj->next=pool->free_first;
      pool->free_first=obj;
      obj->prev=0;
    }
    else {
      pool->free_first=pool->free_last=obj;
      obj->next=0;
      obj->prev=0;
    }
  }
  // memset(ptr, 0xff, pool->object_size-sizeof(nxp_object)); // DEBUG ONLY - wipe freed data
}

void nxp_gc(nxp_pool* pool) {
  if (!pool->chunk->prev) return; // can't free the very first chunk

  nxp_object *obj;
  int object_size=pool->object_size;
  int i;
  for (obj=pool->chunk->pool, i=pool->chunk->nitems; i>0; i--, obj=(nxp_object*)((char*)obj+object_size)) {
    if (obj->in_use) return;
  }
  // all objects in last chunk are not in use
  // => remove them from free list and free the chunk
  for (obj=pool->chunk->pool, i=pool->chunk->nitems; i>0; i--, obj=(nxp_object*)((char*)obj+object_size)) {
    if (obj->prev) obj->prev->next=obj->next;
    else pool->free_first=obj->next;
    if (obj->next) obj->next->prev=obj->prev;
    else pool->free_last=obj->prev;
  }
  nxp_chunk* c=pool->chunk;
  pool->chunk=pool->chunk->prev;
  nx_free(c);
}

void* nxp_iterate_allocated_objects(nxp_pool* pool, nxp_pool_iterator* itr) {
  if (pool) {
    itr->pool=pool;
    itr->chunk=pool->chunk;
    itr->i=0;
  }
  nxp_object *obj;
  while (itr->chunk) {
    while (itr->i < itr->chunk->nitems) {
      obj=(nxp_object*)((char*)itr->chunk->pool + itr->pool->object_size * itr->i);
      itr->i++;
      if (obj->in_use) {
        return obj+1;
      }
    }
    itr->chunk=itr->chunk->prev;
    itr->i=0;
  }
  return 0;
}
