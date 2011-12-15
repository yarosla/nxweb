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

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

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

#include "nxweb_internal.h"

static pthread_t main_thread_id=0;

static volatile int shutdown_in_progress=0;
static volatile int num_connections=0;

static int listen_fd;
static int next_net_thread_idx=0;
static nxe_event listen_evt;
static nxe_loop* main_loop;
static nxweb_net_thread net_threads[N_NET_THREADS];


static void on_keep_alive(nxe_loop* loop, void* _evt) {
  nxe_event* evt=_evt;
  nxweb_connection* conn=NXE_EVENT_DATA(evt);

  nxweb_log_error("[%p] keep-alive socket closed; rc=%d", conn, conn->request_count);
  nxe_stop(loop, evt);
  _nxweb_close_bad_socket(evt->fd);
  nxe_delete_event(loop, evt);
}

static void on_write_timeout(nxe_loop* loop, void* _evt) {
  nxe_event* evt=_evt;
  nxweb_connection* conn=NXE_EVENT_DATA(evt);

  nxweb_log_error("[%p] write timeout socket closed", conn);
  nxe_stop(loop, evt);
  _nxweb_close_bad_socket(evt->fd);
  nxe_delete_event(loop, evt);
}

static void on_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
//  nxweb_log_error("on_error");
  nxe_stop(loop, evt);
  if (events & NXE_CLOSE) {
    //nxweb_log_error("on_error [CLOSE]");
    _nxweb_close_good_socket(evt->fd);
  }
  else if (events & NXE_ERROR) {
    nxweb_log_error("on_error %d, %d", events, evt->last_errno);
    _nxweb_close_bad_socket(evt->fd);
  }
  else if (events & NXE_WRITTEN_NONE) {
    nxweb_log_error("on_error [WRITTEN_NONE] %d", evt->last_errno);
    _nxweb_close_bad_socket(evt->fd);
  }
  else if (events & NXE_HUP) {
    nxweb_log_error("on_error [HUP]");
    _nxweb_close_bad_socket(evt->fd);
  }
  else if (events & NXE_RDHUP) {
    nxweb_log_error("on_error [RDHUP]");
    _nxweb_close_bad_socket(evt->fd);
  }
  else if (events & NXE_STALE) {
    nxweb_log_error("on_error [STALE]");
    _nxweb_close_bad_socket(evt->fd);
  }
  else {
    //nxweb_log_error("on_error %d", events);
    _nxweb_close_bad_socket(evt->fd);
  }
  nxe_delete_event(loop, evt);
}

static void stop_receiving_request(nxe_loop* loop, nxe_event* evt, nxweb_connection* conn) {
  nxe_unset_timer(loop, NXE_TIMER_READ, &conn->timer_read);
  nxe_mark_read_down(loop, evt, NXE_WANT);
  if (!conn->req.content_length || conn->req.content_received < conn->req.content_length) conn->req.request_body=0;
}

