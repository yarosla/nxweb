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

#define _FILE_OFFSET_BITS 64

#include "nx_event.h"
#include "misc.h"

#include <stdlib.h>
#include <assert.h>
#include <sys/eventfd.h>
#include <errno.h>
#include <unistd.h>


// #define IS_IN_LOOP(loop, evt) ((evt)->prev || (loop)->first==(evt))
// #define IS_IN_LOOP(loop, evt) ((evt)->loop==(loop))
#define IS_IN_LOOP(evt) ((evt)->loop)

void nxe_link(nxe_loop* loop, nxe_event* evt) {
  //if (IS_IN_LOOP(loop, evt)) return; // already linked
  assert(loop);
  assert(evt->receiver.itf);
  assert(!IS_IN_LOOP(evt)); // not linked
  // link to end
  evt->next=0;
  evt->prev=loop->last;
  if (loop->last) loop->last->next=evt;
  else loop->first=evt;
  loop->last=evt;
  evt->loop=loop;
}

static void nxe_unlink(nxe_event* evt) {
  nxe_loop* loop=evt->loop;
  assert(loop);
  if (evt->prev) evt->prev->next=evt->next;
  else loop->first=evt->next;
  if (evt->next) evt->next->prev=evt->prev;
  else loop->last=evt->prev;
  evt->next=0;
  evt->prev=0;
  evt->loop=0;
}

static inline void nxe_loop_gc(nxe_loop* loop) {
/*
  nxe_event* evt=loop->first;
  if (loop->stale_event_timeout) {
    while (evt) {
      if (loop->current_time - evt->last_activity < loop->stale_event_timeout) break;
      if (!evt->logged_stale && evt->cls->log_stale) {
        nxweb_log_error("stale event found: class=%p", evt->cls);
        evt->logged_stale=1;
      }
      if (evt->cls->on_stale) evt->cls->on_stale(evt);
      evt=evt->next;
    }
  }
*/
  nxe_publish(&loop->gc_pub, (nxe_data)0);
  nxp_gc(&loop->free_event_pool);
}

int nxe_eventfd_open(int* fd) {
  *fd=eventfd(0, EFD_NONBLOCK);
  if (*fd==-1) {
    nxweb_log_error("eventfd() returned error %d", errno);
    return -1;
  }
  return *fd;
}

