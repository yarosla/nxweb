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

#include "nxweb.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <sys/mman.h>
#include <fcntl.h>

struct nxweb_server_config nxweb_server_config;

static pthread_t main_thread_id=0;

static volatile int shutdown_in_progress=0;
static volatile int num_connections=0;

static nxweb_net_thread_data net_threads[N_NET_THREADS];
__thread nxweb_net_thread_data* _nxweb_net_thread_data;

static nxweb_result default_on_headers(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_send_http_error(resp, 404, "Not Found");
  return NXWEB_ERROR;
}

// nxweb_handler _nxweb_default_handler={.priority=999999999, .on_headers=default_on_headers};
NXWEB_HANDLER(default, 0, .priority=999999999, .on_headers=default_on_headers);

int nxweb_select_handler(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_handler* handler, nxe_data handler_param) {
  conn->handler=handler;
  conn->handler_param=handler_param;
  if (handler->on_select) return handler->on_select(conn, req, resp);
  else return NXWEB_OK;
}

nxweb_result _nxweb_default_request_dispatcher(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_handler* h=nxweb_server_config.handler_list;
  const char* uri=req->uri;
  while (h) {
    if (!h->prefix || nxweb_url_prefix_match(uri, h->prefix, h->prefix_len)) {
      req->path_info=req->uri+h->prefix_len;
      nxweb_result res=nxweb_select_handler(conn, req, resp, h, h->param);
      if (res!=NXWEB_NEXT) {
        if (res==NXWEB_ERROR) {
          // request processing terminated by http error response
          if (req->content_length) resp->keep_alive=0; // close connection if there is body pending
          nxweb_start_sending_response(conn, resp);
          return NXWEB_ERROR;
        }
        if (res!=NXWEB_OK) {
          nxweb_log_error("handler %s on_select() returned error %d", h->name, res);
          break;
        }
        return NXWEB_OK;
      }
    }
    h=h->next;
  }

  req->path_info=0;
  nxweb_select_handler(conn, req, resp, &_nxweb_default_handler, (nxe_data)0);
  return NXWEB_OK;
}

void _nxweb_register_module(nxweb_module* module) {
  if (!nxweb_server_config.module_list) {
    nxweb_server_config.module_list=module;
    module->next=0;
    return;
  }
  else {
    nxweb_module* mod=nxweb_server_config.module_list;
    if (module->priority < mod->priority) {
      module->next=mod;
      nxweb_server_config.module_list=module;
    }
    else {
      while (mod->next && mod->next->priority <= module->priority) {
        mod=mod->next;
      }
      module->next=mod->next;
      mod->next=module;
    }
  }
}

void _nxweb_register_handler(nxweb_handler* handler, nxweb_handler* base) {
  handler->prefix_len=handler->prefix? strlen(handler->prefix) : 0;
  if (base) {
    if (!handler->on_select) handler->on_select=base->on_select;
    if (!handler->on_headers) handler->on_headers=base->on_headers;
    if (!handler->on_post_data) handler->on_post_data=base->on_post_data;
    if (!handler->on_post_data_complete) handler->on_post_data_complete=base->on_post_data_complete;
    if (!handler->on_request) handler->on_request=base->on_request;
    if (!handler->on_complete) handler->on_complete=base->on_complete;
    if (!handler->on_error) handler->on_error=base->on_error;
    if (!handler->flags) handler->flags=base->flags;
  }
  if (!nxweb_server_config.handler_list) {
    nxweb_server_config.handler_list=handler;
    handler->next=0;
    return;
  }
  else {
    nxweb_handler* h=nxweb_server_config.handler_list;
    if (handler->priority < h->priority) {
      handler->next=h;
      nxweb_server_config.handler_list=handler;
    }
    else {
      while (h->next && h->next->priority <= handler->priority) {
        h=h->next;
      }
      handler->next=h->next;
      h->next=handler;
    }
  }
}

static void invoke_request_handler_in_worker(void* ptr) {
  nxweb_http_server_connection* conn=ptr;
  conn->handler->on_request(conn, &conn->hsp.req, &conn->hsp._resp);
}

