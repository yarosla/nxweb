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

#include "nxweb_internal.h"

typedef struct nxweb_accept {
  int client_fd;
  struct in_addr sin_addr;
} nxweb_accept;

typedef struct nxweb_accept_queue {
  nx_queue q;
  nxweb_accept jobs[NXWEB_ACCEPT_QUEUE_SIZE];
} nxweb_accept_queue;

static inline void nxweb_accept_queue_init(nxweb_accept_queue* jq) {
  nx_queue_init(&jq->q, sizeof(nxweb_accept), NXWEB_ACCEPT_QUEUE_SIZE);
}

static inline nxweb_accept* nxweb_accept_queue_push_alloc(nxweb_accept_queue* jq) {
  return nx_queue_push_alloc(&jq->q);
}

static inline nxweb_accept* nxweb_accept_queue_pop(nxweb_accept_queue* jq) {
  return nx_queue_pop(&jq->q);
}

static inline int nxweb_accept_queue_is_empty(nxweb_accept_queue* jq) {
  return nx_queue_is_empty(&jq->q);
}

static inline int nxweb_accept_queue_is_full(nxweb_accept_queue* jq) {
  return nx_queue_is_full(&jq->q);
}

static pthread_t main_thread_id=0;
static struct ev_loop *main_loop=0;

static nxweb_accept_queue accept_queue;
static pthread_mutex_t accept_queue_mux;

static nxweb_job_queue job_queue;
static pthread_mutex_t job_queue_mux;
static pthread_cond_t job_queue_cond;

static volatile sig_atomic_t shutdown_in_progress=0;
static volatile int num_connections=0;

static int ev_next_thread_idx=0;
static nxweb_net_thread net_threads[N_NET_THREADS];
static pthread_t worker_threads[N_WORKER_THREADS];

// local prototypes:
static void socket_read_cb(struct ev_loop *loop, ev_io *w, int revents);
static void socket_write_cb(struct ev_loop *loop, ev_io *w, int revents);
static void read_request_timeout_cb(struct ev_loop *loop, ev_timer *w, int revents);
static void keep_alive_timeout_cb(struct ev_loop *loop, ev_timer *w, int revents);
static void write_response_timeout_cb(struct ev_loop *loop, ev_timer *w, int revents);
static void data_ready_cb(struct ev_loop *loop, struct ev_async *w, int revents);
static void dispatch_request(struct ev_loop *loop, nxweb_connection *conn);


static nxweb_connection* new_connection(struct ev_loop *loop, int client_fd, struct in_addr* sin_addr) {
  nxweb_connection* conn=(nxweb_connection*)calloc(1, sizeof(nxweb_connection));
  if (!conn) return 0;

  conn->fd=client_fd;
  inet_ntop(AF_INET, sin_addr, conn->remote_addr, sizeof(conn->remote_addr));
  conn->loop=loop;

  ev_io_init(&conn->watch_socket_read, socket_read_cb, client_fd, EV_READ);
  ev_io_start(loop, &conn->watch_socket_read);

  ev_timer_init(&conn->watch_keep_alive_time, keep_alive_timeout_cb, KEEP_ALIVE_TIMEOUT, KEEP_ALIVE_TIMEOUT);
  ev_timer_start(loop, &conn->watch_keep_alive_time);

  // init but not start:
  ev_io_init(&conn->watch_socket_write, socket_write_cb, conn->fd, EV_WRITE);
  ev_async_init(&conn->watch_async_data_ready, data_ready_cb);

  __sync_add_and_fetch(&num_connections, 1);

  return conn;
}

