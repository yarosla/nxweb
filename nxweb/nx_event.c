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

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <netdb.h>

#include <sys/epoll.h>

#include "nx_event.h"
#include "misc.h"

volatile int nxe_shutdown_in_progress=0;

static void nxe_loop_gc(nxe_loop* loop) {
  if (!loop->event_pool_chunk->prev) return; // can't free the very first chunk

  nxe_event *evt;
  int sizeof_event=loop->sizeof_event;
  int i;
  for (evt=loop->event_pool_chunk->event_pool, i=loop->event_pool_chunk->nitems; i>0; i--, evt=(nxe_event*)((char*)evt+sizeof_event)) {
    if (evt->in_use) return;
  }
  // all events in last chunk are not in use
  // => remove them from free list and free the chunk
  for (evt=loop->event_pool_chunk->event_pool, i=loop->event_pool_chunk->nitems; i>0; i--, evt=(nxe_event*)((char*)evt+sizeof_event)) {
    if (evt->prev) evt->prev->next=evt->next;
    else loop->free_event_first=evt->next;
    if (evt->next) evt->next->prev=evt->prev;
    else loop->free_event_last=evt->prev;
  }
  nxe_event_pool_chunk* c=loop->event_pool_chunk;
  loop->event_pool_chunk=loop->event_pool_chunk->prev;
  nxweb_log_error("event_pool_chunk(%d, %d) freed", c->id, c->nitems);
  free(c);
}

void nxe_set_timer(nxe_loop* loop, nxe_timer* timer, nxe_time_t usec_interval) {
  timer->abs_time=loop->current_time+usec_interval;
  nxe_timer* t=loop->timer_first;
  while (t && t->next) {
    if (t->next->abs_time > timer->abs_time) break;
  }
  if (!t || t->abs_time > timer->abs_time) {
    loop->timer_first=timer;
    timer->next=t;
  }
  else {
    timer->next=t->next;
    t->next=timer;
  }
}

void nxe_unset_timer(nxe_loop* loop, nxe_timer* timer) {
  nxe_timer* t=loop->timer_first;
  if (t==timer) {
    loop->timer_first=timer->next;
    return;
  }
  while (t && t->next) {
    if (t->next==timer) {
      t->next=timer->next;
      return;
    }
  }
}

static int nxe_process_event(nxe_loop* loop, nxe_event* evt) {
  if (evt->read_status==NXE_CAN_AND_WANT) {
    if (evt->read_ptr) {
      int bytes_read=read(evt->fd, evt->read_ptr, evt->read_size);
      if (bytes_read==0) {
        evt->read_status=0; //&=~NXE_WANT;
        nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_CLOSE);
        return 1;
      }
      else if (bytes_read<0) {
        if (errno==EAGAIN) {
          evt->read_status&=~NXE_CAN;
          return 1;
        }
        else {
          nxweb_log_error("read() error %d", errno);
          nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_ERROR);
          return 1;
        }
      }
      else {
        evt->read_ptr+=bytes_read;
        evt->read_size-=bytes_read;
        int read_full=!evt->read_size;
        if (!read_full) evt->read_status&=~NXE_CAN;
        if (nxe_event_classes[evt->event_class].on_read)
          nxe_event_classes[evt->event_class].on_read(loop, evt, NXE_EVENT_DATA(evt), read_full? NXE_READ_FULL : NXE_READ);
        // callback might have changed read_size
        if (!evt->read_size) evt->read_status&=~NXE_WANT;
        return 1;
      }
    }
    else if (nxe_event_classes[evt->event_class].on_read) {
      nxe_event_classes[evt->event_class].on_read(loop, evt, NXE_EVENT_DATA(evt), NXE_READ);
    }
    else {
      evt->read_status&=~NXE_WANT;
      nxweb_log_error("no action specified for read");
    }
  }

  if (evt->write_status==NXE_CAN_AND_WANT) {
    if (evt->write_ptr) {
      int bytes_written=write(evt->fd, evt->write_ptr, evt->write_size);
      if (bytes_written==0) {
        nxweb_log_error("write() returned 0, error %d", errno);
        nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_WRITTEN_NONE);
        return 1;
      }
      else if (bytes_written<0) {
        if (errno==EAGAIN) {
          evt->write_status&=~NXE_CAN;
          return 1;
        }
        else {
          nxweb_log_error("write() error %d", errno);
          nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_ERROR);
          return 1;
        }
      }
      else {
        evt->write_ptr+=bytes_written;
        evt->write_size-=bytes_written;
        int write_full=!evt->write_size;
        if (!write_full) evt->write_status&=~NXE_CAN;
        if (nxe_event_classes[evt->event_class].on_write)
          nxe_event_classes[evt->event_class].on_write(loop, evt, NXE_EVENT_DATA(evt), write_full? NXE_WRITE_FULL : NXE_WRITE);
        // callback might have changed read_size
        if (!evt->write_size) evt->write_status&=~NXE_WANT;
        return 1;
      }
    }
    else if (nxe_event_classes[evt->event_class].on_write) {
      nxe_event_classes[evt->event_class].on_write(loop, evt, NXE_EVENT_DATA(evt), NXE_WRITE);
    }
    else {
      evt->write_status&=~NXE_WANT;
      nxweb_log_error("no action specified for write");
    }
  }

  return 0;
}

