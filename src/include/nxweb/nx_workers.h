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

#ifndef NX_WORKERS_H
#define	NX_WORKERS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "nx_queue_tpl.h"

#define NXWEB_MAX_WORKERS 512
#define NXWEB_MAX_IDLE_WORKERS_IN_QUEUE 16
#define NXWEB_MAX_WORKERS_IN_QUEUE 128
#define NXWEB_START_WORKERS_IN_QUEUE 0

typedef struct nxw_worker {
  struct nxw_factory* factory;
  pthread_t tid;
  pthread_cond_t start_cond;
  pthread_mutex_t start_mux;
  nxe_eventfd_source complete_efs;
  struct nxw_worker* prev;
  struct nxw_worker* next;
  volatile _Bool shutdown_in_progress;
  volatile _Bool dead;
  // job spec:
  void (*do_job)(void* job_param);
  void* job_param;
  volatile int* job_done;
} nxw_worker;

NX_QUEUE_DECLARE(workers, nxw_worker*, NXWEB_MAX_WORKERS_IN_QUEUE)

typedef struct nxw_factory {
  volatile _Bool shutdown_in_progress;
  int worker_count;
  nxe_loop* loop;
  pthread_mutex_t queue_mux;
  nx_queue_workers queue;
  nxw_worker* list;
} nxw_factory;

void nxw_init_factory(nxw_factory* f, nxe_loop* loop);
void nxw_finalize_factory(nxw_factory* f);
void nxw_gc_factory(nxw_factory* f);
nxw_worker* nxw_get_worker(nxw_factory* f);
void nxw_start_worker(nxw_worker* w, void (*job_func)(void* job_param), void* job_param, volatile int* job_done);

#ifdef	__cplusplus
}
#endif

#endif	/* NX_WORKERS_H */