static void start_sending_response(nxe_loop* loop, nxe_event* evt, nxweb_connection* conn) {
  nxweb_request* req=&conn->req;

  _nxweb_finalize_response(req);

  if (!req->out_headers) {
    _nxweb_prepare_response_headers(&conn->req);
  }

  nxe_set_timer(loop, NXE_TIMER_WRITE, &conn->timer_write);
  conn->cstate=NXWEB_CS_SENDING_HEADERS;
  if (!req->sending_100_continue) {
    // if we are in process of sending 100-continue these have already been activated
    nxe_mark_write_up(loop, evt, NXE_WANT);
    evt->write_ptr=req->out_headers;
    evt->write_size=strlen(req->out_headers);
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

static int prepare_request_for_handler(nxe_loop *loop, nxweb_request* req) {
  const nxweb_uri_handler* handler=req->handler;
  unsigned flags=handler->flags;
  if ((flags&_NXWEB_HANDLE_MASK) && !(flags&NXWEB_HANDLE_ANY)) {
    if ((!(flags&NXWEB_HANDLE_GET) || !req->get_method)
      && (!(flags&NXWEB_HANDLE_POST) || !req->post_method)) {
        nxweb_send_http_error(req, 405, "Method Not Allowed");
        start_sending_response(loop, NXWEB_CONNECTION_EVENT(NXWEB_REQUEST_CONNECTION(req)), NXWEB_REQUEST_CONNECTION(req));
        return -1;
    }
  }
  if (flags&NXWEB_PARSE_PARAMETERS) nxweb_parse_request_parameters(req, 0);
  if (flags&NXWEB_PARSE_COOKIES) nxweb_parse_request_cookies(req);
  return 0;
}

static void dispatch_request(nxe_loop *loop, nxweb_connection *conn) {
  nxweb_request* req=&conn->req;
  while (1) {
    const nxweb_uri_handler* handler=find_handler(req);
    assert(handler!=0); // default handler never returns NXWEB_NEXT
    if (prepare_request_for_handler(loop, req)) return;
    if (handler->flags&NXWEB_INPROCESS) {
      req->handler_result=handler->callback(NXWEB_PH_CONTENT, req);
      if (req->handler_result==NXWEB_NEXT) continue;
      start_sending_response(loop, NXWEB_CONNECTION_EVENT(conn), conn);
    }
    else {
      // go async
      if (!conn->worker_evt.evt.active) {
        nxe_async_init(&conn->worker_evt);
        nxe_add_event(loop, NXE_CLASS_WORKER_JOB_DONE, (nxe_event*)&conn->worker_evt);
        nxe_mark_stale_up(loop, (nxe_event*)&conn->worker_evt, NXE_WANT);
        nxe_start(loop, (nxe_event*)&conn->worker_evt);
      }
      // hand over to worker thread
      nxweb_job job={conn};
      nxweb_net_thread* tdata=(nxweb_net_thread*)loop->user_data;
      pthread_mutex_lock(&tdata->job_queue_mux);
      if (!nxweb_job_queue_push(&tdata->job_queue, &job)) {
        pthread_cond_signal(&tdata->job_queue_cond);
        pthread_mutex_unlock(&tdata->job_queue_mux);
      }
      else { // queue full
        pthread_mutex_unlock(&tdata->job_queue_mux);
        nxweb_send_http_error(req, 503, "Service Unavailable");
        start_sending_response(loop, NXWEB_CONNECTION_EVENT(conn), conn);
      }
    }
    break;
  }
}

static void on_worker_job_done(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {

  nxe_async_rearm((nxe_event_async*)evt);

  nxweb_connection *conn=(nxweb_connection*)(((char*)evt)-offsetof(nxweb_connection, worker_evt));
  nxweb_request* req=&conn->req;

  if (req->handler_result==NXWEB_NEXT) {
    // dispatch again
    dispatch_request(loop, conn);
    return;
  }

  start_sending_response(loop, NXWEB_CONNECTION_EVENT(conn), conn);
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

static void on_read_timeout(nxe_loop* loop, void* _evt) {
  nxe_event* evt=_evt;
  nxweb_connection* conn=NXE_EVENT_DATA(evt);

  // request timeout
  nxb_unfinish_stream(&conn->iobuf);
  stop_receiving_request(loop, evt, conn);
  conn->cstate=NXWEB_CS_ERROR;
  conn->req.keep_alive=0;
  nxweb_send_http_error(&conn->req, 408, "Request Timeout");
  start_sending_response(loop, evt, conn);
}

static const char* response_100_continue = "HTTP/1.1 100 Continue\r\n\r\n";

static void on_read(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;
  nxweb_request* req=&conn->req;

  if (conn->cstate==NXWEB_CS_WAITING_FOR_REQUEST) {
    nxe_unset_timer(loop, NXE_TIMER_KEEP_ALIVE, &conn->timer_keep_alive);
    nxe_set_timer(loop, NXE_TIMER_READ, &conn->timer_read);
    conn->cstate=NXWEB_CS_RECEIVING_HEADERS;
  }

  if (conn->cstate==NXWEB_CS_RECEIVING_HEADERS) {
    char* read_buf=nxb_get_unfinished(&conn->iobuf, 0);
    int bytes_received=evt->read_ptr - read_buf;
    nxb_resize_fast(&conn->iobuf, bytes_received);
    *evt->read_ptr='\0';
    char* end_of_headers;
    if ((end_of_headers=_nxweb_find_end_of_http_headers(read_buf, bytes_received))) {
    //if ((end_of_headers=strstr(read_buf, "\r\n\r\n"))) {
      nxb_blank_fast(&conn->iobuf, 1); // null-terminator
      nxb_finish_stream(&conn->iobuf);

      //nxweb_log_error("[%p] request received", conn);
      memset(req, 0, sizeof(nxweb_request));

      if (_nxweb_parse_http_request(req, read_buf, end_of_headers, bytes_received)) {
        // bad request
        stop_receiving_request(loop, evt, conn);
        conn->cstate=NXWEB_CS_ERROR;
        req->keep_alive=0;
        nxweb_send_http_error(req, 400, "Bad Request");
        start_sending_response(loop, evt, conn);
        return;
      }

      if (is_request_body_complete(req, req->request_body)) {
        // receiving request complete
        if (req->chunked_request) _nxweb_decode_chunked_request(req);
        stop_receiving_request(loop, evt, conn);
        conn->cstate=NXWEB_CS_HANDLING;
        dispatch_request(loop, conn);
        return;
      }

      // so not all content received with headers

      if (req->content_length > REQUEST_CONTENT_SIZE_LIMIT) {
        nxb_unfinish_stream(&conn->iobuf);
        stop_receiving_request(loop, evt, conn);
        conn->cstate=NXWEB_CS_ERROR;
        conn->req.keep_alive=0;
        nxweb_send_http_error(&conn->req, 413, "Request Entity Too Large");
        start_sending_response(loop, evt, conn);
        return;
      }

      if (req->content_length>0) {
        // body size specified; pre-allocate buffer for the content
        nxb_make_room(&conn->iobuf, req->content_length+1); // plus null-terminator char
      }
      else {
        nxb_make_room(&conn->iobuf, req->content_received+128); // have at least 128 bytes room for the start
      }

      if (req->content_received>0) {
        // copy what we have already received with headers
        nxb_append(&conn->iobuf, req->request_body, req->content_received);
        req->request_body=nxb_get_unfinished(&conn->iobuf, 0);
      }

      // continue receiving request body
      conn->cstate=NXWEB_CS_RECEIVING_BODY;
      nxe_unset_timer(loop, NXE_TIMER_READ, &conn->timer_read);
      nxe_set_timer(loop, NXE_TIMER_READ, &conn->timer_read);

      evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
      evt->read_size--; // for null-term

      if (req->expect_100_continue && !req->content_received) {
        // send 100-continue
        req->sending_100_continue=1;
        nxe_mark_write_up(loop, evt, NXE_WANT);
        evt->write_ptr=response_100_continue;
        evt->write_size=strlen(response_100_continue);
        // do not stop reading
      }
    }
    else {
      if (events & NXE_READ_FULL) {
        nxb_unfinish_stream(&conn->iobuf);
        stop_receiving_request(loop, evt, conn);
        conn->cstate=NXWEB_CS_ERROR;
        req->keep_alive=0;
        nxweb_send_http_error(req, 400, "Bad Request");
        start_sending_response(loop, evt, conn);
        return;
      }
    }
  }
  else if (conn->cstate==NXWEB_CS_RECEIVING_BODY) {
    char* read_buf=nxb_get_unfinished(&conn->iobuf, 0);
    req->content_received=evt->read_ptr - read_buf;
    nxb_resize_fast(&conn->iobuf, req->content_received);
    *evt->read_ptr='\0';
    if (is_request_body_complete(req, read_buf)) {
      // receiving request complete
      // append null-terminator and close input buffer
      nxb_append_char(&conn->iobuf, '\0');
      req->request_body=nxb_finish_stream(&conn->iobuf);
      if (req->chunked_request) _nxweb_decode_chunked_request(req);
      stop_receiving_request(loop, evt, conn);
      conn->cstate=NXWEB_CS_HANDLING;
      dispatch_request(loop, conn);
      return;
    }
    if (req->content_received > REQUEST_CONTENT_SIZE_LIMIT) {
      nxb_unfinish_stream(&conn->iobuf);
      stop_receiving_request(loop, evt, conn);
      conn->cstate=NXWEB_CS_ERROR;
      req->keep_alive=0;
      nxweb_send_http_error(req, 413, "Request Entity Too Large");
      start_sending_response(loop, evt, conn);
      return;
    }
    if (req->content_length<0) { // chunked encoding
      nxb_make_room(&conn->iobuf, 128); // add at least 128 bytes room to continue
      evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
      evt->read_size--; // for null-term
    }
    nxweb_log_error("partial receive request body (all ok)"); // for debug only
  }
}

static void on_write(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;
  nxweb_request* req=&conn->req;

  nxe_unset_timer(loop, NXE_TIMER_WRITE, &conn->timer_write);

  if (events & NXE_WRITE_FULL) {
    if (req->sending_100_continue) {
      req->sending_100_continue=0;
      if (conn->cstate==NXWEB_CS_SENDING_HEADERS) {
        evt->write_ptr=req->out_headers;
        evt->write_size=strlen(req->out_headers);
        nxe_set_timer(loop, NXE_TIMER_WRITE, &conn->timer_write);
      }
      else {
        nxe_mark_write_down(loop, evt, NXE_WANT);
      }
      return;
    }
    else if (conn->cstate==NXWEB_CS_SENDING_HEADERS) {
      if (req->out_body_length && !req->head_method) {
        conn->cstate=NXWEB_CS_SENDING_BODY;
        if (req->sendfile_fd) {
          evt->write_ptr=0;
          evt->send_fd=req->sendfile_fd;
          evt->send_offset=req->sendfile_offset;
          evt->write_size=req->out_body_length;
        }
        else {
          evt->write_ptr=req->out_body;
          evt->write_size=req->out_body_length;
          evt->send_fd=0;
        }
        nxe_set_timer(loop, NXE_TIMER_WRITE, &conn->timer_write);
        return;
      }
    }
    // rearm connection for new request
    nxe_mark_write_down(loop, evt, NXE_WANT);
    if (req->sendfile_fd) {
      close(req->sendfile_fd);
      req->sendfile_fd=0;
      evt->send_fd=0;
    }
    nxb_empty(&conn->iobuf);
    conn->request_count++;
    conn->cstate=NXWEB_CS_WAITING_FOR_REQUEST;
    evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
    evt->read_size--; // for null-terminator
    nxe_mark_read_up(loop, evt, NXE_WANT);
    nxe_set_timer(loop, NXE_TIMER_KEEP_ALIVE, &conn->timer_keep_alive);

    //nxweb_log_error("rearm conn %p", conn);

    if (!req->keep_alive) {
      shutdown(evt->fd, SHUT_RDWR);
    }
  }
  else { // continue writing
    nxe_set_timer(loop, NXE_TIMER_WRITE, &conn->timer_write);
  }
}

static void on_new_connection(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;

//  __sync_add_and_fetch(&num_connections, 1);

  //nxweb_log_error("new conn %p", conn);

  conn->timer_read.callback=on_read_timeout;
  conn->timer_read.data=evt;

  conn->timer_write.callback=on_write_timeout;
  conn->timer_write.data=evt;

  conn->timer_keep_alive.callback=on_keep_alive;
  conn->timer_keep_alive.data=evt;
  nxe_set_timer(loop, NXE_TIMER_KEEP_ALIVE, &conn->timer_keep_alive);

  nxb_init(&conn->iobuf, sizeof(conn->iobuf)+sizeof(conn->buf));

  evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
  nxe_mark_read_up(loop, evt, NXE_CAN_AND_WANT);
}

static void on_delete_connection(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;

  //nxweb_log_error("del conn %p", conn);

  nxe_unset_timer(loop, NXE_TIMER_KEEP_ALIVE, &conn->timer_keep_alive);
  nxe_unset_timer(loop, NXE_TIMER_READ, &conn->timer_read);
  nxe_unset_timer(loop, NXE_TIMER_WRITE, &conn->timer_write);
  if (conn->worker_evt.evt.active) {
    nxe_remove_event(loop, (nxe_event*)&conn->worker_evt);
    nxe_async_finalize(&conn->worker_evt);
  }
  if (evt->send_fd) {
    close(evt->send_fd);
    evt->send_fd=0;
  }
  nxb_empty(&conn->iobuf);

//  int connections_left=__sync_sub_and_fetch(&num_connections, 1);
//  assert(connections_left>=0);
//  if (connections_left==0) { // for debug only
//    nxweb_log_error("all connections closed");
//  }
}

static void on_accept(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_accept accept_msg;
  int client_fd;
  struct sockaddr_in client_addr;
  socklen_t client_len=sizeof(client_addr);
  while (!shutdown_in_progress && (client_fd=accept(evt->fd, (struct sockaddr *)&client_addr, &client_len))!=-1) {
    if (_nxweb_set_non_block(client_fd) || _nxweb_setup_client_socket(client_fd)) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("failed to setup client socket");
      continue;
    }
    accept_msg.client_fd=client_fd;
    memcpy(&accept_msg.sin_addr, &client_addr.sin_addr, sizeof(accept_msg.sin_addr));
    nxweb_net_thread* tdata=&net_threads[next_net_thread_idx];
    if (nxweb_accept_queue_push(tdata->accept_queue, &accept_msg)) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("accept queue full; dropping connection");
      continue;
    }
    nxe_async_send(&tdata->accept_evt);
    next_net_thread_idx=(next_net_thread_idx+1)%N_NET_THREADS; // round-robin
  }
  { // log error
    if (errno==EAGAIN) {
      nxe_mark_read_down(loop, evt, NXE_CAN);
    }
    else {
      char buf[1024];
      strerror_r(errno, buf, sizeof(buf));
      nxweb_log_error("accept() returned -1: errno=%d %s", errno, buf);
    }
  }
}

static void on_net_thread_accept(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_net_thread* tdata=((nxweb_net_thread*)(((char*)evt)-offsetof(nxweb_net_thread, accept_evt)));

  nxweb_accept a;

  while (!nxweb_accept_queue_pop(tdata->accept_queue, &a)) {
    nxe_event* cevt=nxe_new_event_fd(loop, NXE_CLASS_SOCKET, a.client_fd);
    nxe_mark_stale_up(loop, cevt, NXE_WANT);
    nxweb_connection* conn=NXE_EVENT_DATA(cevt);
    inet_ntop(AF_INET, &a.sin_addr, conn->remote_addr, sizeof(conn->remote_addr));
    nxe_start(loop, cevt);
  }

  if (shutdown_in_progress) {
    // remove accept_evt
    nxe_remove_event(loop, evt);
    nxe_async_finalize((nxe_event_async*)evt);
  }
  else {
    nxe_async_rearm((nxe_event_async*)evt);
  }
}

static void on_net_thread_shutdown(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_net_thread* tdata=((nxweb_net_thread*)(((char*)evt)-offsetof(nxweb_net_thread, shutdown_evt)));

  nxe_remove_event(loop, evt);
  nxe_async_finalize((nxe_event_async*)evt);

  nxe_async_send(&tdata->accept_evt); // wake it up so it clears itself
}

static void on_accept_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_log_error("on_accept_error");
}

static void on_job_done_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  //nxweb_connection *conn=(nxweb_connection*)(((char*)evt)-offsetof(nxweb_connection, worker_evt));

  nxweb_log_error("on_job_done_error");
  if (events & NXE_STALE) {
    nxe_remove_event(loop, evt);
    nxe_async_finalize((nxe_event_async*)evt);
  }
}

