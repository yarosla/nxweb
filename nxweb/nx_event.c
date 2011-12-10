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

#define _FILE_OFFSET_BITS 64

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

static void nxe_loop_gc(nxe_loop* loop) {
  nxp_gc(&loop->event_pool);
}

static int nxe_async_open_fd() {
  int fd=eventfd(0, EFD_NONBLOCK);
  if (fd==-1) {
    nxweb_log_error("eventfd() returned error %d", errno);
    return -1;
  }
//  if (_nxweb_set_non_block(fd)<0) {
//    nxweb_log_error("can't set eventfd() nonblocking %d", errno);
//    return -1;
//  }
  return fd;
}

void nxe_async_init(nxe_event_async* aevt) {
  nxe_init_event(&aevt->evt);
  aevt->evt.fd=nxe_async_open_fd();
  aevt->evt.read_ptr=(char*)&aevt->buf;
  aevt->evt.read_size=sizeof(eventfd_t);
  aevt->evt.read_status=NXE_CAN_AND_WANT;
}

void nxe_async_finalize(nxe_event_async* aevt) {
  if (aevt->evt.fd) close(aevt->evt.fd);
  aevt->evt.fd=0;
}

void nxe_async_rearm(nxe_event_async* aevt) {
  aevt->evt.read_ptr=(char*)&aevt->buf;
  aevt->evt.read_size=sizeof(eventfd_t);
}

void nxe_async_send(nxe_event_async* aevt) {
//  uint64_t incr=1;
//  write(event_fd, &incr, sizeof(incr));
  eventfd_write(aevt->evt.fd, 1);
}

// NOTE: setting the same timer twice leads to unpredictable results; make sure to unset first
void nxe_set_timer(nxe_loop* loop, nxe_timer* timer, nxe_time_t usec_interval) {
  //////////////
  return;
  //////////////
  timer->abs_time=loop->current_time+usec_interval;
  nxe_timer* t=loop->timer_first;
  while (t && t->next) {
    if (t->next->abs_time > timer->abs_time) break;
    t=t->next;
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
  //////////////
  return;
  //////////////
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
    t=t->next;
  }
}

static int nxe_process_event(nxe_loop* loop, nxe_event* evt) {
  if (evt->read_status==NXE_CAN_AND_WANT) {
    do {
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
            continue;
          }
          else {
            nxweb_log_error("read() error %d", errno);
            evt->read_status&=~NXE_CAN;
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
          continue;
        }
      }
      else if (nxe_event_classes[evt->event_class].on_read) {
        //if (evt->event_class) nxweb_log_error("read_ptr=0; invoking on_read to do read");
        nxe_event_classes[evt->event_class].on_read(loop, evt, NXE_EVENT_DATA(evt), NXE_READ);
        return 1;
      }
      else {
        evt->read_status&=~NXE_WANT;
        nxweb_log_error("no action specified for read");
      }
    } while (evt->read_status==NXE_CAN_AND_WANT);
    return 1;
  }

  if (evt->write_status==NXE_CAN_AND_WANT) {
    _nxweb_batch_write_begin(evt->fd);
    do {
      if (evt->send_fd) {
        size_t bytes_written=sendfile(evt->fd, evt->send_fd, &evt->send_offset, evt->write_size);
        if (bytes_written<=0) {
          _nxweb_batch_write_end(evt->fd);
          if (errno==EAGAIN) {
            evt->write_status&=~NXE_CAN;
            return 1;
          }
          else {
            nxweb_log_error("sendfile() error %d", errno);
            evt->write_status&=~NXE_CAN;
            nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_ERROR);
            return 1;
          }
        }
        else {
          evt->write_size-=bytes_written;
          int write_full=!evt->write_size;
          if (!write_full) evt->write_status&=~NXE_CAN;
          if (nxe_event_classes[evt->event_class].on_write)
            nxe_event_classes[evt->event_class].on_write(loop, evt, NXE_EVENT_DATA(evt), write_full? NXE_WRITE_FULL : NXE_WRITE);
          // callback might have changed read_size
          if (!evt->write_size) evt->write_status&=~NXE_WANT;
          continue;
        }
      }
      else if (evt->write_ptr) {
        size_t bytes_written=write(evt->fd, evt->write_ptr, evt->write_size);
        if (bytes_written==0) {
          _nxweb_batch_write_end(evt->fd);
          nxweb_log_error("write() returned 0, error %d", errno);
          evt->write_status&=~NXE_CAN;
          nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_WRITTEN_NONE);
          return 1;
        }
        else if (bytes_written<0) {
          if (errno==EAGAIN) {
            _nxweb_batch_write_end(evt->fd);
            evt->write_status&=~NXE_CAN;
            return 1;
          }
          else {
            _nxweb_batch_write_end(evt->fd);
            nxweb_log_error("write() error %d", errno);
            evt->write_status&=~NXE_CAN;
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
          continue;
        }
      }
      else if (nxe_event_classes[evt->event_class].on_write) {
        nxweb_log_error("write_ptr=0; invoking on_write to do write");
        nxe_event_classes[evt->event_class].on_write(loop, evt, NXE_EVENT_DATA(evt), NXE_WRITE);
        return 1;
      }
      else {
        evt->write_status&=~NXE_WANT;
        nxweb_log_error("no action specified for write");
      }
    } while (evt->write_status==NXE_CAN_AND_WANT);
    _nxweb_batch_write_end(evt->fd);
    return 1;
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