static void nxweb_http_server_connection_worker_complete_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  nxweb_http_server_connection* conn=OBJ_PTR_FROM_FLD_PTR(nxweb_http_server_connection, worker_complete, sub);
  nxe_unsubscribe(conn->worker_complete.pub, &conn->worker_complete);
  long cnt=0;
  while (!conn->worker_job_done) cnt++;
  __sync_synchronize(); // full memory barrier
  assert(conn->hsp._resp.content && conn->hsp._resp.content_length);
  nxweb_start_sending_response(conn, &conn->hsp._resp);
  if (cnt) nxweb_log_error("job not done in %ld steps", cnt);
}

/*
static void wait_worker_complete(nxweb_http_server_connection* conn) {
  long cnt=0;
  while (!conn->worker_job_done) cnt++;
  __sync_synchronize(); // full memory barrier
  assert(conn->hsp._resp.content && conn->hsp._resp.content_length);
  nxweb_start_sending_response(conn, &conn->hsp._resp);
  if (cnt) nxweb_log_error("job not done in %ld steps", cnt);
}
*/

static inline nxweb_result invoke_request_handler(nxweb_http_server_connection* conn, nxweb_http_request* req,
        nxweb_http_response* resp, nxweb_handler* h, nxweb_handler_flags flags) {
  if (flags&NXWEB_PARSE_PARAMETERS) nxweb_parse_request_parameters(req, 1); // !!(flags&NXWEB_PRESERVE_URI)
  if (flags&NXWEB_PARSE_COOKIES) nxweb_parse_request_cookies(req);
  nxweb_result res=NXWEB_OK;
  if (h->on_request) {
    if (flags&NXWEB_INWORKER) {
      nxw_worker* w=nxw_get_worker(&conn->tdata->workers_factory);
      if (!w) {
        nxweb_send_http_error(resp, 503, "Service Unavailable");
        nxweb_start_sending_response(conn, resp);
        return NXWEB_ERROR;
      }
      nxe_subscribe(conn->tdata->loop, &w->complete_efs.data_notify, &conn->worker_complete);
      nxw_start_worker(w, invoke_request_handler_in_worker, conn, &conn->worker_job_done);
      //wait_worker_complete(conn);
    }
    else {
      res=h->on_request(conn, req, resp);
      if (res!=NXWEB_ASYNC) nxweb_start_sending_response(conn, resp);
    }
  }
  return res;
}