static void close_connection(struct ev_loop *loop, nxweb_connection* conn) {
  if (ev_is_active(&conn->watch_keep_alive_time)) ev_timer_stop(loop, &conn->watch_keep_alive_time);
  if (ev_is_active(&conn->watch_read_request_time)) ev_timer_stop(loop, &conn->watch_read_request_time);
  if (ev_is_active(&conn->watch_write_response_time)) ev_timer_stop(loop, &conn->watch_write_response_time);
  if (ev_is_active(&conn->watch_socket_read)) ev_io_stop(loop, &conn->watch_socket_read);
  if (ev_is_active(&conn->watch_socket_write)) ev_io_stop(loop, &conn->watch_socket_write);
  if (ev_is_active(&conn->watch_async_data_ready)) ev_async_stop(loop, &conn->watch_async_data_ready);

  if (conn->cstate==NXWEB_CS_TIMEOUT || conn->cstate==NXWEB_CS_ERROR) _nxweb_close_bad_socket(conn->fd);
  else _nxweb_close_good_socket(conn->fd);

  if (conn->request->sendfile_fd) close(conn->request->sendfile_fd);

  obstack_free(&conn->data, 0);
  // check if obstack has been initialized; free it if it was
  if (conn->user_data.chunk) obstack_free(&conn->user_data, 0);

  free(conn);

  int connections_left=__sync_sub_and_fetch(&num_connections, 1);
  assert(connections_left>=0);
  if (connections_left==0) { // for debug only
    nxweb_log_error("all connections closed");
  }
}

static void conn_request_received(struct ev_loop *loop, nxweb_connection* conn, int unfinished) {
  nxweb_request* req=conn->request;

  // finish receiving request headers or content in case of error or timeout
  if (unfinished) obstack_finish(&conn->data);

  if (!req->content_length || req->content_received < req->content_length) req->request_body=0;

  ev_timer_stop(loop, &conn->watch_read_request_time);
  ev_io_stop(loop, &conn->watch_socket_read);
}

static void rearm_connection(struct ev_loop *loop, nxweb_connection* conn) {
  if (conn->request->sendfile_fd) close(conn->request->sendfile_fd);
  obstack_free(&conn->data, conn->request);
  conn->request=0;

  conn->cstate=NXWEB_CS_WAITING_REQUEST;
  ev_io_start(loop, &conn->watch_socket_read);
  ev_timer_init(&conn->watch_keep_alive_time, keep_alive_timeout_cb, KEEP_ALIVE_TIMEOUT, KEEP_ALIVE_TIMEOUT);
  ev_timer_start(loop, &conn->watch_keep_alive_time);
}

//static void* nxweb_malloc(size_t size) {
//  nxweb_log_error("nxweb_malloc(%d)", (int)size);
//  return malloc(size);
//}
//
//static void nxweb_free(void* ptr) {
//  nxweb_log_error("nxweb_free()");
//  free(ptr);
//}

static nxweb_request* new_request(struct ev_loop *loop, nxweb_connection* conn) {
  //if (!conn->data.chunk) obstack_specify_allocation(&conn->data, DEFAULT_CHUNK_SIZE, 0, nxweb_malloc, nxweb_free);
  if (!conn->data.chunk) obstack_specify_allocation(&conn->data, DEFAULT_CHUNK_SIZE, 0, malloc, free);
  conn->request=obstack_alloc(&conn->data, sizeof(nxweb_request));
  memset(conn->request, 0, sizeof(nxweb_request));
  conn->request->conn=conn;

  ev_timer_stop(loop, &conn->watch_keep_alive_time);
  ev_timer_init(&conn->watch_read_request_time, read_request_timeout_cb, READ_REQUEST_TIMEOUT, READ_REQUEST_TIMEOUT);
  ev_timer_start(loop, &conn->watch_read_request_time);

  return conn->request;
}

static void start_sending_response(struct ev_loop *loop, nxweb_connection *conn) {

  _nxweb_finalize_response_writing_state(conn->request);

  if (!conn->request->out_headers) {
    _nxweb_prepare_response_headers(conn->request);
  }

  //conn->request->write_pos=0;
  //conn->request->header_sent=0;
  conn->cstate=NXWEB_CS_SENDING_HEADERS;

  if (!conn->sending_100_continue) {
    // if we are in process of sending 100-continue these watchers have already been activated
    ev_timer_init(&conn->watch_write_response_time, write_response_timeout_cb, WRITE_RESPONSE_TIMEOUT, WRITE_RESPONSE_TIMEOUT);
    ev_timer_start(loop, &conn->watch_write_response_time);
    ev_io_start(loop, &conn->watch_socket_write);
  }
}