static void on_shutdown_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_log_error("on_shutdown_error");
}

static void on_net_thread_accept_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_log_error("on_net_thread_accept_error");
}

static void* worker_thread_main(void* pdata) {
  nxweb_net_thread* tdata=(nxweb_net_thread*)pdata;
  nxweb_job job;
  nxweb_connection* conn;
  int result;

  while (!shutdown_in_progress) {
    pthread_mutex_lock(&tdata->job_queue_mux);
    while ((result=nxweb_job_queue_pop(&tdata->job_queue, &job)) && !shutdown_in_progress) {
      pthread_cond_wait(&tdata->job_queue_cond, &tdata->job_queue_mux);
    }
    pthread_mutex_unlock(&tdata->job_queue_mux);
    if (!result) {
      conn=job.conn;
      conn->req.handler_result=conn->req.handler->callback(NXWEB_PH_CONTENT, &conn->req);
      __sync_synchronize(); // full memory barrier
      nxe_async_send(&conn->worker_evt);
    }
  }

  nxweb_log_error("worker thread clean exit");
  return 0;
}

static void* net_thread_main(void* pdata) {
  nxweb_net_thread* tdata=(nxweb_net_thread*)pdata;

  nxweb_job_queue_init(&tdata->job_queue);
  pthread_mutex_init(&tdata->job_queue_mux, 0);
  pthread_cond_init(&tdata->job_queue_cond, 0);

  int i;
  for (i=0; i<N_WORKER_THREADS; i++) {
    pthread_create(&tdata->worker_threads[i], 0, worker_thread_main, tdata);
  }

  tdata->loop=nxe_create(sizeof(nxweb_connection), 256, NXWEB_STALE_EVENT_TIMEOUT);
  tdata->loop->user_data=tdata;

  tdata->accept_queue=nxweb_accept_queue_new(NXWEB_ACCEPT_QUEUE_SIZE);

  // setup timeouts
  nxe_set_timer_queue_timeout(tdata->loop, NXE_TIMER_KEEP_ALIVE, NXWEB_KEEP_ALIVE_TIMEOUT);
  nxe_set_timer_queue_timeout(tdata->loop, NXE_TIMER_READ, NXWEB_READ_TIMEOUT);
  nxe_set_timer_queue_timeout(tdata->loop, NXE_TIMER_WRITE, NXWEB_WRITE_TIMEOUT);

  nxe_async_init(&tdata->shutdown_evt);
  nxe_add_event(tdata->loop, NXE_CLASS_NET_THREAD_SHUTDOWN, (nxe_event*)&tdata->shutdown_evt);
  nxe_start(tdata->loop, (nxe_event*)&tdata->shutdown_evt);

  nxe_async_init(&tdata->accept_evt);
  nxe_add_event(tdata->loop, NXE_CLASS_NET_THREAD_ACCEPT, (nxe_event*)&tdata->accept_evt);
  nxe_start(tdata->loop, (nxe_event*)&tdata->accept_evt);

  nxe_run(tdata->loop);
  nxe_destroy(tdata->loop);
  free(tdata->accept_queue);
  nxweb_log_error("network thread clean exit");
  return 0;
}

