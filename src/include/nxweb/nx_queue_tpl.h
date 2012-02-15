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

