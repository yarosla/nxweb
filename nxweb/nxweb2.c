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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "nxweb_internal.h"


static void on_keep_alive(nxe_loop* loop, void* _evt) {
  nxe_event* evt=_evt;
  nxweb_connection* conn=NXE_EVENT_DATA(evt);

  nxweb_log_error("[%p] keep-alive socket closed", conn);
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
  if (events & NXE_ERROR) {
    nxweb_log_error("on_error! %d", events);
    _nxweb_close_bad_socket(evt->fd);
  }
  else {
    //nxweb_log_error("on_error %d", events);
    _nxweb_close_bad_socket(evt->fd);
  }
  nxe_delete_event(loop, evt);
}

static void stop_receiving_request(nxe_loop* loop, nxe_event* evt, nxweb_connection* conn) {
  nxe_unset_timer(loop, &conn->timer_read);
  evt->read_status&=~NXE_WANT;
  if (!conn->req.content_length || conn->req.content_received < conn->req.content_length) conn->req.request_body=0;
}

static void start_sending_response(nxe_loop* loop, nxe_event* evt, nxweb_connection* conn) {
  _nxweb_finalize_response_writing_state(&conn->req);

  if (!conn->req.out_headers) {
    _nxweb_prepare_response_headers(&conn->req);
  }

  nxe_set_timer(loop, &conn->timer_write, NXWEB_WRITE_TIMEOUT);
  conn->cstate=NXWEB_CS_SENDING_HEADERS;
  if (!conn->req.sending_100_continue) {
    // if we are in process of sending 100-continue these have already been activated
    evt->write_status=NXE_CAN_AND_WANT;
    evt->write_ptr=conn->req.out_headers;
    evt->write_size=strlen(conn->req.out_headers);
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
    if ((!(flags&NXWEB_HANDLE_GET) || (!!strcasecmp(req->method, "GET") && !!strcasecmp(req->method, "HEAD")))
      && (!(flags&NXWEB_HANDLE_POST) || !!strcasecmp(req->method, "POST"))) {
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
      //ev_async_start(loop, &conn->watch_async_data_ready);
      // hand over to worker thread
      nxweb_job job={conn};
      pthread_mutex_lock(&conn->tdata->job_queue_mux);
      if (!nxweb_job_queue_push(&conn->tdata->job_queue, &job)) {
        pthread_cond_signal(&conn->tdata->job_queue_cond);
        pthread_mutex_unlock(&conn->tdata->job_queue_mux);
      }
      else { // queue full
        pthread_mutex_unlock(&conn->tdata->job_queue_mux);
        nxweb_send_http_error(req, 503, "Service Unavailable");
        start_sending_response(loop, NXWEB_CONNECTION_EVENT(conn), conn);
      }
    }
    break;
  }
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
  nxb_unfinish_obj(&conn->iobuf);
  stop_receiving_request(loop, evt, conn);
  conn->cstate=NXWEB_CS_ERROR;
  conn->req.keep_alive=0;
  nxweb_send_http_error(&conn->req, 408, "Request Timeout");
  start_sending_response(loop, evt, conn);

//  nxweb_log_error("[%p] read timeout socket closed", conn);
//  nxe_stop(loop, evt);
//  _nxweb_close_bad_socket(evt->fd);
//  nxe_delete_event(loop, evt);
}

static const char* response_100_continue = "HTTP/1.1 100 Continue\r\n\r\n";

static const char* response = "HTTP/1.1 200 OK\r\n"
                              "Connection: keep-alive\r\n"
                              "Content-Length: 20\r\n"
                              "Content-Type: text/plain\r\n"
                              "\r\n"
                              "<p>Hello, world!</p>";

