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

#include "nxweb.h"

#include <errno.h>
#include <netdb.h>

#define IS_LINKED(hpx) ((hpx)->pool && ((hpx)->prev || (hpx)->pool->first==(hpx)))

static inline void nxd_http_proxy_link(nxd_http_proxy* hpx, nxd_http_proxy_pool* pp) {
#if 1
  // add to tail
  hpx->next=0;
  hpx->prev=pp->last;
  if (pp->last) pp->last->next=hpx;
  else pp->first=hpx;
  pp->last=hpx;
#else
  // add to head
  hpx->prev=0;
  hpx->next=pp->first;
  if (pp->first) pp->first->prev=hpx;
  else pp->last=hpx;
  pp->first=hpx;
#endif
}

static inline void nxd_http_proxy_unlink(nxd_http_proxy* hpx) {
  nxd_http_proxy_pool* pp=hpx->pool;
  if (hpx->prev) hpx->prev->next=hpx->next;
  else pp->first=hpx->next;
  if (hpx->next) hpx->next->prev=hpx->prev;
  else pp->last=hpx->prev;
  hpx->next=0;
  hpx->prev=0;
}

static void nxd_http_proxy_events_sub_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  // this callback is only subscribed while connection is in idle pooled state
  nxd_http_proxy* hpx=(nxd_http_proxy*)((char*)sub-offsetof(nxd_http_proxy, events_sub));
  if (data.i<0) {
    nxd_http_proxy_pool* pp=hpx->pool;
    nxd_http_proxy_finalize(hpx, 0);
    nxp_free(pp->free_pool, hpx);
  }
}

static const nxe_subscriber_class nxd_http_proxy_events_sub_class={.on_message=nxd_http_proxy_events_sub_on_message};

void nxd_http_proxy_init(nxd_http_proxy* hpx, nxp_pool* nxb_pool) {
  memset(hpx, 0, sizeof(*hpx));
  nxd_http_client_proto_init(&hpx->hcp, nxb_pool);
  nxd_socket_init(&hpx->sock);
}

int nxd_http_proxy_connect(nxd_http_proxy* hpx, nxe_loop* loop, const char* host, struct addrinfo* saddr) {
  hpx->hcp.host=host;
  int fd=socket(saddr->ai_family, saddr->ai_socktype|SOCK_NONBLOCK, saddr->ai_protocol);
  if (fd==-1) {
    nxweb_log_error("can't open socket %d", errno);
    return -1;
  }
  if (/*_nxweb_set_non_block(fd)<0 ||*/ _nxweb_setup_client_socket(fd)<0) {
    nxweb_log_error("can't setup http client socket");
    return -1;
  }
  if (connect(fd, saddr->ai_addr, saddr->ai_addrlen)) {
    if (errno!=EINPROGRESS && errno!=EALREADY && errno!=EISCONN) {
      nxweb_log_error("can't connect http client %d", errno);
      return -1;
    }
  }
  hpx->sock.fs.fd=fd;
  hpx->hcp.sock_fd=fd;
  nxe_register_fd_source(loop, &hpx->sock.fs);
  nxe_subscribe(loop, &hpx->sock.fs.data_error, &hpx->hcp.data_error);
  nxe_connect_streams(loop, &hpx->sock.fs.data_is, &hpx->hcp.data_in);
  nxe_connect_streams(loop, &hpx->hcp.data_out, &hpx->sock.fs.data_os);
  nxd_http_client_proto_connect(&hpx->hcp, loop);
  hpx->uid=nxweb_generate_unique_id();
  return 0;
}

void nxd_http_proxy_finalize(nxd_http_proxy* hpx, int good) {
  if (IS_LINKED(hpx)) nxd_http_proxy_unlink(hpx);
  nxd_http_client_proto_finalize(&hpx->hcp);
  nxd_socket_finalize(&hpx->sock, good);
}

nxweb_http_request* nxd_http_proxy_prepare(nxd_http_proxy* hpx) {
  return &hpx->hcp._req;
}

void nxd_http_proxy_start_request(nxd_http_proxy* hpx, nxweb_http_request* req) {
  nxd_http_client_proto_start_request(&hpx->hcp, req);
}

// Proxy pool methods:

static void gc_sub_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  nxd_http_proxy_pool* pp=(nxd_http_proxy_pool*)((char*)sub-offsetof(nxd_http_proxy_pool, gc_sub));
  nxp_gc(pp->free_pool);
}