nxe_event_class nxe_event_classes[]={
  // NXE_CLASS_LISTEN:
  {.on_read=on_accept, .on_error=on_accept_error},
  // NXE_CLASS_SOCKET:
  {.on_read=on_read, .on_write=on_write, .on_error=on_error,
   .on_new=on_new_connection, .on_delete=on_delete_connection, .log_stale=1},
  // NXE_CLASS_WORKER_JOB_DONE:
  {.on_read=on_worker_job_done, .on_error=on_job_done_error},
  // NXE_CLASS_NET_THREAD_ACCEPT:
  {.on_read=on_net_thread_accept, .on_error=on_net_thread_accept_error},
  // NXE_CLASS_NET_THREAD_SHUTDOWN:
  {.on_read=on_net_thread_shutdown, .on_error=on_shutdown_error}
};

// Signal server to shutdown. Async function. Can be called from worker threads.
void nxweb_shutdown() {
  pthread_kill(main_thread_id, SIGTERM);
}

static void on_sigterm(int sig) {
  nxweb_log_error("SIGTERM/SIGINT(%d) received", sig);
  if (shutdown_in_progress) return;
  shutdown_in_progress=1; // tells net_threads to finish their work
  unlink(NXWEB_PID_FILE);

  nxe_break(main_loop); // this is a little bit dirty; should modify main loop from callback

  int i;
  nxweb_net_thread* tdata;
  for (i=0, tdata=net_threads; i<N_NET_THREADS; i++, tdata++) {
    // wake up workers
    pthread_mutex_lock(&tdata->job_queue_mux);
    pthread_cond_broadcast(&tdata->job_queue_cond);
    pthread_mutex_unlock(&tdata->job_queue_mux);

    nxe_async_send(&tdata->shutdown_evt);
  }
  alarm(5); // make sure we terminate via SIGALRM if some connections do not close in 5 seconds
}