void nxe_eventfd_close(int* fd) {
  close(*fd);
  *fd=0;
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
  timer->super.loop=loop;
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
    while (tq->timer_first && tq->timer_first->abs_time-250000 <= loop->current_time) { // 0.25 sec precision
      t=tq->timer_first;
      tq->timer_first=t->next;
      if (tq->timer_first) tq->timer_first->prev=0;
      else tq->timer_last=0;
      t->next=0;
      t->prev=0;
      t->abs_time=0;
      TIMER_CLASS(t)->on_timeout(t, t->data);
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

static void nxe_process_loop(nxe_loop* loop) {
  nxe_event* evt;
  int count=100000;
  while ((evt=loop->first)) {
    loop->first=evt->next;
    nxe_unlink(evt);

    evt->cls->deliver(evt);

    if (loop->batch_write_fd) {
      _nxweb_batch_write_end(loop->batch_write_fd);
      loop->batch_write_fd=0;
    }

    // stream event might have been relinked to the loop again here; but stream events don't have free method anyway
    if (/*!IS_IN_LOOP(loop, evt) &&*/ evt->cls->free) evt->cls->free(loop, evt);

    nxweb_activate_log_debug(--count<=0, "too many events in loop");
  }
}

void nxe_break(nxe_loop* loop) {
  loop->broken=1;
}

void nxe_run(nxe_loop* loop) {
  int i;
  int time_to_wait;
  nxe_timer_queue* closest_tq=0;

  loop->current_time=nxe_get_time_usec();

  while (!loop->broken) {
    nxe_process_timers(loop);
    if (loop->first) {
      nxe_process_loop(loop);
    }
    else {
      nxe_loop_gc(loop);
      if (loop->first) {
        nxe_process_loop(loop);
      }
    }
    closest_tq=nxe_closest_tq(loop);
    if (!loop->first && !closest_tq && loop->ref_count<=0) break;

    // now do epoll_wait
    time_to_wait=closest_tq? (int)((closest_tq->timer_first->abs_time - loop->current_time)/1000) : 1000;
    if (time_to_wait>1000) time_to_wait=1000; // for gc
    if (time_to_wait<0) time_to_wait=0;
    loop->num_epoll_events=epoll_wait(loop->epoll_fd, loop->epoll_events, loop->max_epoll_events, time_to_wait);
    loop->current_time=nxe_get_time_usec();
    if (loop->num_epoll_events<0) {
      if (errno!=EINTR) nxweb_log_error("epoll_wait error: %d", errno);
      continue;
    }

    //nxweb_log_error("epoll_wait: %d events", loop->num_epoll_events);
    // update statuses
    nxe_event_source es;
    struct epoll_event* ev;
    for (i=loop->num_epoll_events, ev=loop->epoll_events; i>0; i--, ev++) {
      es.fs=(nxe_fd_source*)ev->data.ptr; // ptr is not necessarily fd_source but we don't care as we only need cls member here
      es.fs->cls->emit(loop, es, ev->events);
    }
  }
}

static void fd_source_emit(nxe_loop* loop, nxe_event_source source, uint32_t events) {
  if (events&EPOLLIN) nxe_istream_set_ready(loop, &source.fs->data_is);
  if (events&EPOLLOUT) nxe_ostream_set_ready(loop, &source.fs->data_os);
  if (events&EPOLLERR) nxe_publish(&source.fs->data_error, (nxe_data)NXE_ERROR);
  else if (events&EPOLLRDHUP) nxe_publish(&source.fs->data_error, (nxe_data)NXE_RDHUP);
  else if (events&EPOLLHUP) nxe_publish(&source.fs->data_error, (nxe_data)NXE_HUP);
}

const nxe_event_source_class _NXE_ES_FD={.emit=fd_source_emit};
const nxe_event_source_class* const NXE_ES_FD=&_NXE_ES_FD;

static void eventfd_source_emit(nxe_loop* loop, nxe_event_source source, uint32_t events) {
  if (events&EPOLLIN) {
    // consume written data to rearm the source
    eventfd_t val;
    eventfd_read(source.efs->fd[0], &val);
    nxe_publish(&source.efs->data_notify, (nxe_data)0);
  }
  if (events&EPOLLOUT) /* ignore */;
  if (events&EPOLLERR) nxe_publish(&source.efs->data_notify, (nxe_data)NXE_ERROR);
  else if (events&EPOLLRDHUP) nxe_publish(&source.efs->data_notify, (nxe_data)NXE_RDHUP);
  else if (events&EPOLLHUP) nxe_publish(&source.efs->data_notify, (nxe_data)NXE_HUP);
}

const nxe_event_source_class _NXE_ES_EFD={.emit=eventfd_source_emit};
const nxe_event_source_class* const NXE_ES_EFD=&_NXE_ES_EFD;

static void listenfd_source_emit(nxe_loop* loop, nxe_event_source source, uint32_t events) {
  if (events&EPOLLIN) nxe_publish(&source.lfs->data_notify, (nxe_data)0);
  if (events&EPOLLOUT) /* ignore */;
  if (events&EPOLLERR) nxe_publish(&source.lfs->data_notify, (nxe_data)NXE_ERROR);
  else if (events&EPOLLRDHUP) nxe_publish(&source.lfs->data_notify, (nxe_data)NXE_RDHUP);
  else if (events&EPOLLHUP) nxe_publish(&source.lfs->data_notify, (nxe_data)NXE_HUP);
}

const nxe_event_source_class _NXE_ES_LFD={.emit=listenfd_source_emit};
const nxe_event_source_class* const NXE_ES_LFD=&_NXE_ES_LFD;

void nxe_register_fd_source(nxe_loop* loop, nxe_fd_source* fs) {
  assert(!fs->data_is.super.loop); // not registered yet
  // add event to epoll
  struct epoll_event ev={EPOLLIN|EPOLLOUT|EPOLLRDHUP|EPOLLHUP|EPOLLET, {.ptr=fs}};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fs->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl ADD error: %d", errno);
    return;
  }
  fs->data_is.super.loop=loop;
  loop->ref_count++;
}

void nxe_unregister_fd_source(nxe_fd_source* fs) { // removes nxe_event's
  nxe_loop* loop=fs->data_is.super.loop;
  assert(loop);
  // remove event from epoll
  struct epoll_event ev={0};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fs->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl DEL error: %d", errno);
    return;
  }
  if (fs->data_is.pair) nxe_disconnect_streams(&fs->data_is, fs->data_is.pair);
  if (fs->data_os.pair) nxe_disconnect_streams(fs->data_os.pair, &fs->data_os);
  while (fs->data_error.sub) nxe_unsubscribe(&fs->data_error, fs->data_error.sub);
  fs->data_is.super.loop=0;
  loop->ref_count--;
}