static const nxe_subscriber_class gc_sub_class={.on_message=gc_sub_on_message};

void nxd_http_proxy_pool_init(nxd_http_proxy_pool* pp, nxe_loop* loop, nxp_pool* nxb_pool, const char* host, struct addrinfo* saddr) {
  pp->conn_count=
  pp->conn_count_max=0;
  pp->loop=loop;
  pp->nxb_pool=nxb_pool;
  pp->host=host;
  pp->saddr=saddr;
  pp->first=0;
  pp->last=0;
  time_t *samples=pp->backend_time_delta;
  int i;
  for (i=0; i<NXD_HTTP_PROXY_POOL_TIME_DELTA_SAMPLES; i++) {
    samples[i]=NXD_HTTP_PROXY_POOL_TIME_DELTA_NO_VALUE;
  }
  pp->free_pool=nxp_create(sizeof(nxd_http_proxy), NXD_FREE_PROXY_POOL_INITIAL_SIZE);
  nxe_init_subscriber(&pp->gc_sub, &gc_sub_class);
  nxe_subscribe(loop, &loop->gc_pub, &pp->gc_sub);
}

void nxd_http_proxy_pool_report_backend_time_delta(nxd_http_proxy_pool* pp, time_t delta) {
  pp->backend_time_delta[pp->backend_time_delta_idx]=delta;
  pp->backend_time_delta_idx=(pp->backend_time_delta_idx+1)%NXD_HTTP_PROXY_POOL_TIME_DELTA_SAMPLES;
}

time_t nxd_http_proxy_pool_get_backend_time_delta(nxd_http_proxy_pool* pp) {
  time_t *samples=pp->backend_time_delta;
  int i, cnt=0;
  time_t sum=0;
  for (i=0; i<NXD_HTTP_PROXY_POOL_TIME_DELTA_SAMPLES; i++) {
    if (samples[i]!=NXD_HTTP_PROXY_POOL_TIME_DELTA_NO_VALUE) {
      sum+=samples[i];
      cnt++;
    }
  }
  if (!cnt) return 0;
  time_t avg=(sum+cnt/2)/cnt;
  return avg;
}

nxd_http_proxy* nxd_http_proxy_pool_connect(nxd_http_proxy_pool* pp) {
  if (pp->first) {
    nxd_http_proxy* hpx=pp->first;
    nxd_http_proxy_unlink(hpx);
    nxe_unsubscribe(&hpx->hcp.events_pub, &hpx->events_sub);
    pp->conn_count++;
    if (pp->conn_count > pp->conn_count_max) pp->conn_count_max=pp->conn_count;
    return hpx;
  }
  else {
    nxd_http_proxy* hpx=nxp_alloc(pp->free_pool);
    nxd_http_proxy_init(hpx, pp->nxb_pool);
    hpx->pool=pp;
    if (nxd_http_proxy_connect(hpx, pp->loop, pp->host, pp->saddr)) return 0;
    pp->conn_count++;
    if (pp->conn_count > pp->conn_count_max) pp->conn_count_max=pp->conn_count;
    //nxweb_log_error("New conn to backend");
    return hpx;
  }
}

void nxd_http_proxy_pool_return(nxd_http_proxy* hpx, int closed) {
  nxd_http_proxy_pool* pp=hpx->pool;
  pp->conn_count--;
  if (closed || !hpx->hcp.request_complete || hpx->hcp.state!=HCP_IDLE || !hpx->hcp.resp.keep_alive) {
    nxd_http_proxy_finalize(hpx, 0);
    nxp_free(pp->free_pool, hpx);
  }
  else {
    nxd_http_client_proto_rearm(&hpx->hcp); // disconnect & unsubscribe & free resources
    nxe_init_subscriber(&hpx->events_sub, &nxd_http_proxy_events_sub_class);
    nxe_subscribe(pp->loop, &hpx->hcp.events_pub, &hpx->events_sub);
    nxd_http_proxy_link(hpx, pp);
  }
}

void nxd_http_proxy_pool_finalize(nxd_http_proxy_pool* pp) {
  if (!pp || !pp->saddr) return; // not initialized
  //nxweb_log_error("proxy_pool conn=%d max=%d", pp->conn_count, pp->conn_count_max);
  nxd_http_proxy* hpx;
  while ((hpx=pp->first)) {
    nxd_http_proxy_finalize(hpx, 0);
  }
  nxe_unsubscribe(&pp->loop->gc_pub, &pp->gc_sub);
  nxp_destroy(pp->free_pool);
}