static void write_response_timeout_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
  nxweb_connection *conn=((nxweb_connection*)(((char*)w)-offsetof(nxweb_connection, watch_write_response_time)));
  conn->cstate=NXWEB_CS_TIMEOUT;
  nxweb_log_error("write timeout - connection [%s] closed", conn->remote_addr);
  close_connection(loop, conn);
}

static void read_request_timeout_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
  nxweb_connection *conn=((nxweb_connection*)(((char*)w)-offsetof(nxweb_connection, watch_read_request_time)));

  nxweb_log_error("connection [%s] request timeout", conn->remote_addr);
  conn_request_received(loop, conn, 1);
  conn->keep_alive=0;
  nxweb_send_http_error(conn->request, 408, "Request Timeout");
  start_sending_response(loop, conn);
}

static void keep_alive_timeout_cb(struct ev_loop *loop, struct ev_timer *w, int revents) {
  nxweb_connection *conn=((nxweb_connection*)(((char*)w)-offsetof(nxweb_connection, watch_keep_alive_time)));
  conn->cstate=NXWEB_CS_TIMEOUT;
  nxweb_log_error("keep-alive connection [%s] closed", conn->remote_addr);
  close_connection(loop, conn);
}

static void data_ready_cb(struct ev_loop *loop, struct ev_async *w, int revents) {
  nxweb_connection *conn=((nxweb_connection*)(((char*)w)-offsetof(nxweb_connection, watch_async_data_ready)));
  nxweb_request* req=conn->request;

  ev_async_stop(loop, w);

  if (req->handler_result==NXWEB_NEXT) {
    // dispatch again
    dispatch_request(loop, conn);
    return;
  }

  start_sending_response(loop, conn);
}

static const char* response_100_continue = "HTTP/1.1 100 Continue\r\n\r\n";

static const void* next_write_bytes(nxweb_request* req, int* size) {
  nxweb_connection* conn=req->conn;
  const void* bytes=0;
  int bytes_avail=0;
  if (conn->sending_100_continue) { // sending 100-continue
    assert(req->write_pos<=strlen(response_100_continue));
    bytes=response_100_continue + req->write_pos;
    bytes_avail=strlen(bytes);
  }
  else {
    if (conn->cstate==NXWEB_CS_SENDING_HEADERS) {
      bytes=req->out_headers? req->out_headers + req->write_pos : 0;
      bytes_avail=bytes? strlen(bytes) : 0;
      if (!bytes_avail) {
        // header complete => start sending body
        conn->cstate=NXWEB_CS_SENDING_BODY;
        req->write_chunk=req->out_body_chunk;
        req->write_pos=0;
      }
    }
    if (conn->cstate==NXWEB_CS_SENDING_BODY) {
      if (!req->head_method) {
        while (req->write_chunk) {
          bytes=req->write_chunk->data + req->write_pos;
          bytes_avail=req->write_chunk->size - req->write_pos;
          if (bytes_avail) break;
          req->write_chunk=req->write_chunk->next;
          req->write_pos=0;
        }
      }
    }
  }
  *size=bytes_avail;
  return bytes;
}