static void nxweb_http_server_connection_events_sub_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  nxweb_http_server_connection* conn=(nxweb_http_server_connection*)((char*)sub-offsetof(nxweb_http_server_connection, events_sub));
  //nxe_loop* loop=sub->super.loop;
  nxweb_http_request* req=&conn->hsp.req;
  nxweb_http_response* resp=&conn->hsp._resp;
  if (data.i==NXD_HSP_REQUEST_RECEIVED) {
    assert(nxweb_server_config.request_dispatcher);
    nxweb_server_config.request_dispatcher(conn, req, resp);
    if (!conn->handler) conn->handler=&_nxweb_default_handler;

    if (conn->hsp.state==HSP_SENDING_HEADERS) return; // one of callbacks has already started sending headers

    nxweb_handler* h=conn->handler;
    nxweb_handler_flags flags=h->flags;

    if (flags&_NXWEB_HANDLE_MASK) {
      if (((!(flags&NXWEB_HANDLE_GET) || !req->get_method)
        && (!(flags&NXWEB_HANDLE_POST) || !req->post_method)
        && (!(flags&NXWEB_HANDLE_OTHER) || !req->other_method))
        || (req->content_length && !(flags&(NXWEB_HANDLE_POST|NXWEB_ACCEPT_CONTENT)))) {
          nxweb_send_http_error(resp, 405, "Method Not Allowed");
          if (req->content_length) resp->keep_alive=0; // close connection if there is body pending
          nxweb_start_sending_response(conn, resp);
          return;
      }
    }

    if (h->on_headers) {
      if (NXWEB_OK!=h->on_headers(conn, req, resp)) {
        // request processing terminated by http error response
        if (req->content_length) resp->keep_alive=0; // close connection if there is body pending
        nxweb_start_sending_response(conn, resp);
        return;
      }
    }

    if (conn->hsp.state==HSP_SENDING_HEADERS) return; // one of callbacks has already started sending headers

    if (req->content_length) {
      if (h->on_post_data) h->on_post_data(conn, req, resp);
      else {
        if (!conn->hsp.req_body_out.pair) { // stream not connected
          if (req->content_length>NXWEB_MAX_REQUEST_BODY_SIZE) {
            nxweb_send_http_error(resp, 413, "Request Entity Too Large");
            resp->keep_alive=0; // close connection
            nxweb_start_sending_response(conn, resp);
            return;
          }
          nxe_loop* loop=conn->tdata->loop;
          nxd_ibuffer_init(&conn->hsp.ib, conn->hsp.nxb, req->content_length>0? req->content_length+1 : NXWEB_MAX_REQUEST_BODY_SIZE);
          nxe_connect_streams(loop, &conn->hsp.req_body_out, &conn->hsp.ib.data_in);
          nxe_ostream_set_ready(loop, &conn->hsp.data_in);
        }
      }
    }
    else {
      invoke_request_handler(conn, req, resp, h, flags);
    }
  }
  else if (data.i==NXD_HSP_REQUEST_BODY_RECEIVED) {
    assert(conn->handler);
    nxweb_handler* h=conn->handler;
    nxweb_handler_flags flags=h->flags;
    if (h->on_post_data_complete) h->on_post_data_complete(conn, req, resp);
    else {
      if (conn->hsp.req_body_out.pair==&conn->hsp.ib.data_in) {
        req->content=nxd_ibuffer_get_result(&conn->hsp.ib, 0);
      }
    }
    invoke_request_handler(conn, req, resp, h, flags);
  }
  else if (data.i==NXD_HSP_REQUEST_COMPLETE) {
    if (conn->handler && conn->handler->on_complete) conn->handler->on_complete(conn, req, resp);
    conn->handler=0;
    conn->handler_param=(nxe_data)0;
  }
  else if (data.i<0) {
    if (conn->handler && conn->handler->on_error) conn->handler->on_error(conn, req, resp);
    conn->handler=0;
    conn->handler_param=(nxe_data)0;
    if (conn->hsp.headers_bytes_received) {
      nxweb_log_error("conn %p error: i=%d errno=%d state=%d rc=%d br=%d", conn, data.i, errno, conn->hsp.state, conn->hsp.request_count, conn->hsp.headers_bytes_received);
    }
    if (!conn->hsp.headers_bytes_received && (data.i==NXE_RDHUP || data.i==NXE_HUP || data.i==NXE_RDCLOSED)) {
      // normal close
      nxweb_http_server_connection_finalize(conn, 1);
    }
    else {
      nxweb_http_server_connection_finalize(conn, 0);
    }
  }
}

static const nxe_subscriber_class nxweb_http_server_connection_events_sub_class={.on_message=nxweb_http_server_connection_events_sub_on_message};
static const nxe_subscriber_class nxweb_http_server_connection_worker_complete_class={.on_message=nxweb_http_server_connection_worker_complete_on_message};

static void nxweb_http_server_connection_init(nxweb_http_server_connection* conn, nxweb_net_thread_data* tdata, int lconf_idx) {
  memset(conn, 0, sizeof(nxweb_http_server_connection));
  conn->tdata=tdata;
  conn->lconf_idx=lconf_idx;
  nxd_http_server_proto_init(&conn->hsp, tdata->free_conn_nxb_pool);
#ifdef WITH_SSL
  nxweb_server_listen_config* lconf=&nxweb_server_config.listen_config[conn->lconf_idx];
  if (lconf->secure) {
    conn->secure=1;
    nxd_ssl_server_socket_init(&conn->sock, lconf->x509_cred, lconf->priority_cache, &lconf->session_ticket_key);
  }
  else {
    nxd_socket_init((nxd_socket*)&conn->sock);
  }
#else
  nxd_socket_init(&conn->sock);
#endif // WITH_SSL
  conn->events_sub.super.cls.sub_cls=&nxweb_http_server_connection_events_sub_class;
  conn->worker_complete.super.cls.sub_cls=&nxweb_http_server_connection_worker_complete_class;
}