void nxe_register_eventfd_source(nxe_loop* loop, nxe_eventfd_source* efs) {
  assert(!efs->data_notify.super.loop); // not registered yet
  // add event to epoll
  struct epoll_event ev={EPOLLIN|/*EPOLLOUT|*/EPOLLRDHUP|EPOLLHUP|EPOLLET, {.ptr=efs}};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, efs->fd[0], &ev)==-1) {
    nxweb_log_error("epoll_ctl ADD error: %d", errno);
    close(efs->fd[0]);
    efs->fd[0]=0;
    return;
  }
  efs->data_notify.super.loop=loop;
  loop->ref_count++;
}

void nxe_unregister_eventfd_source(nxe_eventfd_source* efs) {
  nxe_loop* loop=efs->data_notify.super.loop;
  assert(loop);
  if (efs->fd[0]) {
    // remove event from epoll
    struct epoll_event ev={0};
    if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, efs->fd[0], &ev)==-1) {
      nxweb_log_error("epoll_ctl DEL error: %d", errno);
    }
  }
  while (efs->data_notify.sub) nxe_unsubscribe(&efs->data_notify, efs->data_notify.sub);
  efs->data_notify.super.loop=0;
  loop->ref_count--;
}

void nxe_trigger_eventfd(nxe_eventfd_source* efs) {
  eventfd_write(efs->fd[0], 1);
}

void nxe_register_listenfd_source(nxe_loop* loop, nxe_listenfd_source* lfs) {
  assert(!lfs->data_notify.super.loop); // not registered yet
  // add event to epoll
  struct epoll_event ev={EPOLLIN|/*EPOLLOUT|*/EPOLLRDHUP|EPOLLHUP|EPOLLET, {.ptr=lfs}};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, lfs->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl ADD error: %d", errno);
    return;
  }
  lfs->data_notify.super.loop=loop;
  loop->ref_count++;
}

void nxe_unregister_listenfd_source(nxe_listenfd_source* lfs) {
  nxe_loop* loop=lfs->data_notify.super.loop;
  assert(loop);
  // remove event from epoll
  struct epoll_event ev={0};
  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, lfs->fd, &ev)==-1) {
    nxweb_log_error("epoll_ctl DEL error: %d", errno);
  }
  while (lfs->data_notify.sub) nxe_unsubscribe(&lfs->data_notify, lfs->data_notify.sub);
  lfs->data_notify.super.loop=0;
  loop->ref_count--;
}

void nxe_unref(nxe_loop* loop) {
  loop->ref_count--;
}

/*
static inline void nxe_ostream_batch_write_begin(nxe_ostream* os) {
  if (!os->batch_write_started) {
    nxe_fd_source* fs=os->part_of_fd_source? (nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os)) : 0;
    if (fs) {
      _nxweb_batch_write_begin(fs->fd);
      os->batch_write_started=1;
    }
  }
}

static inline void nxe_ostream_batch_write_end(nxe_ostream* os) {
  if (os->batch_write_started) {
    nxe_fd_source* fs=(nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os));
    _nxweb_batch_write_end(fs->fd);
    os->batch_write_started=0;
  }
}
*/