static void socket_write_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
  nxweb_connection *conn=((nxweb_connection*)(((char*)w)-offsetof(nxweb_connection, watch_socket_write)));
  nxweb_request* req=conn->request;

  if (revents & EV_WRITE) {
    int bytes_avail, bytes_sent=0;
    const void* bytes;
    _nxweb_batch_write_begin(conn->fd);
    do {
      bytes=next_write_bytes(req, &bytes_avail);
      if (bytes_avail) {
        bytes_sent=write(conn->fd, bytes, bytes_avail);
        req->write_pos+=bytes_sent;
        if (bytes_sent>0) {
          ev_timer_again(loop, &conn->watch_write_response_time);
        }
      }
      else if (req->sendfile_fd && req->sendfile_offset<req->sendfile_length && !req->head_method) {
        bytes_avail=req->sendfile_length - req->sendfile_offset;
        bytes_sent=sendfile(conn->fd, req->sendfile_fd, &req->sendfile_offset, bytes_avail);
        //nxweb_log_error("sendfile: %d bytes sent", bytes_sent); // debug only
      }
      else { // all sent
        if (conn->sending_100_continue) {
          if (conn->cstate==NXWEB_CS_SENDING_HEADERS) {
            // we've already reached sending headers phase
            // (this could have happened if we were writing slower than we were reading)
            // do not stop watchers, continue writing headers
            conn->sending_100_continue=0;
            req->write_pos=0;
            continue;
          }
        }
        _nxweb_batch_write_end(conn->fd);
        ev_io_stop(loop, &conn->watch_socket_write);
        ev_timer_stop(loop, &conn->watch_write_response_time);
        if (conn->sending_100_continue) {
          conn->sending_100_continue=0;
          req->write_pos=0;
        }
        else if (req->conn->keep_alive && !shutdown_in_progress) {
          // rearm connection for keep-alive
          rearm_connection(loop, conn);
        }
        else {
          _nxweb_batch_write_end(conn->fd);
          conn->cstate=NXWEB_CS_CLOSING;
          //nxweb_log_error("connection closed OK");
          close_connection(loop, conn);
        }
        return;
      }
    } while (bytes_sent==bytes_avail);
    _nxweb_batch_write_end(conn->fd);
  }
}

static nxweb_result default_uri_callback(nxweb_uri_handler_phase phase, nxweb_request *req) {
  nxweb_send_http_error(req, 404, "Not Found");
  return NXWEB_OK;
}

static const nxweb_uri_handler default_handler={0, default_uri_callback, NXWEB_INPROCESS|NXWEB_HANDLE_ANY};

static const nxweb_uri_handler* find_handler(nxweb_request* req) {
  if (req->handler==&default_handler) return 0; // no more handlers
  char c;
  const char* uri=req->uri;
  int uri_len=strlen(uri);
  req->path_info=uri;
  const nxweb_module* const* module=req->handler_module? req->handler_module : nxweb_modules;
  const nxweb_uri_handler* handler=req->handler? req->handler+1 : (*module? (*module)->uri_handlers : 0);
  while (*module) {
    while (handler && handler->callback && handler->uri_prefix) {
      int prefix_len=strlen(handler->uri_prefix);
      if (prefix_len==0 || (prefix_len<=uri_len
          && strncmp(uri, handler->uri_prefix, prefix_len)==0
          && ((c=uri[prefix_len])==0 || c=='?' || c=='/'))) {
        req->path_info=uri+prefix_len;
        req->handler=handler;
        req->handler_module=module; // save position
        return handler;
      }
      handler++;
    }
    module++;
    handler=(*module)? (*module)->uri_handlers : 0;
  }
  handler=&default_handler;
  req->handler=handler;
  req->handler_module=module; // save position
  return handler;
}

static int prepare_request_for_handler(struct ev_loop *loop, nxweb_request* req) {
  const nxweb_uri_handler* handler=req->handler;
  unsigned flags=handler->flags;
  if ((flags&_NXWEB_HANDLE_MASK) && !(flags&NXWEB_HANDLE_ANY)) {
    if ((!(flags&NXWEB_HANDLE_GET) || (!!strcasecmp(req->method, "GET") && !!strcasecmp(req->method, "HEAD")))
      && (!(flags&NXWEB_HANDLE_POST) || !!strcasecmp(req->method, "POST"))) {
        nxweb_send_http_error(req, 405, "Method Not Allowed");
        start_sending_response(loop, req->conn);
        return -1;
    }
  }
  if (flags&NXWEB_PARSE_PARAMETERS) nxweb_parse_request_parameters(req, 0);
  if (flags&NXWEB_PARSE_COOKIES) nxweb_parse_request_cookies(req);
  return 0;
}

