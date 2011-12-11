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

#define IS_IN_LOOP(loop, evt) ((evt)->prev || (loop)->train_first==(evt))

static inline void nxe_link(nxe_loop* loop, nxe_event* evt) {
  // link to end
  evt->next=0;
  evt->prev=loop->train_last;
  if (loop->train_last) loop->train_last->next=evt;
  else loop->train_first=evt;
  loop->train_last=evt;

  if (!loop->current) loop->current=evt;
}

static inline void nxe_unlink(nxe_loop* loop, nxe_event* evt) {
  // unlink
  if (evt->prev) evt->prev->next=evt->next;
  else loop->train_first=evt->next;
  if (evt->next) evt->next->prev=evt->prev;
  else loop->train_last=evt->prev;

  /// To fully unlink also do the following:
  // if (loop->current==evt) loop->current=evt->next;
  // evt->next=0;
  // evt->prev=0;
}

void nxe_relink(nxe_loop* loop, nxe_event* evt) {
  evt->last_activity=loop->current_time;
  evt->logged_stale=0;
  if (loop->current && (evt==loop->current || evt==loop->train_last)) return; // no need to relink
  assert(IS_IN_LOOP(loop, evt));

  nxe_unlink(loop, evt);
  nxe_link(loop, evt);
}

static inline void nxe_loop_gc(nxe_loop* loop) {
  nxe_event* evt=loop->train_first;
  while (evt) {
    if (loop->current_time - evt->last_activity < NXE_STALE_EVENT_TIMEOUT) break;
    nxe_mark_stale_up(loop, evt, NXE_CAN);
    if (!evt->logged_stale && nxe_event_classes[evt->event_class].log_stale) {
      nxweb_log_error("stale event found: class=%d", evt->event_class);
      evt->logged_stale=1;
    }
    evt=evt->next;
  }
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
  aevt->evt.read_status=NXE_WANT;
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

void nxe_set_timer_queue_timeout(nxe_loop* loop, int queue_idx, nxe_time_t usec_interval) {
  assert(queue_idx>=0 && queue_idx<NXE_NUMBER_OF_TIMER_QUEUES);
  loop->timers[queue_idx].timeout=usec_interval;
}

// NOTE: setting the same timer twice leads to unpredictable results; make sure to unset first
void nxe_set_timer(nxe_loop* loop, int queue_idx, nxe_timer* timer) {
  assert(queue_idx>=0 && queue_idx<NXE_NUMBER_OF_TIMER_QUEUES);
  assert(timer->abs_time==0 && timer->next==0 && timer->prev==0);
  nxe_timer_queue* tq=&loop->timers[queue_idx];
  timer->abs_time=loop->current_time+tq->timeout;
  timer->next=0;
  timer->prev=tq->timer_last;
  if (tq->timer_last) tq->timer_last->next=timer;
  else tq->timer_first=timer;
  tq->timer_last=timer;
}

void nxe_unset_timer(nxe_loop* loop, int queue_idx, nxe_timer* timer) {
  if (!timer->abs_time) return;
  assert(queue_idx>=0 && queue_idx<NXE_NUMBER_OF_TIMER_QUEUES);
  nxe_timer_queue* tq=&loop->timers[queue_idx];
  if (timer->prev) timer->prev->next=timer->next;
  else tq->timer_first=timer->next;
  if (timer->next) timer->next->prev=timer->prev;
  else tq->timer_last=timer->prev;
  timer->next=0;
  timer->prev=0;
  timer->abs_time=0;
}

static void nxe_process_timers(nxe_loop* loop) {
  nxe_timer* t;
  int i;
  nxe_timer_queue* tq;
  for (i=NXE_NUMBER_OF_TIMER_QUEUES, tq=loop->timers; i--; tq++) {
    while (tq->timer_first && tq->timer_first->abs_time-500000 <= loop->current_time) { // 0.5 sec precision
      t=tq->timer_first;
      tq->timer_first=t->next;
      if (tq->timer_first) tq->timer_first->prev=0;
      else tq->timer_last=0;
      t->next=0;
      t->prev=0;
      t->abs_time=0;
      t->callback(loop, t->data);
    }
  }
}

static nxe_timer_queue* nxe_closest_tq(nxe_loop* loop) {
  int i;
  nxe_timer_queue* tq;
  nxe_timer_queue* closest_tq=0;
  for (i=NXE_NUMBER_OF_TIMER_QUEUES, tq=loop->timers; i--; tq++) {
    if (tq->timer_first && (!closest_tq || tq->timer_first->abs_time < closest_tq->timer_first->abs_time))
      closest_tq=tq;
  }
  return closest_tq;
}

static int nxe_process_event(nxe_loop* loop, nxe_event* evt) {
  if (evt->read_status==NXE_CAN_AND_WANT) {
    do {
      if (evt->read_ptr) {
        int bytes_read=read(evt->fd, evt->read_ptr, evt->read_size);
        if (bytes_read==0) {
          evt->read_status&=~NXE_CAN;
          evt->last_errno=errno;
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
            evt->last_errno=errno;
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
          //if (!evt->read_size) evt->read_status&=~NXE_WANT; // callback must do this
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
            evt->last_errno=errno;
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
          //if (!evt->write_size) evt->write_status&=~NXE_WANT; // must be done by callback
          continue;
        }
      }
      else if (evt->write_ptr) {
        size_t bytes_written=write(evt->fd, evt->write_ptr, evt->write_size);
        if (bytes_written==0) {
          _nxweb_batch_write_end(evt->fd);
          nxweb_log_error("write() returned 0, error %d", errno);
          evt->write_status&=~NXE_CAN;
          evt->last_errno=errno;
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
            evt->last_errno=errno;
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
          //if (!evt->write_size) evt->write_status&=~NXE_WANT; // must be done by callback
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

  if (evt->err_status & NXE_CAN) {
    evt->err_status&=~NXE_CAN;
    evt->last_errno=errno;
    nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_ERROR);
    return 1;
  }

  if (evt->hup_status & NXE_CAN) {
    evt->hup_status&=~NXE_CAN;
    evt->last_errno=errno;
    nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_HUP);
    return 1;
  }

  if (evt->rdhup_status & NXE_CAN) {
    evt->rdhup_status&=~NXE_CAN;
    evt->last_errno=errno;
    nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_RDHUP);
    return 1;
  }

  if (evt->stale_status==NXE_CAN_AND_WANT) {
    evt->stale_status&=~NXE_CAN;
    evt->last_errno=0;
    nxe_event_classes[evt->event_class].on_error(loop, evt, NXE_EVENT_DATA(evt), NXE_STALE);
    return 1;
  }

  return 0;
}

static void nxe_process_loop(nxe_loop* loop) {
  while (loop->current) {
    while (loop->current && loop->current->active && nxe_process_event(loop, loop->current)) ;
    if (!loop->current) return;
    loop->current=loop->current->next;
  }
}

void nxe_break(nxe_loop* loop) {
  loop->broken=1;
}

void nxe_run(nxe_loop* loop) {
  int i;
  nxe_event* evt;
  int time_to_wait;
  nxe_timer_queue* closest_tq=0;

  loop->current_time=nxe_get_time_usec();

  while (!loop->broken) {
    nxe_process_timers(loop);
    if (loop->current) {
      nxe_process_loop(loop);
    }
    else {
      nxe_loop_gc(loop);
      if (loop->current) {
        nxe_process_loop(loop);
      }
    }
    closest_tq=nxe_closest_tq(loop);
    if (!loop->train_first && !closest_tq) break;

    // now do epoll_wait
    time_to_wait=closest_tq? (int)((closest_tq->timer_first->abs_time - loop->current_time)/1000) : 1000;
    if (time_to_wait>1000) time_to_wait=1000; // for gc
    if (time_to_wait<0) time_to_wait=0;
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
      if (ev->events&EPOLLIN) nxe_mark_read_up(loop, evt, NXE_CAN);
      if (ev->events&EPOLLOUT) nxe_mark_write_up(loop, evt, NXE_CAN);
      if (ev->events&EPOLLRDHUP) { evt->rdhup_status|=NXE_CAN; nxe_relink(loop, evt); }
      if (ev->events&EPOLLHUP) { evt->hup_status|=NXE_CAN; nxe_relink(loop, evt); }
      if (ev->events&EPOLLERR) { evt->err_status|=NXE_CAN; nxe_relink(loop, evt); }
    }
  }
}

void nxe_start(nxe_loop* loop, nxe_event* evt) {
  assert(!evt->active);
  // add event to epoll
  struct epoll_event ev={EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLET, {.ptr=evt}};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, evt->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl ADD error: %d", errno);
    return;
  }
  evt->active=1;
}

void nxe_stop(nxe_loop* loop, nxe_event* evt) {
  assert(evt->active);
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
  nxe_link(loop, evt);
  evt->event_class=evt_class;
  evt->last_activity=loop->current_time;
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
  if (!IS_IN_LOOP(loop, evt)) return; // not in loop
  if (evt->active) nxe_stop(loop, evt);
  // unlink
  nxe_unlink(loop, evt);
  if (loop->current==evt) loop->current=evt->next;
  evt->next=0;
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

  loop->current_time=nxe_get_time_usec();

  loop->epoll_fd=epoll_create(1); // size ignored

  return loop;
}

void nxe_destroy(nxe_loop* loop) {
  nxp_finalize(&loop->event_pool);
  free(loop);
}