static void stream_event_deliver(nxe_event* evt) {
  nxe_ostream* os=evt->receiver.os;
  nxe_istream* is=os->pair;
  if (!is || !os) return; // not connected
  nxweb_log_debug("stream_event_deliver");
  int count=50;
  if (ISTREAM_CLASS(is)->do_write) {
    while (is->ready && os->ready && is==os->pair) {
      if (!count--) { // protect against dead loops
        nxweb_log_info("possible dead loop");
        nxe_link(os->super.loop, evt); // relink to the end of queue, let other events being processed
        break;
      }
      ISTREAM_CLASS(is)->do_write(is, os);
    }
  }
  else if (OSTREAM_CLASS(os)->do_read) {
    while (is->ready && os->ready && is==os->pair) {
      if (!count--) { // protect against dead loops
        nxweb_log_info("possible dead loop");
        nxe_link(os->super.loop, evt); // relink to the end of queue, let other events being processed
        break;
      }
      OSTREAM_CLASS(os)->do_read(os, is);
    }
  }
  else assert(0);
/*
  if (os->batch_write_started) {
    nxe_fd_source* fs=(nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os));
    _nxweb_batch_write_end(fs->fd);
    os->batch_write_started=0;
  }
*/
}

const nxe_event_class _NXE_EV_STREAM={.deliver=stream_event_deliver};
const nxe_event_class* const NXE_EV_STREAM=&_NXE_EV_STREAM;

static void message_event_deliver(nxe_event* evt) {
  nxe_subscriber* sub=evt->receiver.sub;
  nxe_publisher* pub=sub->pub;
  assert(sub->evt==evt);
  nxweb_log_debug("message_event_deliver");
  sub->evt=evt->next_by_receiver;
  if (PUBLISHER_CLASS(pub)->do_publish) {
    PUBLISHER_CLASS(pub)->do_publish(pub, sub, evt->data);
  }
  else {
    SUBSCRIBER_CLASS(sub)->on_message(sub, pub, evt->data);
  }
}

static void message_event_free(nxe_loop* loop, nxe_event* evt) {
  nxp_free(&loop->free_event_pool, evt);
}

const nxe_event_class _NXE_EV_MESSAGE={.deliver=message_event_deliver, .free=message_event_free};
const nxe_event_class* const NXE_EV_MESSAGE=&_NXE_EV_MESSAGE;

static void callback_event_deliver(nxe_event* evt) {
  evt->receiver.callback(evt->data);
}

static void callback_event_free(nxe_loop* loop, nxe_event* evt) {
  nxp_free(&loop->free_event_pool, evt);
}

const nxe_event_class _NXE_EV_CALLBACK={.deliver=callback_event_deliver, .free=callback_event_free};
const nxe_event_class* const NXE_EV_CALLBACK=&_NXE_EV_CALLBACK;

const nxe_publisher_class _NXE_PUB_DEFAULT={.do_publish=0};
const nxe_publisher_class* const NXE_PUB_DEFAULT=&_NXE_PUB_DEFAULT;

void nxe_subscribe(nxe_loop* loop, nxe_publisher* pub, nxe_subscriber* sub) {
  assert(!sub->pub);
  assert(!sub->next);
  pub->super.loop=loop;
  sub->super.loop=loop;
  sub->pub=pub;
  sub->next=pub->sub;
  pub->sub=sub;
}

void nxe_unsubscribe(nxe_publisher* pub, nxe_subscriber* sub) {
  assert(sub->pub==pub);
  if (pub->sub==sub) {
    pub->sub=sub->next;
    sub->pub=0;
    sub->next=0;
  }
  else {
    nxe_subscriber* s=pub->sub;
    while (s) {
      if (s->next==sub) {
        s->next=sub->next;
        sub->pub=0;
        sub->next=0;
        break;
      }
      s=s->next;
    }
  }
  // unlink all scheduled events:
  nxe_event* evt;
  nxe_event* next_evt=sub->evt;
  nxe_loop* loop=sub->super.loop;
  while ((evt=next_evt)) {
    //nxweb_log_error("deleting undelivered event %d", evt->data.i);
    next_evt=evt->next_by_receiver;
    nxe_unlink(evt);
    nxp_free(&loop->free_event_pool, evt);
  }
  sub->evt=0;
}