static void on_sigalrm(int sig) {
  nxweb_log_error("SIGALRM received. Exiting");
  exit(EXIT_SUCCESS);
}

/*
static void speed_test() {
  char buf[4096];
  nxweb_connection conn;
  nxweb_request* req=&conn.req;
  static char* test_request="GET /hello HTTP/1.1\r\n"
                            "User-Agent: curl/7.21.6 (x86_64-pc-linux-gnu) libcurl/7.21.6 OpenSSL/1.0.0e zlib/1.2.3.4 libidn/1.22 librtmp/2.3\r\n"
                            "Host: test.mforum.ru:8777\r\n"
                            "Accept: text/html\r\n\r\n";
  int req_len=strlen(test_request);
  char* end_of_headers=buf+req_len-4;
  int i;

  nxb_init(&conn.iobuf, sizeof(conn.iobuf)+sizeof(conn.buf));

  nxe_time_t t1=nxe_get_time_usec();
  for (i=1000000; i--;) {
    memcpy(buf, test_request, req_len+1);
    //end_of_headers=strstr(buf, "\r\n\r\n");
    end_of_headers=_nxweb_find_end_of_http_headers(buf, req_len);
    _nxweb_parse_http_request(req, buf, end_of_headers, req_len);
  }
  nxe_time_t t2=nxe_get_time_usec();
  printf("test complete in %ld us\n", (t2-t1));
}
*/