static void nxweb_http_server_connection_connect(nxweb_http_server_connection* conn, nxe_loop* loop, int fd) {
  conn->sock.fs.fd=fd;
  nxe_register_fd_source(loop, &conn->sock.fs);
  nxe_subscribe(loop, &conn->sock.fs.data_error, &conn->hsp.data_error);
  nxe_subscribe(loop, &conn->hsp.events_pub, &conn->events_sub);
  nxe_connect_streams(loop, &conn->sock.fs.data_is, &conn->hsp.data_in);
  nxe_connect_streams(loop, &conn->hsp.data_out, &conn->sock.fs.data_os);

  //__sync_add_and_fetch(&num_connections, 1);
}

void nxweb_http_server_connection_finalize(nxweb_http_server_connection* conn, int good) {
  //nxe_loop* loop=conn->sock.fs.data_is.super.loop;
  if (conn->worker_complete.pub) nxe_unsubscribe(conn->worker_complete.pub, &conn->worker_complete);
  nxd_http_server_proto_finalize(&conn->hsp);
  conn->sock.cls->finalize((nxd_socket*)&conn->sock, good);
  nxp_free(conn->tdata->free_conn_pool, conn);

  //if (!__sync_sub_and_fetch(&num_connections, 1)) nxweb_log_error("all connections closed");
}

static void on_net_thread_shutdown(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  int i;
  nxweb_net_thread_data* tdata=(nxweb_net_thread_data*)((char*)sub-offsetof(nxweb_net_thread_data, shutdown_sub));
  //nxe_loop* loop=sub->super.loop;
  nxe_unsubscribe(pub, sub);

  nxweb_server_listen_config* lconf;
  nxweb_http_server_listening_socket* lsock;
  for (i=0, lconf=nxweb_server_config.listen_config, lsock=tdata->listening_sock; i<NXWEB_MAX_LISTEN_SOCKETS; i++, lconf++, lsock++) {
    if (lconf->listen_fd) {
      nxe_unsubscribe(&lsock->listen_source.data_notify, &lsock->listen_sub);
      nxe_unregister_listenfd_source(&lsock->listen_source);
    }
  }

  nxe_unregister_eventfd_source(&tdata->shutdown_efs);
  nxe_finalize_eventfd_source(&tdata->shutdown_efs);

  nxw_finalize_factory(&tdata->workers_factory);

  // close keep-alive connections to backends
  for (i=0; i<NXWEB_NUM_PROXY_POOLS; i++) {
    nxd_http_proxy_pool_finalize(&tdata->proxy_pool[i]);
  }
}

static void on_net_thread_gc(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  nxweb_net_thread_data* tdata=(nxweb_net_thread_data*)((char*)sub-offsetof(nxweb_net_thread_data, gc_sub));
  nxp_gc(tdata->free_conn_pool);
  nxp_gc(tdata->free_conn_nxb_pool);
  nxp_gc(tdata->free_rbuf_pool);
  nxw_gc_factory(&tdata->workers_factory);
}