static void dispatch_request(struct ev_loop *loop, nxweb_connection *conn) {
  nxweb_request* req=conn->request;
  while (1) {
    const nxweb_uri_handler* handler=find_handler(req);
    assert(handler!=0); // default handler never returns NXWEB_NEXT
    if (prepare_request_for_handler(loop, req)) return;
    if (handler->flags&NXWEB_INPROCESS) {
      req->handler_result=handler->callback(NXWEB_PH_CONTENT, req);
      if (req->handler_result==NXWEB_NEXT) continue;
      start_sending_response(loop, conn);
    }
    else {
      // go async
      ev_async_start(loop, &conn->watch_async_data_ready);
      // hand over to worker thread
      pthread_mutex_lock(&job_queue_mux);
      nxweb_job* job=nxweb_job_queue_push_alloc(&job_queue);
      if (job) {
        job->conn=conn;
        pthread_cond_signal(&job_queue_cond);
        pthread_mutex_unlock(&job_queue_mux);
      }
      else { // queue full
        pthread_mutex_unlock(&job_queue_mux);
        nxweb_send_http_error(req, 503, "Service Unavailable");
        start_sending_response(loop, conn);
      }
    }
    break;
  }
}

static int next_room_for_read(struct ev_loop *loop, nxweb_connection* conn, void** room, int* size) {
  nxweb_request* req=conn->request;
  obstack* ob=&conn->data;

  int size_left=obstack_room(ob);

  if (size_left==0) {
    if (conn->cstate==NXWEB_CS_RECEIVING_HEADERS) {
      // do not expand; initial buffer should be enough
      // request headers too long
      nxweb_log_error("connection [%s] rejected (request headers too long)", conn->remote_addr);
      conn_request_received(loop, conn, 1);
      conn->cstate=NXWEB_CS_ERROR;
      conn->keep_alive=0;
      nxweb_send_http_error(req, 400, "Bad Request");
      start_sending_response(loop, conn);
      return -1;
    }
    else {
      assert(req->content_length<0); // unspecified length: chunked (or until close?)
      int received_size=req->content_received;
      int add_size=min(REQUEST_CONTENT_SIZE_LIMIT-received_size, received_size);
      if (add_size<=0) {
        // Too long
        obstack_free(ob, obstack_finish(ob));
        conn_request_received(loop, conn, 0);
        conn->cstate=NXWEB_CS_ERROR;
        conn->keep_alive=0;
        nxweb_send_http_error(req, 413, "Request Entity Too Large");
        // switch to sending response
        start_sending_response(loop, conn);
        return -1;
      }
      obstack_make_room(ob, add_size);
    }
  }

  *room=obstack_next_free(ob);
  *size=obstack_room(ob);
  assert(*size>0);
  return 0;
}

static int do_read(struct ev_loop *loop, nxweb_connection* conn, void* room, int size) {

  int bytes_received=read(conn->fd, room, size);

  if (bytes_received<0) { // EAGAIN or ...?
    char buf[1024];
    strerror_r(errno, buf, sizeof(buf));
    nxweb_log_error("read() returned %d: %d %s", bytes_received, errno, buf);
    return -1; // EAGAIN or ...?
  }

  if (!bytes_received) { // connection closed by client
    // this is OK for keep-alive connections
    if (!conn->keep_alive)
      nxweb_log_error("connection [%s] closed (nothing to read)", conn->remote_addr);
    conn->cstate=NXWEB_CS_CLOSING;
    close_connection(loop, conn);
    return -2;
  }

  return bytes_received;
}

static int is_request_body_complete(nxweb_request* req, const char* body) {
  if (req->content_length==0) {
    return 1; // no body needed
  }
  else if (req->content_length>0) {
    return req->content_received >= req->content_length;
  }
  else if (req->chunked_request) { // req->content_length<0
    // verify chunked
    return _nxweb_verify_chunked(body, req->content_received)>=0;
  }
  return 1;
}

