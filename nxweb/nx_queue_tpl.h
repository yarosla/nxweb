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

#ifndef NX_QUEUE_TPL_H
#define	NX_QUEUE_TPL_H

#ifdef	__cplusplus
extern "C" {
#endif


#define NX_QUEUE_DECLARE(name, item_type, max_items) \
\
typedef struct nx_queue_##name { \
  volatile int head, tail; \
  item_type items[max_items]; \
} nx_queue_##name; \
\
static inline void nx_queue_##name##_init(nx_queue_##name* q) { \
  q->head=q->tail=0; \
} \
\
static inline int nx_queue_##name##_is_empty(const nx_queue_##name* q) { \
  return (q->head==q->tail); \
} \
\
static inline int nx_queue_##name##_is_full(const nx_queue_##name* q) { \
  return (q->head==(q->tail+1)%(max_items)); \
} \
\
static inline int nx_queue_##name##_length(const nx_queue_##name* q) { \
  int nitems=q->tail - q->head; \
  return nitems>=0? nitems : nitems+(max_items); \
} \
\
static inline int nx_queue_##name##_pop(nx_queue_##name* q, item_type* item) { \
  if (nx_queue_##name##_is_empty(q)) return -1; /* empty */ \
  __sync_synchronize(); /* full memory barrier */ \
  *item=q->items[q->head]; \
  __sync_synchronize(); /* full memory barrier */ \
  q->head=(q->head+1)%(max_items); \
  return 0; \
} \
\
static inline int nx_queue_##name##_push(nx_queue_##name* q, item_type* item) { \
  if (nx_queue_##name##_is_full(q)) return -1; /* full */ \
  __sync_synchronize(); /* full memory barrier */ \
  q->items[q->tail]=*item; \
  __sync_synchronize(); \
  q->tail=(q->tail+1)%(max_items); \
  return 0; \
}


#ifdef	__cplusplus
}
#endif

#endif	/* NX_QUEUE_TPL_H */