static void accept_connection(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  nxweb_http_server_listening_socket* lsock=(nxweb_http_server_listening_socket*)((char*)sub-offsetof(nxweb_http_server_listening_socket, listen_sub));
  int lconf_idx=lsock->idx;
  nxweb_net_thread_data* tdata=(nxweb_net_thread_data*)((char*)(lsock-lconf_idx)-offsetof(nxweb_net_thread_data, listening_sock));
  nxe_loop* loop=sub->super.loop;
  //static int next_net_thread_idx=0;
  //nxweb_accept accept_msg;
  int client_fd;
  struct sockaddr_in client_addr;
  socklen_t client_len=sizeof(client_addr);
  while (!shutdown_in_progress && (client_fd=accept4(lsock->listen_source.fd, (struct sockaddr *)&client_addr, &client_len, SOCK_NONBLOCK))!=-1) {
    if (/*_nxweb_set_non_block(client_fd) ||*/ _nxweb_setup_client_socket(client_fd)) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("failed to setup client socket");
      continue;
    }
    nxweb_http_server_connection* conn=nxp_alloc(tdata->free_conn_pool);
    nxweb_http_server_connection_init(conn, tdata, lconf_idx);
    inet_ntop(AF_INET, &client_addr.sin_addr, conn->remote_addr, sizeof(conn->remote_addr));
    nxweb_http_server_connection_connect(conn, loop, client_fd);
  }
  // log error
  if (!shutdown_in_progress && errno!=EAGAIN) {
    nxweb_log_error("accept() failed %d", errno);
  }
}

static const nxe_subscriber_class listen_sub_class={.on_message=accept_connection};
static const nxe_subscriber_class shutdown_sub_class={.on_message=on_net_thread_shutdown};
static const nxe_subscriber_class gc_sub_class={.on_message=on_net_thread_gc};

static void* net_thread_main(void* ptr) {
  nxweb_net_thread_data* tdata=ptr;
  _nxweb_net_thread_data=tdata;

  nxe_loop* loop=nxe_create(128);
  tdata->loop=loop;

  nxe_set_timer_queue_timeout(loop, NXWEB_TIMER_KEEP_ALIVE, NXWEB_KEEP_ALIVE_TIMEOUT);
  nxe_set_timer_queue_timeout(loop, NXWEB_TIMER_READ, NXWEB_READ_TIMEOUT);
  nxe_set_timer_queue_timeout(loop, NXWEB_TIMER_WRITE, NXWEB_WRITE_TIMEOUT);
  nxe_set_timer_queue_timeout(loop, NXWEB_TIMER_BACKEND, NXWEB_BACKEND_TIMEOUT);
  nxe_set_timer_queue_timeout(loop, NXWEB_TIMER_100CONTINUE, NXWEB_100CONTINUE_TIMEOUT);

  nxweb_server_listen_config* lconf;
  nxweb_http_server_listening_socket* lsock;
  int i;
  for (i=0, lconf=nxweb_server_config.listen_config, lsock=tdata->listening_sock; i<NXWEB_MAX_LISTEN_SOCKETS; i++, lconf++, lsock++) {
    lsock->idx=i;
    if (lconf->listen_fd) {
      nxe_init_listenfd_source(&lsock->listen_source, lconf->listen_fd, NXE_PUB_DEFAULT);
      nxe_register_listenfd_source(loop, &lsock->listen_source);
      nxe_init_subscriber(&lsock->listen_sub, &listen_sub_class);
      nxe_subscribe(loop, &lsock->listen_source.data_notify, &lsock->listen_sub);
    }
  }

  nxe_init_eventfd_source(&tdata->shutdown_efs, NXE_PUB_DEFAULT);
  nxe_register_eventfd_source(loop, &tdata->shutdown_efs);
  nxe_init_subscriber(&tdata->shutdown_sub, &shutdown_sub_class);
  nxe_subscribe(loop, &tdata->shutdown_efs.data_notify, &tdata->shutdown_sub);
  nxe_init_subscriber(&tdata->gc_sub, &gc_sub_class);
  nxe_subscribe(loop, &loop->gc_pub, &tdata->gc_sub);

  tdata->free_conn_pool=nxp_create(sizeof(nxweb_http_server_connection), 8);
  tdata->free_conn_nxb_pool=nxp_create(NXWEB_CONN_NXB_SIZE, 8);
  tdata->free_rbuf_pool=nxp_create(NXWEB_RBUF_SIZE, 2);

  nxw_init_factory(&tdata->workers_factory, loop);

  // initialize proxy pools:
  for (i=0; i<NXWEB_NUM_PROXY_POOLS; i++) {
    if (nxweb_server_config.http_proxy_pool_config[i].host) {
      nxd_http_proxy_pool_init(&tdata->proxy_pool[i], loop, tdata->free_conn_nxb_pool,
              nxweb_server_config.http_proxy_pool_config[i].host,
              nxweb_server_config.http_proxy_pool_config[i].saddr);
    }
  }

  nxweb_module* mod=nxweb_server_config.module_list;
  while (mod) {
    if (mod->on_thread_startup) {
      if (mod->on_thread_startup()) {
        nxweb_log_error("module %s on_thread_startup() returned non-zero", mod->name);
        exit(EXIT_SUCCESS); // not to be respawned
      }
      else {
        nxweb_log_error("module %s [%d] network thread successfully initialized", mod->name, mod->priority);
      }
    }
    mod=mod->next;
  }

  nxe_run(loop);

  mod=nxweb_server_config.module_list;
  while (mod) {
    if (mod->on_thread_shutdown) mod->on_thread_shutdown();
    mod=mod->next;
  }

  nxp_destroy(tdata->free_conn_pool);
  nxp_destroy(tdata->free_conn_nxb_pool);
  nxp_destroy(tdata->free_rbuf_pool);
/*
  for (i=0; i<NXWEB_NUM_PROXY_POOLS; i++) {
    if (nxweb_server_config.http_proxy_pool_config[i].host)
      nxd_http_proxy_pool_finalize(&tdata->proxy_pool[i]);
  }
*/
  nxe_destroy(loop);
  nxweb_log_error("network thread clean exit");
  return 0;
}

