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

#ifndef NX_EVENT_H_INCLUDED
#define NX_EVENT_H_INCLUDED

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <time.h>
#include <sys/eventfd.h>

#include "nx_pool.h"

#define NXE_EVENT_POOL_INITIAL_SIZE 16
#define NXE_NUMBER_OF_TIMER_QUEUES 4
#define NXE_STALE_EVENT_TIMEOUT 30000000

enum nxe_status {
  NXE_CAN=1,
  NXE_WANT=2,
  NXE_CAN_AND_WANT=3
};

enum nxe_event_code {
  NXE_READ=1,
  NXE_READ_FULL=2,
  NXE_WRITE=4,
  NXE_WRITE_FULL=8,
  NXE_CLOSE=0x100,
  NXE_ERROR=0x200,
  NXE_RDHUP=0x400,
  NXE_HUP=0x800,
  NXE_WRITTEN_NONE=0x1000,
  NXE_STALE=0x2000
};

struct nxe_loop;
struct nxe_event;

typedef void (*nxe_handler)(struct nxe_loop* loop, struct nxe_event* evt, void* evt_data, int events);

typedef unsigned short nxe_bits_t;
typedef unsigned short nxe_event_class_t;
typedef unsigned short nxe_event_chunk_id_t;
typedef uint64_t nxe_time_t;

typedef struct nxe_event_class {
  nxe_handler on_new;
  nxe_handler on_delete;
  nxe_handler on_read;
  nxe_handler on_write;
  nxe_handler on_error;

  nxe_bits_t log_stale:1;
} nxe_event_class;

extern nxe_event_class nxe_event_classes[];

typedef struct nxe_event {
  nxp_object super; // nxe_event extends nxp_object

  nxe_event_class_t event_class:4;
  nxe_bits_t read_status:2;
  nxe_bits_t write_status:2;
  nxe_bits_t stale_status:2;
  nxe_bits_t rdhup_status:1; // do not track WANT
  nxe_bits_t err_status:1; // do not track WANT
  nxe_bits_t hup_status:1; // do not track WANT

  nxe_bits_t active:1;
  nxe_bits_t write_started:1;
  nxe_bits_t logged_stale:1;

  int fd;
  int last_errno;

  int read_size;
  int send_fd;
  size_t write_size;
  off_t send_offset;
  char* read_ptr;
  const char* write_ptr;

  nxe_time_t last_activity;

  struct nxe_event* next;
  struct nxe_event* prev;

} nxe_event;

#define NXE_EVENT_DATA(evt) ((void*)((evt)+1))

typedef struct nxe_event_async {
  nxe_event evt;
  eventfd_t buf;
} nxe_event_async;

typedef void (*nxe_timer_callback)(struct nxe_loop* loop, void* timer_data);

typedef struct nxe_timer {
  nxe_time_t abs_time;
  nxe_timer_callback callback;
  void* data;
  struct nxe_timer* next;
  struct nxe_timer* prev;
} nxe_timer;

typedef struct nxe_timer_queue {
  nxe_time_t timeout;
  nxe_timer* timer_first;
  nxe_timer* timer_last;
} nxe_timer_queue;


typedef struct nxe_loop {
  nxe_event* current;
  nxe_event* train_first;
  nxe_event* train_last;

  nxe_time_t current_time;

  nxe_bits_t broken:1;

  int epoll_fd;

  int max_epoll_events;
  int num_epoll_events;
  struct epoll_event *epoll_events;

  void* user_data;

  nxe_timer_queue timers[NXE_NUMBER_OF_TIMER_QUEUES];

  nxp_pool event_pool;
  nxp_chunk event_pool_initial_chunk;
  // chunk data to follow here!!!
} nxe_loop;

nxe_loop* nxe_create(int event_data_size, int max_epoll_events);
void nxe_destroy(nxe_loop* loop);

void nxe_run(nxe_loop* loop);
void nxe_break(nxe_loop* loop);

void nxe_start(nxe_loop* loop, nxe_event* evt);
void nxe_stop(nxe_loop* loop, nxe_event* evt);

nxe_event* nxe_new_event_fd(nxe_loop* loop, nxe_event_class_t evt_class, int fd);
void nxe_delete_event(nxe_loop* loop, nxe_event* evt);

void nxe_init_event(nxe_event* evt);

// NOTE: make sure evt structure is properly initialized (status bits, read/write ptrs, etc.)
void nxe_add_event(nxe_loop* loop, nxe_event_class_t evt_class, nxe_event* evt);
void nxe_add_event_fd(nxe_loop* loop, nxe_event_class_t evt_class, nxe_event* evt, int fd);
void nxe_remove_event(nxe_loop* loop, nxe_event* evt);

void nxe_set_timer_queue_timeout(nxe_loop* loop, int queue_idx, nxe_time_t usec_interval);
void nxe_set_timer(nxe_loop* loop, int queue_idx, nxe_timer* timer);
void nxe_unset_timer(nxe_loop* loop, int queue_idx, nxe_timer* timer);

void nxe_async_init(nxe_event_async* aevt);
void nxe_async_finalize(nxe_event_async* aevt);
void nxe_async_send(nxe_event_async* aevt);
void nxe_async_rearm(nxe_event_async* aevt);

static inline nxe_time_t nxe_get_time_usec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return (nxe_time_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
}

void nxe_relink(nxe_loop* loop, nxe_event* evt);

static inline void nxe_mark_read_up(nxe_loop* loop, nxe_event* evt, nxe_bits_t read_bits) {
  if ((evt->read_status|=read_bits)==NXE_CAN_AND_WANT) nxe_relink(loop, evt);
}

static inline void nxe_mark_write_up(nxe_loop* loop, nxe_event* evt, nxe_bits_t write_bits) {
  if ((evt->write_status|=write_bits)==NXE_CAN_AND_WANT) nxe_relink(loop, evt);
}

static inline void nxe_mark_stale_up(nxe_loop* loop, nxe_event* evt, nxe_bits_t stale_bits) {
  if ((evt->stale_status|=stale_bits)==NXE_CAN_AND_WANT) nxe_relink(loop, evt);
}

static inline void nxe_mark_read_down(nxe_loop* loop, nxe_event* evt, nxe_bits_t read_bits) {
  evt->read_status&=~read_bits;
}

static inline void nxe_mark_write_down(nxe_loop* loop, nxe_event* evt, nxe_bits_t write_bits) {
  evt->write_status&=~write_bits;
}

#endif // NX_EVENT_H_INCLUDED