void nxe_publish(nxe_publisher* pub, nxe_data data) {
  nxe_loop* loop=pub->super.loop;
  if (!loop) return; // no subscribers yet
  nxe_subscriber* sub=pub->sub;
  while (sub) {
    nxe_event* evt=nxp_alloc(&loop->free_event_pool);
    evt->cls=NXE_EV_MESSAGE;
    evt->receiver.sub=sub;
    evt->data=data;
    evt->next_by_receiver=0;
    evt->loop=0;
    nxe_link(loop, evt);
    // add to the end of list of active events
    if (!sub->evt) {
      sub->evt=evt;
    }
    else {
      nxe_event* e;
      for (e=sub->evt; e->next_by_receiver; e=e->next_by_receiver) ;
      e->next_by_receiver=evt;
    }
    sub=sub->next;
  }
}

void nxe_schedule_callback(nxe_loop* loop, void (*func)(nxe_data data), nxe_data data) {
  assert(loop);
  nxe_event* evt=nxp_alloc(&loop->free_event_pool);
  evt->cls=NXE_EV_CALLBACK;
  evt->receiver.callback=func;
  evt->data=data;
  evt->next_by_receiver=0;
  evt->loop=0;
  nxe_link(loop, evt);
}

void nxe_connect_streams(nxe_loop* loop, nxe_istream* is, nxe_ostream* os) {
  assert(loop);
  is->super.loop=loop;
  os->super.loop=loop;
  is->pair=os;
  os->pair=is;
  if (is->ready && os->ready) { // emit event
    is->evt.receiver.os=os;
    nxe_link(loop, &is->evt); // emit event
  }
}

void nxe_disconnect_streams(nxe_istream* is, nxe_ostream* os) {
  is->pair=0;
  os->pair=0;
  if (IS_IN_LOOP(&is->evt)) {
    //nxweb_log_error("deleting undelivered istream event");
    nxe_unlink(&is->evt);
  }
}

nxe_loop* nxe_create(int max_epoll_events) {
  int s=4;
  while (s<max_epoll_events) s<<=1; // align to power of 2
  max_epoll_events=s;
  nxe_loop* loop=nx_alloc(sizeof(nxe_loop)+sizeof(struct epoll_event)*max_epoll_events);
  if (!loop) return 0;
  memset(loop, 0, sizeof(nxe_loop));
  nxp_init(&loop->free_event_pool, sizeof(nxe_event), &loop->free_event_pool_initial_chunk, sizeof(nxp_chunk)+sizeof(loop->free_events));
  loop->epoll_events=(struct epoll_event*)((char*)(loop+1));
  loop->max_epoll_events=max_epoll_events;

  loop->gc_pub.super.cls.pub_cls=NXE_PUB_DEFAULT;

  loop->current_time=nxe_get_time_usec();
  loop->epoll_fd=epoll_create(1); // size ignored
  return loop;
}

void nxe_destroy(nxe_loop* loop) {
  nxp_finalize(&loop->free_event_pool);
  nx_free(loop);
}

time_t nxe_get_current_http_time(nxe_loop* loop) {
  if (loop->current_time - loop->last_http_time >= 1000000L) {
    time(&loop->http_time);
    loop->last_http_time=loop->current_time;
    loop->http_time_str[0]='\0';
    loop->iso8601_time_str[0]='\0';
  }
  return loop->http_time;
}

const char* nxe_get_current_http_time_str(nxe_loop* loop) {
  nxe_get_current_http_time(loop);
  if (!loop->http_time_str[0]) {
    struct tm tm;
    gmtime_r(&loop->http_time, &tm);
    nxweb_format_http_time(loop->http_time_str, &tm);
  }
  return loop->http_time_str;
}

const char* nxe_get_current_iso8601_time_str(nxe_loop* loop) {
  nxe_get_current_http_time(loop);
  if (!loop->iso8601_time_str[0]) {
    struct tm tm;
    localtime_r(&loop->http_time, &tm);
    nxweb_format_iso8601_time(loop->iso8601_time_str, &tm);
  }
  return loop->iso8601_time_str;
}