static void nxe_process_loop(nxe_loop* loop) {
  loop->started_from=loop->current;
  do {
    while (loop->current && loop->current->active && nxe_process_event(loop, loop->current)) ;
    if (!loop->current) return;
    loop->current=loop->current->next;
  } while (loop->current && loop->current!=loop->started_from);
  loop->current=loop->current->prev; // shift current, so next run we start from different one
}

static void nxe_process_timers(nxe_loop* loop) {
  nxe_timer* t;
  while (loop->timer_first && loop->timer_first->abs_time-500000 <= loop->current_time) { // 0.5 sec precision
    t=loop->timer_first;
    loop->timer_first=t->next;
    t->callback(loop, t->data);
    loop->num_epoll_events++;
  }
}

void nxe_run(nxe_loop* loop) {
  int i;
  nxe_event* evt;
  int time_to_wait;

  loop->current_time=nxe_get_time_usec();
  loop->num_epoll_events=1; // initiate process_loop() from the start

  while (!nxe_shutdown_in_progress) {
    nxe_process_timers(loop);
    if (loop->num_epoll_events) {
      nxe_process_loop(loop);
    }
    else {
      nxe_loop_gc(loop);
    }
    if (!loop->current && !loop->timer_first) break;

    // now do epoll_wait
    time_to_wait=loop->timer_first? (int)((loop->timer_first->abs_time - loop->current_time)/1000) : 1000;
    if (time_to_wait>1000) time_to_wait=1000; // for gc
    loop->num_epoll_events=epoll_wait(loop->epoll_fd, loop->epoll_events, loop->max_epoll_events, time_to_wait);
    loop->current_time=nxe_get_time_usec();
    if (loop->num_epoll_events<0) {
      nxweb_log_error("epoll_wait error: %d", errno);
      return;
    }

    //nxweb_log_error("epoll_wait: %d events", loop->num_epoll_events);
    // update statuses
    struct epoll_event* ev;
    for (i=loop->num_epoll_events, ev=loop->epoll_events; i>0; i--, ev++) {
      evt=ev->data.ptr;
      if (ev->events&EPOLLIN) evt->read_status|=NXE_CAN;
      if (ev->events&EPOLLOUT) evt->write_status|=NXE_CAN;
      if (ev->events&EPOLLRDHUP) evt->rdhup_status|=NXE_CAN;
      if (ev->events&EPOLLHUP) evt->hup_status|=NXE_CAN;
      if (ev->events&EPOLLERR) evt->err_status|=NXE_CAN;
    }
  }
}

void nxe_start(nxe_loop* loop, nxe_event* evt) {
  // add event to epoll
  struct epoll_event ev={EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLET, {.ptr=evt}};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, evt->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl ADD error: %d", errno);
    return;
  }
  evt->active=1;
}

void nxe_stop(nxe_loop* loop, nxe_event* evt) {
  // remove event from epoll
  struct epoll_event ev={0};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, evt->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl DEL error: %d", errno);
    return;
  }
  evt->active=0;
}

static void nxe_init_event_pool_chunk(nxe_loop* loop) {
  int i;
  nxe_event *evt, *prev;
  nxe_event_pool_chunk* chunk=loop->event_pool_chunk;
  nxe_event_chunk_id_t chunk_id=chunk->id;
  prev=loop->free_event_last;
  int sizeof_event=loop->sizeof_event;
  for (i=chunk->nitems, evt=chunk->event_pool; i>0; i--, evt=(nxe_event*)((char*)evt+sizeof_event)) {
    evt->event_pool_chunk_id=chunk_id;
    evt->prev=prev;
    if (prev) prev->next=evt;
    prev=evt;
  }
  loop->free_event_last=prev;
  if (!loop->free_event_first) loop->free_event_first=chunk->event_pool;
}