static void on_read(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;
  nxweb_request* req=&conn->req;

  if (conn->cstate==NXWEB_CS_WAITING_FOR_REQUEST) {
    nxe_unset_timer(loop, &conn->timer_keep_alive);
    nxe_set_timer(loop, &conn->timer_read, NXWEB_READ_TIMEOUT);
    conn->cstate=NXWEB_CS_RECEIVING_HEADERS;
  }

  if (conn->cstate==NXWEB_CS_RECEIVING_HEADERS) {
    char* read_buf=nxb_get_unfinished(&conn->iobuf, 0);
    int bytes_received=evt->read_ptr - read_buf;
    nxb_resize_fast(&conn->iobuf, bytes_received);
    *evt->read_ptr='\0';
    char* end_of_headers;
    if ((end_of_headers=strstr(read_buf, "\r\n\r\n"))) {
      nxb_blank_fast(&conn->iobuf, 1); // null-terminator
      nxb_finish_obj(&conn->iobuf);

      //nxweb_log_error("[%p] request received", conn);
      memset(req, 0, sizeof(nxweb_request));

      ///////////////
      stop_receiving_request(loop, evt, conn);
      req->keep_alive=1;
      req->out_headers=response;
      start_sending_response(loop, evt, conn);
      return;
      ///////////////

      if (_nxweb_parse_http_request(req, read_buf, end_of_headers, bytes_received)) {
        // bad request
        stop_receiving_request(loop, evt, conn);
        conn->cstate=NXWEB_CS_ERROR;
        req->keep_alive=0;
        nxweb_send_http_error(req, 400, "Bad Request");
        start_sending_response(loop, evt, conn);
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
        nxb_unfinish_obj(&conn->iobuf);
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
      nxe_unset_timer(loop, &conn->timer_read);
      nxe_set_timer(loop, &conn->timer_read, NXWEB_READ_BODY_TIMEOUT);

      evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
      evt->read_size--; // for null-term

      if (req->expect_100_continue && !req->content_received) {
        // send 100-continue
        req->sending_100_continue=1;
        evt->write_status=NXE_CAN_AND_WANT;
        evt->write_ptr=response_100_continue;
        evt->write_size=strlen(response_100_continue);
        // do not stop reading
      }
    }
    else {
      if (events & NXE_READ_FULL) {
        nxb_unfinish_obj(&conn->iobuf);
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
      req->request_body=nxb_finish_obj(&conn->iobuf);
      if (req->chunked_request) _nxweb_decode_chunked_request(req);
      stop_receiving_request(loop, evt, conn);
      conn->cstate=NXWEB_CS_HANDLING;
      dispatch_request(loop, conn);
      return;
    }
    if (req->content_received > REQUEST_CONTENT_SIZE_LIMIT) {
      nxb_unfinish_obj(&conn->iobuf);
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

  nxe_unset_timer(loop, &conn->timer_write);

  if (events & NXE_WRITE_FULL) {
    if (req->sending_100_continue) {
      req->sending_100_continue=0;
      if (conn->cstate==NXWEB_CS_SENDING_HEADERS) {
        evt->write_ptr=req->out_headers;
        evt->write_size=strlen(req->out_headers);
        nxe_set_timer(loop, &conn->timer_write, NXWEB_WRITE_TIMEOUT);
      }
      else {
        evt->write_status&=~NXE_WANT;
      }
      return;
    }
    else if (conn->cstate==NXWEB_CS_SENDING_HEADERS) {
      if (req->out_body_length && !req->head_method) {
        conn->cstate=NXWEB_CS_SENDING_BODY;
        if (req->sendfile_fd) {
          evt->send_fd=req->sendfile_fd;
          evt->send_offset=req->sendfile_offset;
          evt->send_end=req->sendfile_end;
          evt->write_ptr=0;
          evt->write_size=0;
        }
        else {
          evt->write_ptr=req->out_body;
          evt->write_size=req->out_body_length;
          evt->send_fd=0;
        }
        nxe_set_timer(loop, &conn->timer_write, NXWEB_WRITE_BODY_TIMEOUT);
        return;
      }
    }
    // rearm connection for new request
    evt->write_status&=~NXE_WANT;
    conn->request_count++;
    nxb_empty(&conn->iobuf);
    conn->cstate=NXWEB_CS_WAITING_FOR_REQUEST;
    evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
    evt->read_size--; // for null-terminator
    evt->read_status=NXE_CAN_AND_WANT;
    nxe_set_timer(loop, &conn->timer_keep_alive, NXWEB_KEEP_ALIVE_TIMEOUT);
    if (!req->keep_alive) {
      shutdown(evt->fd, SHUT_RDWR);
    }
  }
  else { // continue writing
    nxe_set_timer(loop, &conn->timer_write, conn->cstate==NXWEB_CS_SENDING_BODY? NXWEB_WRITE_BODY_TIMEOUT : NXWEB_WRITE_TIMEOUT);
  }
}

static void on_new_connection(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;

  //nxweb_log_error("[%p] new conn", conn);

  conn->timer_read.callback=on_read_timeout;
  conn->timer_read.data=evt;

  conn->timer_write.callback=on_write_timeout;
  conn->timer_write.data=evt;

  conn->timer_keep_alive.callback=on_keep_alive;
  conn->timer_keep_alive.data=evt;
  nxe_set_timer(loop, &conn->timer_keep_alive, NXWEB_KEEP_ALIVE_TIMEOUT);

  nxb_init(&conn->iobuf, sizeof(conn->iobuf)+sizeof(conn->buf));

  evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
  evt->read_status=NXE_CAN_AND_WANT;
}

static void on_delete_connection(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;
  nxe_unset_timer(loop, &conn->timer_keep_alive);
  nxe_unset_timer(loop, &conn->timer_read);
  nxe_unset_timer(loop, &conn->timer_write);
  if (conn->req.worker_notify) nxe_remove_event(loop, conn->req.worker_notify);
  nxb_empty(&conn->iobuf);
}

static void on_accept(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  int client_fd;
  struct sockaddr_in client_addr;
  socklen_t client_len=sizeof(client_addr);
  while ((client_fd=accept(evt->fd, (struct sockaddr *)&client_addr, &client_len))!=-1) {
    if (_nxweb_set_non_block(client_fd) || _nxweb_setup_client_socket(client_fd)) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("failed to setup client socket");
      continue;
    }
    nxe_event* cevt=nxe_new_event_fd(loop, NXE_CLASS_SOCKET, client_fd);
    nxweb_connection* conn=NXE_EVENT_DATA(cevt);
    inet_ntop(AF_INET, &client_addr.sin_addr, conn->remote_addr, sizeof(conn->remote_addr));
    nxe_start(loop, cevt);
  }
  { // log error
    if (errno==EAGAIN) {
      evt->read_status&=~NXE_CAN;
    }
    else {
      char buf[1024];
      strerror_r(errno, buf, sizeof(buf));
      nxweb_log_error("accept() returned -1: errno=%d %s", errno, buf);
    }
  }
}

static void on_accept_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_log_error("on_accept_error");
}

static int listen_fd;

static void* net_thread_main(void* pdata) {
  nxe_loop* loop=nxe_create(sizeof(nxweb_connection), 128);
  nxe_event* evt;

  evt=nxe_new_event_fd(loop, NXE_CLASS_LISTEN, listen_fd);
  evt->read_status=NXE_CAN_AND_WANT;

  nxe_start(loop, evt);

  nxe_run(loop);
  return 0;
}

#define NTHREADS 4

nxe_event_class nxe_event_classes[]={
  {.on_read=on_accept, .on_error=on_accept_error},
  {.on_read=on_read, .on_write=on_write, .on_error=on_error,
   .on_new=on_new_connection, .on_delete=on_delete_connection}
};

/*
static void test_nx_buffer() {
  int i;
  nxb_buffer* nxb=nxb_create(111);
  for (i=0; i<20; i++) nxb_append(nxb, "test", 4);
  void* p1=nxb_alloc_obj(nxb, 64);
  void* p2=nxb_finish_obj(nxb);
  for (i=0; i<2000; i++) nxb_append(nxb, "TEST", 4);
  void* p3=nxb_alloc_obj(nxb, 6400);
  void* p4=nxb_finish_obj(nxb);
  nxb_destroy(nxb);
}
*/

// Signal server to shutdown. Async function. Can be called from worker threads.
void nxweb_shutdown() {
}

void _nxweb_main() {
  _nxweb_register_printf_extensions();

  nxweb_log_error("loop[%d] evt[%d] %ld\n", (int)sizeof(nxe_loop), (int)sizeof(nxe_event), nxe_get_time_usec());

  listen_fd=_nxweb_bind_socket(8777);

  pthread_t tids[NTHREADS];
  int i;
  for (i=0; i<NTHREADS; i++) {
    pthread_create(&tids[i], 0, net_thread_main, 0);
  }
  //nxweb_log_error("started at %ld [%d]\n", nxe_get_time_usec(), (int)sizeof(nxe_time_t));
  for (i=0; i<NTHREADS; i++) {
    pthread_join(tids[i], 0);
  }

  close(listen_fd);
}
