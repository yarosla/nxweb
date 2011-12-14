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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nx_queue.h"


// Can store up to queue_max_size-1 items

// This queue it safe for non-blocking use
// as long as there is only one producer thread (does push)
// and only one consumer thread (does pop)

nx_queue* nx_queue_new(int item_size, int queue_max_size) {
  assert(item_size>0);
  assert(queue_max_size>2);
  nx_queue* q=(nx_queue*)calloc(1, sizeof(nx_queue)+item_size*queue_max_size);
  q->item_size=item_size;
  q->size=queue_max_size;
  return q;
}

void nx_queue_init(nx_queue* q, int item_size, int queue_max_size) {
  assert(item_size>0);
  assert(queue_max_size>2);
  q->item_size=item_size;
  q->size=queue_max_size;
  q->head=q->tail=0;
}

int nx_queue_pop(nx_queue* q, void* item) {
  if (nx_queue_is_empty(q)) return -1; // empty
  memcpy(item, q->items+q->head*q->item_size, q->item_size);
  __sync_synchronize(); // full memory barrier
  q->head=(q->head+1)%q->size;
  return 0;
}

int nx_queue_push(nx_queue* q, const void* item) {
  if (nx_queue_is_full(q)) return -1; // full
  void* pitem=q->items+q->tail*q->item_size;
  if (!pitem) return -1; // full
  memcpy(pitem, item, q->item_size);
  __sync_synchronize(); // full memory barrier
  q->tail=(q->tail+1)%q->size;
  return 0;
}