static void on_sigterm(int sig) {
  nxweb_log_error("SIGTERM/SIGINT(%d) received", sig);
  if (shutdown_in_progress) return;
  shutdown_in_progress=1; // tells net_threads to finish their work

  //nxe_break(main_loop); // this is a little bit dirty; should modify main loop from callback

  int i;
  nxweb_net_thread_data* tdata;
  for (i=0, tdata=net_threads; i<N_NET_THREADS; i++, tdata++) {
/*
    // wake up workers
    pthread_mutex_lock(&tdata->job_queue_mux);
    pthread_cond_broadcast(&tdata->job_queue_cond);
    pthread_mutex_unlock(&tdata->job_queue_mux);
*/

    nxe_trigger_eventfd(&tdata->shutdown_efs);
  }
  alarm(5); // make sure we terminate via SIGALRM if some connections do not close in 5 seconds
}

static void on_sigalrm(int sig) {
  nxweb_log_error("SIGALRM received. Exiting");
  exit(EXIT_SUCCESS);
}

int nxweb_listen(const char* host_and_port, int backlog, _Bool secure, const char* cert_file, const char* key_file, const char* dh_params_file) {
  assert(nxweb_server_config.listen_config_idx>=0 && nxweb_server_config.listen_config_idx<NXWEB_MAX_LISTEN_SOCKETS);

  nxweb_log_error("nxweb binding %s for http%s", host_and_port, secure?"s":"");

  nxweb_server_listen_config* lconf=&nxweb_server_config.listen_config[nxweb_server_config.listen_config_idx++];

  lconf->listen_fd=_nxweb_bind_socket(host_and_port, backlog);
  if (lconf->listen_fd==-1) {
    return -1;
  }
#ifdef WITH_SSL
  lconf->secure=secure;
  if (secure) {
    nxd_ssl_socket_init_server_parameters(&lconf->x509_cred, &lconf->dh_params, &lconf->priority_cache, &lconf->session_ticket_key, cert_file, key_file, dh_params_file);
  }
#endif // WITH_SSL
  return 0;
}

int nxweb_setup_http_proxy_pool(int idx, const char* host_and_port) {
  assert(idx>=0 && idx<NXWEB_NUM_PROXY_POOLS);
  nxweb_server_config.http_proxy_pool_config[idx].host=host_and_port;
  nxweb_server_config.http_proxy_pool_config[idx].saddr=_nxweb_resolve_host(host_and_port, 0);
  return !!nxweb_server_config.http_proxy_pool_config[idx].saddr;
}