static void socket_read_cb(struct ev_loop *loop, ev_io *w, int revents) {
  nxweb_connection *conn=((nxweb_connection*)(((char*)w)-offsetof(nxweb_connection, watch_socket_read)));
  nxweb_request* req=conn->request;
  obstack* ob=&conn->data;

  if (revents & EV_READ) {
    if (!req) { // just started receiving new request
      req=new_request(loop, conn);
      conn->cstate=NXWEB_CS_RECEIVING_HEADERS;
    }

    void* room;
    int room_size, bytes_received;

    do {
      if (next_room_for_read(loop, conn, &room, &room_size)) return;
      bytes_received=do_read(loop, conn, room, room_size);
      if (bytes_received<=0) return;
      obstack_blank_fast(ob, bytes_received);

      if (conn->cstate==NXWEB_CS_RECEIVING_HEADERS) {
        void* in_headers=obstack_base(ob);
        int in_headers_size=obstack_object_size(ob);
        char* end_of_request_headers=_nxweb_find_end_of_http_headers(in_headers, in_headers_size);

        if (!end_of_request_headers) {
          nxweb_log_error("partial headers receive (all ok)"); // rare case; log for debug
          return;
        }

        // null-terminate and finish
        obstack_1grow(ob, 0);
        req->in_headers=obstack_finish(ob);

        // parse request
        if (_nxweb_parse_http_request(req, req->in_headers==in_headers? end_of_request_headers:0, in_headers_size)) {
          conn_request_received(loop, conn, 1);
          conn->cstate=NXWEB_CS_ERROR;
          conn->keep_alive=0;
          nxweb_send_http_error(req, 400, "Bad Request");
          // switch to sending response
          start_sending_response(loop, conn);
          return;
        }

        if (is_request_body_complete(req, req->request_body)) {
          // receiving request complete
          if (req->chunked_request) _nxweb_decode_chunked_request(req);
          conn_request_received(loop, conn, 0);
          conn->cstate=NXWEB_CS_HANDLING;
          dispatch_request(loop, conn);
          return;
        }

        // so not all content received with headers

        if (req->content_length > REQUEST_CONTENT_SIZE_LIMIT) {
          conn_request_received(loop, conn, 1);
          conn->cstate=NXWEB_CS_ERROR;
          conn->keep_alive=0;
          nxweb_send_http_error(req, 413, "Request Entity Too Large");
          // switch to sending response
          start_sending_response(loop, conn);
          return;
        }

        if (req->content_length>0) {
          // body size specified; pre-allocate buffer for the content
          obstack_make_room(ob, req->content_length+1); // plus null-terminator char
        }

        if (req->content_received>0) {
          // copy what we have already received with headers
          void* new_body_ptr=obstack_next_free(ob);
          obstack_grow(ob, req->request_body, req->content_received);
          req->request_body=new_body_ptr;
        }

        // continue receiving request body
        conn->cstate=NXWEB_CS_RECEIVING_BODY;
        ev_timer_again(loop, &conn->watch_read_request_time);

        if (req->expect_100_continue && !req->content_received) {
          // send 100-continue
          conn->sending_100_continue=1;
          req->write_pos=0;
          ev_timer_init(&conn->watch_write_response_time, write_response_timeout_cb, WRITE_RESPONSE_TIMEOUT, WRITE_RESPONSE_TIMEOUT);
          ev_timer_start(loop, &conn->watch_write_response_time);
          ev_io_start(loop, &conn->watch_socket_write);
          // do not stop reading
        }
      }
      else if (conn->cstate==NXWEB_CS_RECEIVING_BODY) {
        req->content_received+=bytes_received;
        if (is_request_body_complete(req, obstack_base(ob))) {
          // receiving request complete
          // append null-terminator and close input buffer
          obstack_1grow(ob, 0);
          req->request_body=obstack_finish(ob);
          if (req->chunked_request) _nxweb_decode_chunked_request(req);
          conn_request_received(loop, conn, 0);
          conn->cstate=NXWEB_CS_HANDLING;
          dispatch_request(loop, conn);
          return;
        }
        nxweb_log_error("partial receive request body (all ok)"); // for debug only
      }
    } while (bytes_received==room_size);
  }
}