void _nxweb_main() {
  int i, j;

  _nxweb_register_printf_extensions();

  pid_t pid=getpid();
  main_thread_id=pthread_self();

  nxweb_log_error("*** NXWEB startup: pid=%d port=%d N_NET_THREADS=%d N_WORKER_THREADS=%d"
                  " short=%d int=%d long=%d size_t=%d evt=%d conn=%d req=%d",
                  (int)pid, NXWEB_LISTEN_PORT, N_NET_THREADS, N_WORKER_THREADS,
                  (int)sizeof(short), (int)sizeof(int), (int)sizeof(long), (int)sizeof(size_t),
                  (int)sizeof(nxe_event), (int)sizeof(nxweb_connection), (int)sizeof(nxweb_request));

  listen_fd=_nxweb_bind_socket(NXWEB_LISTEN_PORT);
  if (listen_fd==-1) {
    // simulate succesful exit (error have been logged)
    // otherwise launcher will keep trying
    return;
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

  nxweb_net_thread* tdata;
  for (i=0, tdata=net_threads; i<N_NET_THREADS; i++, tdata++) {
    pthread_create(&tdata->thread_id, 0, net_thread_main, tdata);
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

  nxe_loop* loop=nxe_create(sizeof(nxe_event), 4, 0);
  main_loop=loop;

  nxe_init_event(&listen_evt);
  nxe_add_event_fd(loop, NXE_CLASS_LISTEN, &listen_evt, listen_fd);
  nxe_mark_read_up(loop, &listen_evt, NXE_CAN_AND_WANT);
  nxe_start(loop, &listen_evt);

  nxe_run(loop);
  nxe_destroy(loop);
  close(listen_fd);

  for (i=0; i<N_NET_THREADS; i++) {
    pthread_join(net_threads[i].thread_id, NULL);
    for (j=0; j<N_WORKER_THREADS; j++) {
      pthread_join(net_threads[i].worker_threads[j], NULL);
    }
  }

  nxweb_log_error("end of _nxweb_main()");
}
