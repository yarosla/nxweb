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

#ifndef NX_WORKERS_H
#define	NX_WORKERS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "nx_queue_tpl.h"

#define NXWEB_MAX_WORKERS 512
#define NXWEB_MAX_IDLE_WORKERS_IN_QUEUE 32
#define NXWEB_MAX_WORKERS_IN_QUEUE 128
#define NXWEB_START_WORKERS_IN_QUEUE 16

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