static void net_thread_accept_cb(struct ev_loop *loop, struct ev_async *w, int revents) {
  nxweb_accept a, *pa;

  while (1) {
    pthread_mutex_lock(&accept_queue_mux);
    pa=nxweb_accept_queue_pop(&accept_queue);
    if (pa) memcpy(&a, pa, sizeof(a));
    pthread_mutex_unlock(&accept_queue_mux);

    if (pa) {
      if (!new_connection(loop, a.client_fd, &a.sin_addr)) {
        nxweb_log_error("out of memory for connection");
        _nxweb_close_bad_socket(a.client_fd);
      }
    }
    else {
      break;
    }
  }
}

static void main_accept_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
  int client_fd;
  struct sockaddr_in client_addr;
  socklen_t client_len=sizeof(client_addr);
  nxweb_accept* accept_msg;
  while ((client_fd=accept(w->fd, (struct sockaddr *)&client_addr, &client_len))!=-1) {
    if (_nxweb_set_non_block(client_fd) || _nxweb_setup_client_socket(client_fd)) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("failed to set client socket to non-blocking");
      return;
    }

    nxweb_net_thread* tdata=&net_threads[ev_next_thread_idx];

    pthread_mutex_lock(&accept_queue_mux);
    accept_msg=nxweb_accept_queue_push_alloc(&accept_queue);
    if (!accept_msg) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("accept queue full; dropping connection");
    }
    accept_msg->client_fd=client_fd;
    memcpy(&accept_msg->sin_addr, &client_addr.sin_addr, sizeof(accept_msg->sin_addr));
    pthread_mutex_unlock(&accept_queue_mux);

    ev_async_send(tdata->loop, &tdata->watch_accept);

    ev_next_thread_idx=(ev_next_thread_idx+1)%N_NET_THREADS; // round-robin
  }
}

static void net_thread_shutdown_cb(struct ev_loop *loop, struct ev_async *w, int revents) {
  nxweb_net_thread* tdata=((nxweb_net_thread*)(((char*)w)-offsetof(nxweb_net_thread, watch_shutdown)));

  ev_async_stop(loop, &tdata->watch_shutdown);
  ev_async_stop(loop, &tdata->watch_accept);
}

static void* worker_thread_main(void* pdata) {
  nxweb_job* job;
  nxweb_connection* conn;

  while (!shutdown_in_progress) {
    pthread_mutex_lock(&job_queue_mux);
    while (!(job=nxweb_job_queue_pop(&job_queue)) && !shutdown_in_progress) {
      pthread_cond_wait(&job_queue_cond, &job_queue_mux);
    }
    conn=job? job->conn : 0;
    pthread_mutex_unlock(&job_queue_mux);
    if (conn) {
      conn->request->handler_result=conn->request->handler->callback(NXWEB_PH_CONTENT, conn->request);
      ev_async_send(conn->loop, &conn->watch_async_data_ready);
    }
  }

  nxweb_log_error("worker thread exited");
  return 0;
}

static void* ev_thread_main(void* pdata) {
  nxweb_net_thread* tdata=(nxweb_net_thread*)pdata;
  ev_run(tdata->loop, 0);
  ev_loop_destroy(tdata->loop);
  nxweb_log_error("net thread exited");
  return 0;
}

// Signal server to shutdown. Async function. Can be called from worker threads.
void nxweb_shutdown() {
  pthread_kill(main_thread_id, SIGTERM);
}

static void do_shutdown() {
  if (shutdown_in_progress) return;
  shutdown_in_progress=1; // tells net_threads to finish their work
  ev_break(main_loop, EVBREAK_ONE); // this stops accepting new connections

  // wake up workers
  pthread_mutex_lock(&job_queue_mux);
  pthread_cond_broadcast(&job_queue_cond);
  pthread_mutex_unlock(&job_queue_mux);

  int i;
  for (i=0; i<N_NET_THREADS; i++) {
    ev_async_send(net_threads[i].loop, &net_threads[i].watch_shutdown);
  }
  alarm(5); // make sure we terminate via SIGALRM if some connections do not close in 5 seconds
}

static void sigterm_cb(struct ev_loop *loop, struct ev_signal *w, int revents) {
  nxweb_log_error("SIGTERM/SIGINT(%d) received", w->signum);
  do_shutdown();
}