void nxweb_run() {
  int i;

  pid_t pid=getpid();
  main_thread_id=pthread_self();

  nxweb_log_error("NXWEB startup: pid=%d N_NET_THREADS=%d pg=%d"
                  " short=%d int=%d long=%d size_t=%d evt=%d conn=%d req=%d td=%d",
                  (int)pid, N_NET_THREADS, (int)sysconf(_SC_PAGE_SIZE),
                  (int)sizeof(short), (int)sizeof(int), (int)sizeof(long), (int)sizeof(size_t),
                  (int)sizeof(nxe_event), (int)sizeof(nxweb_http_server_connection), (int)sizeof(nxweb_http_request),
                  (int)sizeof(nxweb_net_thread_data));

  nxweb_handler* h=nxweb_server_config.handler_list;
  while (h) {
    nxweb_log_error("handler %s [%d] registered for url: %s", h->name, h->priority, h->prefix);
    h=h->next;
  }

  nxweb_module* mod=nxweb_server_config.module_list;
  while (mod) {
    if (mod->on_server_startup) {
      if (mod->on_server_startup()) {
        nxweb_log_error("module %s on_server_startup() returned non-zero", mod->name);
        exit(EXIT_SUCCESS); // not to be respawned
      }
      else {
        nxweb_log_error("module %s [%d] successfully initialized", mod->name, mod->priority);
      }
    }
    if (mod->request_dispatcher) {
      nxweb_server_config.request_dispatcher=mod->request_dispatcher;
      nxweb_log_error("module %s [%d] registered custom request dispatcher", mod->name, mod->priority);
    }
    mod=mod->next;
  }

  if (!nxweb_server_config.request_dispatcher) {
    nxweb_server_config.request_dispatcher=_nxweb_default_request_dispatcher;
    nxweb_log_error("using default request dispatcher");
  }

  // Block signals for all threads
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGTERM);
  sigaddset(&set, SIGPIPE);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGHUP);
  if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
    nxweb_log_error("Can't set pthread_sigmask");
    exit(EXIT_FAILURE);
  }

  nxweb_net_thread_data* tdata;
  for (i=0, tdata=net_threads; i<N_NET_THREADS; i++, tdata++) {
    pthread_attr_t tattr;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(i, &cpuset);
    pthread_attr_init(&tattr);
    pthread_attr_setaffinity_np(&tattr, sizeof(cpu_set_t), &cpuset);
    pthread_create(&tdata->thread_id, &tattr, net_thread_main, tdata);
    pthread_attr_destroy(&tattr);
  }

  signal(SIGTERM, on_sigterm);
  signal(SIGINT, on_sigterm);
  signal(SIGALRM, on_sigalrm);

  // Unblock signals for the main thread;
  // other threads have inherited sigmask we set earlier
  sigdelset(&set, SIGPIPE); // except SIGPIPE
  if (pthread_sigmask(SIG_UNBLOCK, &set, NULL)) {
    nxweb_log_error("Can't unset pthread_sigmask");
    exit(EXIT_FAILURE);
  }

  for (i=0; i<N_NET_THREADS; i++) {
    pthread_join(net_threads[i].thread_id, 0);
  }

  mod=nxweb_server_config.module_list;
  while (mod) {
    if (mod->on_server_shutdown) mod->on_server_shutdown();
    mod=mod->next;
  }

  nxweb_server_listen_config* lconf;
  for (i=0, lconf=nxweb_server_config.listen_config; i<NXWEB_MAX_LISTEN_SOCKETS; i++, lconf++) {
    if (lconf->listen_fd) {
      close(lconf->listen_fd);
#ifdef WITH_SSL
      if (lconf->secure)
        nxd_ssl_socket_finalize_server_parameters(lconf->x509_cred, lconf->dh_params, lconf->priority_cache, &lconf->session_ticket_key);
#endif // WITH_SSL
    }
  }

  for (i=0; i<NXWEB_NUM_PROXY_POOLS; i++) {
    if (nxweb_server_config.http_proxy_pool_config[i].saddr)
      _nxweb_free_addrinfo(nxweb_server_config.http_proxy_pool_config[i].saddr);
  }

  nxweb_log_error("end of _nxweb_main()");
}
