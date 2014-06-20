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

#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "misc.h"
#include "nx_alloc.h"
#include "nx_event.h"
#include "nx_workers.h"

__thread nxw_worker* _nxweb_worker_thread_data;

static void* nxw_worker_main(void* ptr);
static nxw_worker* nxw_create_worker(nxw_factory* f);
static void nxw_destroy_worker(nxw_worker* w);

static inline void link_worker(nxw_worker* w) {
  w->next=w->factory->list;
  if (w->factory->list) w->factory->list->prev=w;
  w->factory->list=w;
  w->prev=0;
}

static inline void unlink_worker(nxw_worker* w) {
  if (w->prev) w->prev->next=w->next;
  else w->factory->list=w->next;
  if (w->next) w->next->prev=w->prev;
  w->next=0;
  w->prev=0;
}

void nxw_init_factory(nxw_factory* f, nxe_loop* loop) {
  f->loop=loop;
  nx_queue_workers_init(&f->queue);
  pthread_mutex_init(&f->queue_mux, 0);
  int i;
  for (i=NXWEB_START_WORKERS_IN_QUEUE; i--; ) {
    nxw_create_worker(f);
  }
}

void nxw_finalize_factory(nxw_factory* f) {
  pthread_mutex_lock(&f->queue_mux);
  f->shutdown_in_progress=1;
  pthread_mutex_unlock(&f->queue_mux);

  nxweb_log_error("shutting down %d workers", f->worker_count);

  nxw_worker* w;
  while (!nx_queue_workers_pop(&f->queue, &w)) {
    //nxweb_log_error("waking up %p", (void*)w->tid);
    pthread_mutex_lock(&w->start_mux);
    pthread_cond_signal(&w->start_cond);
    pthread_mutex_unlock(&w->start_mux);
  }

  while ((w=f->list)) {
    pthread_join(w->tid, 0);
    nxw_destroy_worker(w);
  }

  pthread_mutex_destroy(&f->queue_mux);
}

void nxw_gc_factory(nxw_factory* f) {
  nxw_worker* w=f->list;
  nxw_worker* next;
  int cnt=0;
  while (w) {
    next=w->next;
    if (w->dead) {
      pthread_join(w->tid, 0);
      nxw_destroy_worker(w);
      cnt++;
    }
    w=next;
  }
  if (cnt) nxweb_log_error("gc destroyed %d dead workers", cnt);
  int extra=(nx_queue_workers_length(&f->queue) - NXWEB_MAX_IDLE_WORKERS_IN_QUEUE + 1)/2;
  // wake up half extra workers so we can destroy them dead in the next gc round
  for (; extra>0; extra--) {
    if (nx_queue_workers_pop(&f->queue, &w)) break;
    w->shutdown_in_progress=1;
    pthread_mutex_lock(&w->start_mux);
    pthread_cond_signal(&w->start_cond);
    pthread_mutex_unlock(&w->start_mux);
  }
}

nxw_worker* nxw_get_worker(nxw_factory* f) {
  if (f->shutdown_in_progress) return 0;
  nxw_worker* w;
  if (nx_queue_workers_pop(&f->queue, &w)) {
    if (f->worker_count>NXWEB_MAX_WORKERS) return 0; // no more workers
    w=nxw_create_worker(f);
  }
  return w;
}

void nxw_start_worker(nxw_worker* w, void (*job_func)(void* job_param), void* job_param, volatile int* job_done) {
  pthread_mutex_lock(&w->start_mux);
  w->do_job=job_func;
  w->job_param=job_param;
  w->job_done=job_done;
  *job_done=0;
  pthread_cond_signal(&w->start_cond);
  pthread_mutex_unlock(&w->start_mux);
}

static nxw_worker* nxw_create_worker(nxw_factory* f) {
  nxw_worker* w=nx_calloc(sizeof(nxw_worker));
  w->factory=f;
  pthread_cond_init(&w->start_cond, 0);
  pthread_mutex_init(&w->start_mux, 0);
  nxe_init_eventfd_source(&w->complete_efs, NXE_PUB_DEFAULT);
  nxe_register_eventfd_source(f->loop, &w->complete_efs);
  link_worker(w);
  f->worker_count++;
  if (pthread_create(&w->tid, 0, nxw_worker_main, w)) {
    nxweb_log_error("can't create worker thread");
    nxw_destroy_worker(w);
    return 0;
  }
  return w;
}

static void nxw_destroy_worker(nxw_worker* w) { // must be called from factory thread (which runs the loop)!!!
  nxe_unregister_eventfd_source(&w->complete_efs);
  nxe_finalize_eventfd_source(&w->complete_efs);
  pthread_cond_destroy(&w->start_cond);
  pthread_mutex_destroy(&w->start_mux);
  unlink_worker(w);
  w->factory->worker_count--;
  nx_free(w);
}

static void* nxw_worker_main(void* ptr) {
  nxw_worker* w=ptr;
  _nxweb_worker_thread_data=w;

  while (1) {
    // wait for start
    pthread_mutex_lock(&w->start_mux);
    while (!w->do_job && !w->shutdown_in_progress && !w->factory->shutdown_in_progress) {
      pthread_cond_wait(&w->start_cond, &w->start_mux);
      //nxweb_log_error("woken up %p", (void*)w->tid);
    }
    pthread_mutex_unlock(&w->start_mux);
    if (!w->do_job) break;

    w->do_job(w->job_param);
    w->do_job=0;
    __sync_synchronize(); // full memory barrier
    *w->job_done=1;
    __sync_synchronize(); // full memory barrier
    nxe_trigger_eventfd(&w->complete_efs);

    // put itself into queue
    pthread_mutex_lock(&w->factory->queue_mux);
    if (w->shutdown_in_progress || w->factory->shutdown_in_progress) {
      pthread_mutex_unlock(&w->factory->queue_mux);
      break;
    }
    int result=nx_queue_workers_push(&w->factory->queue, &w);
    pthread_mutex_unlock(&w->factory->queue_mux);
    if (result) break; // queue full => kill itself
  }

  w->dead=1;
  //nxweb_log_error("worker thread clean exit");
  return 0;
}