void nxe_break(nxe_loop* loop) {
  loop->broken=1;
}

void nxe_run(nxe_loop* loop) {
  int i;
  nxe_event* evt;
  int time_to_wait;

  loop->current_time=nxe_get_time_usec();
  loop->num_epoll_events=1; // initiate process_loop() from the start

  while (!loop->broken) {
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

void nxe_init_event(nxe_event* evt) {
  memset(evt, 0, sizeof(nxe_event));
}

// NOTE: make sure evt structure is properly initialized (status bits, read/write ptrs, etc.)
void nxe_add_event(nxe_loop* loop, nxe_event_class_t evt_class, nxe_event* evt) {
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
  if (nxe_event_classes[evt_class].on_new)
    nxe_event_classes[evt_class].on_new(loop, evt, NXE_EVENT_DATA(evt), 0);
}

nxe_event* nxe_new_event_fd(nxe_loop* loop, nxe_event_class_t evt_class, int fd) {
  nxe_event *evt=(nxe_event*)nxp_get(&loop->event_pool);
  evt->fd=fd;
  nxe_add_event(loop, evt_class, evt);
  return evt;
}

// NOTE: make sure evt structure is properly initialized (status bits, read/write ptrs, etc.)
void nxe_add_event_fd(nxe_loop* loop, nxe_event_class_t evt_class, nxe_event* evt, int fd) {
  evt->fd=fd;
  nxe_add_event(loop, evt_class, evt);
}

void nxe_delete_event(nxe_loop* loop, nxe_event* evt) {
  nxe_remove_event(loop, evt);
  nxp_recycle(&loop->event_pool, &evt->super);
}

void nxe_remove_event(nxe_loop* loop, nxe_event* evt) {
  if (!evt->next) return; // not in loop
  if (evt->active) nxe_stop(loop, evt);
  evt->next->prev=evt->prev;
  evt->prev->next=evt->next;
  if (evt==loop->current) loop->current=loop->current==evt->next? 0 : evt->next;
  if (evt==loop->started_from) loop->started_from=loop->started_from==evt->next? 0 : evt->next;
  evt->next=
  evt->prev=0;

  if (evt->send_fd) {
    close(evt->send_fd);
    evt->send_fd=0;
  }

  if (nxe_event_classes[evt->event_class].on_delete)
    nxe_event_classes[evt->event_class].on_delete(loop, evt, NXE_EVENT_DATA(evt), 0);
}

nxe_loop* nxe_create(int event_data_size, int max_epoll_events) {
  int s=4;
  while (s<max_epoll_events) s<<=1; // align to power of 2
  max_epoll_events=s;
  int chunk_allocated_size=(sizeof(nxe_event)+event_data_size)*NXE_EVENT_POOL_INITIAL_SIZE;
  nxe_loop* loop=calloc(1, sizeof(nxe_loop)
                           +chunk_allocated_size
                           +sizeof(struct epoll_event)*max_epoll_events);
  if (!loop) return 0;
  int event_object_size=sizeof(nxe_event)+event_data_size;
  nxp_init(&loop->event_pool, event_object_size, &loop->event_pool_initial_chunk, sizeof(nxp_chunk)+chunk_allocated_size);
  loop->epoll_events=(struct epoll_event*)((char*)(loop+1)+chunk_allocated_size);
  loop->max_epoll_events=max_epoll_events;

  loop->epoll_fd=epoll_create(1); // size ignored

  return loop;
}

void nxe_destroy(nxe_loop* loop) {
  nxp_finalize(&loop->event_pool);
  free(loop);
}