static nxe_event* nxe_new_event(nxe_loop* loop, nxe_event_class_t evt_class) {
  nxe_event *evt=loop->free_event_first;
  if (!evt) {
    int nitems=loop->event_pool_chunk->nitems*2;
    if (nitems>512) nitems=512;
    nxe_event_pool_chunk* chunk=calloc(1, offsetof(nxe_event_pool_chunk, event_pool)
                                      +nitems*loop->sizeof_event);
    //nxweb_log_error("alloc %d evt chunk", nitems);
    chunk->nitems=nitems;
    chunk->prev=loop->event_pool_chunk;
    chunk->id=chunk->prev? chunk->prev->id+1 : 1;
    loop->event_pool_chunk=chunk;
    nxe_init_event_pool_chunk(loop);
    evt=loop->free_event_first;
  }
  if (evt->next) {
    loop->free_event_first=evt->next;
    evt->next->prev=0;
  }
  else {
    loop->free_event_first=loop->free_event_last=0;
  }

  evt->in_use=1;
  if (loop->current) {
    evt->next=loop->current->next;
    loop->current->next=evt;
    evt->prev=loop->current;
    evt->next->prev=evt;
  }
  else {
    loop->current=evt;
    evt->next=evt;
    evt->prev=evt;
  }
  evt->event_class=evt_class;
  return evt;
}

nxe_event* nxe_new_event_fd(nxe_loop* loop, nxe_event_class_t evt_class, int fd) {
  nxe_event *evt=nxe_new_event(loop, evt_class);
  evt->fd=fd;
  if (nxe_event_classes[evt_class].on_new)
    nxe_event_classes[evt_class].on_new(loop, evt, NXE_EVENT_DATA(evt), 0);
  return evt;
}

void nxe_delete_event(nxe_loop* loop, nxe_event* evt) {
  if (evt->active) nxe_stop(loop, evt);
  evt->next->prev=evt->prev;
  evt->prev->next=evt->next;
  if (evt==loop->current) loop->current=loop->current==evt->next? 0 : evt->next;
  if (evt==loop->started_from) loop->started_from=loop->started_from==evt->next? 0 : evt->next;

  if (nxe_event_classes[evt->event_class].on_delete)
    nxe_event_classes[evt->event_class].on_delete(loop, evt, NXE_EVENT_DATA(evt), 0);

  nxe_event_chunk_id_t chunk_id=evt->event_pool_chunk_id;
  memset(evt, 0, sizeof(nxe_event));
  evt->event_pool_chunk_id=chunk_id;

  if (loop->event_pool_chunk->id==chunk_id) {
    // belongs to last chunk =>
    // put at the end of list
    if (loop->free_event_last) {
      loop->free_event_last->next=evt;
      evt->prev=loop->free_event_last;
      loop->free_event_last=evt;
    }
    else {
      loop->free_event_first=loop->free_event_last=evt;
    }
  }
  else {
    // put at the beginning of list
    if (loop->free_event_first) {
      loop->free_event_first->prev=evt;
      evt->next=loop->free_event_first;
      loop->free_event_first=evt;
    }
    else {
      loop->free_event_first=loop->free_event_last=evt;
    }
  }
}

nxe_loop* nxe_create(int event_data_size, int max_epoll_events) {
  int s=4;
  while (s<max_epoll_events) s<<=1; // align to power of 2
  max_epoll_events=s;
  nxe_loop* loop=calloc(1, sizeof(nxe_loop)
                           +sizeof(struct epoll_event)*max_epoll_events
                           +offsetof(nxe_event_pool_chunk, event_pool)
                           +(sizeof(nxe_event)+event_data_size)*NXE_EVENT_POOL_CHUNK_SIZE);
  if (!loop) return 0;
  loop->sizeof_event=sizeof(nxe_event)+event_data_size;
  loop->epoll_events=(struct epoll_event*)(loop+1);
  loop->max_epoll_events=max_epoll_events;
  loop->event_pool_chunk=
  loop->initial_chunk=(nxe_event_pool_chunk*)(loop->epoll_events+max_epoll_events);
  loop->event_pool_chunk->nitems=NXE_EVENT_POOL_CHUNK_SIZE;
  loop->event_pool_chunk->id=1;
  loop->event_pool_chunk->prev=0;
  nxe_init_event_pool_chunk(loop);

  loop->epoll_fd=epoll_create(1); // size ignored

  return loop;
}

void nxe_destroy(nxe_loop* loop) {
  nxe_event_pool_chunk* c=loop->event_pool_chunk;
  nxe_event_pool_chunk* cp;
  while (c!=loop->initial_chunk) {
    cp=c->prev;
    free(c);
    c=cp;
  }
  free(loop);
}