static void on_sigalrm(int sig) {
  nxweb_log_error("SIGALRM received. Exiting");
  exit(EXIT_SUCCESS);
}

void _nxweb_main() {
  _nxweb_register_printf_extensions();

  struct ev_loop *loop=EV_DEFAULT;
  int i;

  pid_t pid=getpid();

  main_loop=loop;

  nxweb_log_error("*** NXWEB startup: pid=%d port=%d ev_backend=%x N_NET_THREADS=%d N_WORKER_THREADS=%d"
                  " short=%d int=%d long=%d size_t=%d conn=%d req=%d nxweb_accept=%d",
                  (int)pid, NXWEB_LISTEN_PORT, ev_backend(loop), N_NET_THREADS, N_WORKER_THREADS,
                  (int)sizeof(short), (int)sizeof(int), (int)sizeof(long), (int)sizeof(size_t),
                  (int)sizeof(nxweb_connection), (int)sizeof(nxweb_request), (int)sizeof(nxweb_accept));

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

  nxweb_accept_queue_init(&accept_queue);
  pthread_mutex_init(&accept_queue_mux, 0);

  for (i=0; i<N_NET_THREADS; i++) {
    net_threads[i].loop=ev_loop_new(EVFLAG_AUTO);

    ev_async_init(&net_threads[i].watch_shutdown, net_thread_shutdown_cb);
    ev_async_start(net_threads[i].loop, &net_threads[i].watch_shutdown);
    ev_async_init(&net_threads[i].watch_accept, net_thread_accept_cb);
    ev_async_start(net_threads[i].loop, &net_threads[i].watch_accept);

    pthread_create(&net_threads[i].thread_id, 0, ev_thread_main, &net_threads[i]);
  }

  main_thread_id=pthread_self();

  nxweb_job_queue_init(&job_queue);
  pthread_mutex_init(&job_queue_mux, 0);
  pthread_cond_init(&job_queue_cond, 0);
  for (i=0; i<N_WORKER_THREADS; i++) {
    pthread_create(&worker_threads[i], 0, worker_thread_main, 0);
  }


//  signal(SIGTERM, on_sigterm);
//  signal(SIGINT, on_sigterm);
  signal(SIGALRM, on_sigalrm);

  ev_signal watch_sigterm;
  ev_signal_init(&watch_sigterm, sigterm_cb, SIGTERM);
  ev_signal_start(loop, &watch_sigterm);

  ev_signal watch_sigint;
  ev_signal_init(&watch_sigint, sigterm_cb, SIGINT);
  ev_signal_start(loop, &watch_sigint);

  // Unblock signals for the main thread;
  // other threads have inherited sigmask we set earlier
  sigdelset(&set, SIGPIPE); // except SIGPIPE
  if (pthread_sigmask(SIG_UNBLOCK, &set, NULL)) {
    nxweb_log_error("Can't unset pthread_sigmask");
    exit(EXIT_FAILURE);
  }

  int listen_fd=_nxweb_bind_socket(NXWEB_LISTEN_PORT);
  if (listen_fd==-1) {
    // simulate succesful exit (error have been logged)
    // otherwise launcher will keep trying
    return;
  }
  ev_io watch_accept;
  ev_io_init(&watch_accept, main_accept_cb, listen_fd, EV_READ);
  ev_io_start(loop, &watch_accept);

  FILE* f=fopen(NXWEB_PID_FILE, "w");
  if (f) {
    fprintf(f, "%d", (int)pid);
    fclose(f);
  }

  const nxweb_module* const * module=nxweb_modules;
  while (*module) {
    if ((*module)->server_startup_callback) {
      (*module)->server_startup_callback();
    }
    module++;
  }

  ev_run(loop, 0);

  close(listen_fd);

  for (i=0; i<N_NET_THREADS; i++) {
    pthread_join(net_threads[i].thread_id, NULL);
  }

  for (i=0; i<N_WORKER_THREADS; i++) {
    pthread_join(worker_threads[i], NULL);
  }
}
