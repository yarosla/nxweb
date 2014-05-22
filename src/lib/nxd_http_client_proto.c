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
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>


static inline void start_receiving_response(nxd_http_client_proto* hcp, nxe_loop* loop) {

  nxweb_log_debug("http_client start_receiving_response");

  nxe_istream_unset_ready(&hcp->data_out);
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
  nxe_set_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
  nxe_ostream_set_ready(loop, &hcp->data_in);
  hcp->state=HCP_WAITING_FOR_RESPONSE;
}

static inline void wait_for_100_continue(nxd_http_client_proto* hcp, nxe_loop* loop) {
  nxe_istream_unset_ready(&hcp->data_out);
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
  nxe_set_timer(loop, NXWEB_TIMER_100CONTINUE, &hcp->timer_100_continue);
  nxe_ostream_set_ready(loop, &hcp->data_in);
  hcp->receiving_100_continue=1;
  hcp->state=HCP_WAITING_FOR_RESPONSE;
}

static inline void request_complete(nxd_http_client_proto* hcp, nxe_loop* loop) {

  nxweb_log_debug("http_client request_complete");

  nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_REQUEST_COMPLETE);
  if (hcp->queued_error_message.i) nxe_publish(&hcp->events_pub, hcp->queued_error_message);
  nxe_ostream_unset_ready(&hcp->data_in);
  nxe_istream_unset_ready(&hcp->data_out);
  nxe_unset_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
  /* not sure if we need this; nxd_http_proxy_pool_return() terminates non-keep-alive connections anyway (with RST)
  if (!hcp->resp.keep_alive) {
    nxe_ostream* os=hcp->data_out.pair;
    if (os && OSTREAM_CLASS(os)->shutdown) OSTREAM_CLASS(os)->shutdown(os);
  }
  */
  nxe_set_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hcp->timer_keep_alive);
  hcp->request_complete=1;
  hcp->state=HCP_IDLE;
  hcp->request_count++;
}

static void data_in_do_read(nxe_ostream* os, nxe_istream* is) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)os-offsetof(nxd_http_client_proto, data_in));
  nxe_loop* loop=os->super.loop;

  nxweb_log_debug("http_client data_in_do_read");

  if (hcp->state==HCP_WAITING_FOR_RESPONSE) {
    nxe_unset_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
    nxe_unset_timer(loop, NXWEB_TIMER_100CONTINUE, &hcp->timer_100_continue);
    nxe_set_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
    nxb_make_room(hcp->nxb, NXWEB_MAX_REQUEST_HEADERS_SIZE);
    hcp->state=HCP_RECEIVING_HEADERS;
  }

  if (hcp->state==HCP_RECEIVING_HEADERS) {
    int size;
    nxe_flags_t flags=0;
    void* ptr=nxb_get_room(hcp->nxb, &size);
    int bytes_received=ISTREAM_CLASS(is)->read(is, os, ptr, size, &flags);
    if (bytes_received) {
      nxb_blank_fast(hcp->nxb, bytes_received);
      int read_buf_size;
      char* read_buf=nxb_get_unfinished(hcp->nxb, &read_buf_size);
      char* end_of_headers;
      char* start_of_body;
      if ((end_of_headers=_nxweb_find_end_of_http_headers(read_buf, read_buf_size, &start_of_body))) {
        nxb_finish_stream(hcp->nxb, 0);
        memset(&hcp->resp, 0, sizeof(hcp->resp));
        hcp->resp.nxb=hcp->nxb;
        hcp->resp.cdstate.monitor_only=hcp->chunked_do_not_decode;
        if (_nxweb_parse_http_response(&hcp->resp, read_buf, end_of_headers)) {
          // bad response
          nxe_unset_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
          nxe_ostream_unset_ready(os);
          nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_BAD_RESPONSE);
          return;
        }
        if (hcp->receiving_100_continue) {
          hcp->receiving_100_continue=0;
          if (hcp->resp.status_code==100) {
            // back to sending request body
            nxe_ostream_unset_ready(&hcp->data_in);
            nxe_unset_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
            nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
            nxe_istream_set_ready(loop, &hcp->data_out);
            hcp->state=HCP_SENDING_BODY;
            return;
          }
          else {
            nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_NO_100CONTINUE);
          }
        }
        char* read_buf_end=read_buf+read_buf_size;
        if (start_of_body<read_buf_end) {
          hcp->first_body_chunk=start_of_body;
          hcp->first_body_chunk_end=read_buf_end;

          nxe_size_t first_body_chunk_size=hcp->first_body_chunk_end-hcp->first_body_chunk;
          if (hcp->resp.chunked_encoding) {
            int r=_nxweb_decode_chunked_stream(&hcp->resp.cdstate, hcp->first_body_chunk, &first_body_chunk_size);
            hcp->first_body_chunk_end=hcp->first_body_chunk+first_body_chunk_size;
            if (r<0) nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_RESPONSE_CHUNKED_ENCODING_ERROR);
            else if (r>0) hcp->response_body_complete=1;
          }
          else if (first_body_chunk_size >= hcp->resp.content_length && hcp->resp.content_length>=0) {
            hcp->response_body_complete=1;
          }
        }
        else {
          hcp->first_body_chunk=0;
          hcp->first_body_chunk_end=0;
        }
        nxe_ostream_unset_ready(os);
        nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_RESPONSE_RECEIVED);
        hcp->state=HCP_RECEIVING_BODY;
        nxe_istream_set_ready(loop, &hcp->resp_body_out);
        if (hcp->resp_body_out.pair) {
          nxe_ostream_set_ready(loop, os);
        }
      }
      else {
        if (read_buf_size>=NXWEB_MAX_REQUEST_HEADERS_SIZE) {
          // bad response (headers too large)
          nxe_unset_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
          nxe_ostream_unset_ready(os);
          nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_BAD_RESPONSE);
          return;
        }
      }
    }
  }
  else if (hcp->state==HCP_RECEIVING_BODY) {
    nxe_ostream* next_os=hcp->resp_body_out.pair;
    if (next_os) {
      if (next_os->ready) OSTREAM_CLASS(next_os)->do_read(next_os, &hcp->resp_body_out);
      if (!next_os->ready) {
        nxe_ostream_unset_ready(os);
        nxe_istream_set_ready(loop, &hcp->resp_body_out); // get notified when next_os becomes ready again
      }
    }
    else {
      nxweb_log_error("no connected device for hcp->resp_body_out");
      nxe_ostream_unset_ready(os);
    }
  }
}

static void data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)is-offsetof(nxd_http_client_proto, data_out));
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("http_client data_out_do_write");

  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);

  if (hcp->state==HCP_CONNECTING) {
    nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_CONNECTED);
    if (hcp->req) hcp->state=HCP_SENDING_HEADERS;
    else {
      hcp->state=HCP_IDLE;
      nxe_set_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hcp->timer_keep_alive);
      nxe_istream_unset_ready(is);
      return;
    }
  }

  nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);

  if (hcp->state==HCP_SENDING_HEADERS) {
    if (hcp->req_headers_ptr && *hcp->req_headers_ptr) {
      int size=strlen(hcp->req_headers_ptr);
      nxe_flags_t flags=0;
      int bytes_sent=OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)hcp->req_headers_ptr, size, &flags);
      hcp->req_headers_ptr+=bytes_sent;
      if (bytes_sent<size) return;
    }
    // all headers sent
    hcp->req_headers_ptr=0;
    if (!hcp->req->content_length) {
      // no request body => get response
      start_receiving_response(hcp, loop);
      return;
    }
    if (hcp->req->expect_100_continue) {
      nxe_istream_unset_ready(is);
      wait_for_100_continue(hcp, loop);
      return;
    }
    hcp->state=HCP_SENDING_BODY;
  }

  if (hcp->state==HCP_SENDING_BODY) {
    hcp->req_body_sending_started=1;
    nxe_istream* prev_is=hcp->req_body_in.pair;
    if (prev_is) {
      if (prev_is->ready) {
        hcp->req_body_in.ready=1;
        ISTREAM_CLASS(prev_is)->do_write(prev_is, &hcp->req_body_in);
      }
      if (!prev_is->ready) {
        nxe_istream_unset_ready(is);
        nxe_ostream_set_ready(loop, &hcp->req_body_in); // get notified when prev_is becomes ready again
      }
    }
    else {
      nxweb_log_error("no connected device for hcp->req_body_in");
      nxe_istream_unset_ready(is);
    }
  }
  else {
    // wrong state
    nxweb_log_error("called data_out_do_write() at wrong HCP state %d", hcp->state);
    nxe_istream_unset_ready(is);
  }
}

static nxe_size_t resp_body_out_read(nxe_istream* is, nxe_ostream* os, void* ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)is-offsetof(nxd_http_client_proto, resp_body_out));
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("http_client resp_body_out_read");

  if (hcp->request_complete) {
    nxweb_log_error("hcp->request_complete - resp_body_out_read() should not be called");
    *flags|=NXEF_EOF;
    nxe_istream_unset_ready(is);
    return 0;
  }

  if (hcp->state!=HCP_RECEIVING_BODY) {
    nxe_istream_unset_ready(is);
    nxe_ostream_set_ready(loop, &hcp->data_in); // get notified when prev_is ready
    return 0;
  }

  nxe_size_t bytes_received=0;

  if (hcp->first_body_chunk) {
    nxe_size_t first_body_chunk_size=hcp->first_body_chunk_end-hcp->first_body_chunk;
    if (first_body_chunk_size<=size) {
      bytes_received=first_body_chunk_size;
      memcpy(ptr, hcp->first_body_chunk, bytes_received);
      hcp->first_body_chunk=0;
      hcp->first_body_chunk_end=0;
    }
    else {
      bytes_received=size;
      memcpy(ptr, hcp->first_body_chunk, size);
      hcp->first_body_chunk+=size;
    }
    hcp->resp.content_received+=bytes_received;
    if (hcp->response_body_complete) {
      // rearm connection
      request_complete(hcp, loop);
      nxe_istream_unset_ready(is);
      *flags|=NXEF_EOF;
      return bytes_received;
    }
    ptr+=bytes_received;
    size-=bytes_received;
  }

  if (size>0) {
    nxe_istream* prev_is=hcp->data_in.pair;
    if (prev_is) {
      nxe_size_t bytes_received2=0;
      nxe_flags_t rflags=0;
      if (prev_is->ready) bytes_received2=ISTREAM_CLASS(prev_is)->read(prev_is, &hcp->data_in, ptr, size, &rflags);
      if (!prev_is->ready) {
        nxe_istream_unset_ready(is);
        nxe_ostream_set_ready(loop, &hcp->data_in); // get notified when prev_is becomes ready again
      }

      if (hcp->resp.chunked_encoding) {
        int r=_nxweb_decode_chunked_stream(&hcp->resp.cdstate, ptr, &bytes_received2);
        hcp->resp.content_received+=bytes_received2;
        bytes_received+=bytes_received2;
        if (r<0) nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_RESPONSE_CHUNKED_ENCODING_ERROR);
        else if (r>0) hcp->response_body_complete=1;
      }
      else {
        hcp->resp.content_received+=bytes_received2;
        bytes_received+=bytes_received2;
        if (hcp->resp.content_received >= hcp->resp.content_length && hcp->resp.content_length>=0) {
          hcp->response_body_complete=1;
        }
      }
      if (hcp->response_body_complete) {
        // rearm connection
        request_complete(hcp, loop);
        nxe_istream_unset_ready(is);
        *flags|=NXEF_EOF;
        return bytes_received;
      }
    }
    else {
      if (!prev_is) nxweb_log_error("no connected device for hcp->data_in");
      nxe_istream_unset_ready(is);
    }
  }

  return bytes_received;
}

static nxe_ssize_t req_body_in_write(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  //nxweb_log_error("req_body_in_write(%d)", size);
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)os-offsetof(nxd_http_client_proto, req_body_in));
  nxe_loop* loop=os->super.loop;

  nxweb_log_debug("http_client req_body_in_write");

  if (hcp->state!=HCP_SENDING_BODY) {
    nxe_ostream_unset_ready(os);
    nxe_istream_set_ready(loop, &hcp->data_out); // get notified when next_os ready
    return 0;
  }
  hcp->req_body_sending_started=1;
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
  nxe_ssize_t bytes_sent=0;
  if (size>0) {
    nxe_ostream* next_os=hcp->data_out.pair;
    if (next_os) {
      nxe_flags_t wflags=*flags;
      if (next_os->ready) bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &hcp->data_out, 0, 0, (nxe_data)ptr, size, &wflags);
      if (!next_os->ready) {
        //nxweb_log_error("req_body_in_write(%d) - next_os unready", size);
        nxe_ostream_unset_ready(os);
        nxe_istream_set_ready(loop, &hcp->data_out); // get notified when next_os becomes ready again
      }
    }
    else {
      nxweb_log_error("no connected device for hcp->data_out");
      nxe_ostream_unset_ready(os);
    }
  }
  if (*flags&NXEF_EOF && bytes_sent==size) {
    // end of request => get response
    nxe_ostream_unset_ready(os);
    start_receiving_response(hcp, loop);
    return bytes_sent;
  }
  nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
  return bytes_sent;
}

static void data_error_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {

  nxweb_log_debug("http_client data_error_on_message");

  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)sub-offsetof(nxd_http_client_proto, data_error));
  nxe_loop* loop=sub->super.loop;
  if ((data.i==NXE_HUP || data.i==NXE_RDHUP || data.i==NXE_RDCLOSED)
       && (hcp->resp.content_length<0 && !hcp->resp.chunked_encoding)
       && (hcp->state!=HCP_IDLE)) {
    // content-length = until-close
    //request_complete(hcp, loop);
    hcp->response_body_complete=1;
    nxe_istream_set_ready(loop, &hcp->resp_body_out); // signal readiness for EOF
    return;
  }
  else if (hcp->response_body_complete && !hcp->request_complete && data.i<0) {
    // queue error message until request is complete
    hcp->queued_error_message=data;
    return;
  }
  nxe_publish(&hcp->events_pub, data);
}

static void timer_keep_alive_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)timer-offsetof(nxd_http_client_proto, timer_keep_alive));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_KEEP_ALIVE_TIMEOUT);
  nxweb_log_info("client connection %p keep-alive timeout", hcp);
}

static void timer_read_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)timer-offsetof(nxd_http_client_proto, timer_read));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_READ_TIMEOUT);
  nxweb_log_warning("client connection %p read timeout", hcp);
}

static void timer_write_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)timer-offsetof(nxd_http_client_proto, timer_write));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_WRITE_TIMEOUT);
  nxweb_log_warning("client connection %p write timeout", hcp);
}

static void timer_100_continue_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_client_proto* hcp=(nxd_http_client_proto*)((char*)timer-offsetof(nxd_http_client_proto, timer_100_continue));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hcp->events_pub, (nxe_data)NXD_HCP_100CONTINUE_TIMEOUT);
  nxweb_log_warning("client connection %p 100-continue timeout", hcp);
}

static const nxe_ostream_class data_in_class={.do_read=data_in_do_read};
static const nxe_istream_class data_out_class={.do_write=data_out_do_write};
static const nxe_subscriber_class data_error_class={.on_message=data_error_on_message};
static const nxe_istream_class resp_body_out_class={.read=resp_body_out_read};
static const nxe_ostream_class req_body_in_class={.write=req_body_in_write};
static const nxe_timer_class timer_keep_alive_class={.on_timeout=timer_keep_alive_on_timeout};
static const nxe_timer_class timer_read_class={.on_timeout=timer_read_on_timeout};
static const nxe_timer_class timer_write_class={.on_timeout=timer_write_on_timeout};
static const nxe_timer_class timer_100_continue_class={.on_timeout=timer_100_continue_on_timeout};

void nxd_http_client_proto_init(nxd_http_client_proto* hcp, nxp_pool* nxb_pool) {
  memset(hcp, 0, sizeof(nxd_http_client_proto));
  hcp->nxb_pool=nxb_pool;
  hcp->data_in.super.cls.os_cls=&data_in_class;
  hcp->data_out.super.cls.is_cls=&data_out_class;
  hcp->data_out.evt.cls=NXE_EV_STREAM;
  hcp->data_error.super.cls.sub_cls=&data_error_class;
  hcp->resp_body_out.super.cls.is_cls=&resp_body_out_class;
  hcp->resp_body_out.evt.cls=NXE_EV_STREAM;
  hcp->req_body_in.super.cls.os_cls=&req_body_in_class;
  hcp->events_pub.super.cls.pub_cls=NXE_PUB_DEFAULT;
  hcp->timer_keep_alive.super.cls.timer_cls=&timer_keep_alive_class;
  hcp->timer_read.super.cls.timer_cls=&timer_read_class;
  hcp->timer_write.super.cls.timer_cls=&timer_write_class;
  hcp->timer_100_continue.super.cls.timer_cls=&timer_100_continue_class;
  hcp->data_out.ready=1;
  hcp->req_body_in.ready=0;
}

void nxd_http_client_proto_finalize(nxd_http_client_proto* hcp) {
  nxe_loop* loop=hcp->data_in.super.loop;
  nxe_unset_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hcp->timer_keep_alive);
  nxe_unset_timer(loop, NXWEB_TIMER_READ, &hcp->timer_read);
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
  nxe_unset_timer(loop, NXWEB_TIMER_100CONTINUE, &hcp->timer_100_continue);
  if (hcp->data_error.pub) nxe_unsubscribe(hcp->data_error.pub, &hcp->data_error);
  while (hcp->events_pub.sub) nxe_unsubscribe(&hcp->events_pub, hcp->events_pub.sub);
  if (hcp->data_in.pair) nxe_disconnect_streams(hcp->data_in.pair, &hcp->data_in);
  if (hcp->data_out.pair) nxe_disconnect_streams(&hcp->data_out, hcp->data_out.pair);
  if (hcp->req_body_in.pair) nxe_disconnect_streams(hcp->req_body_in.pair, &hcp->req_body_in);
  if (hcp->resp_body_out.pair) nxe_disconnect_streams(&hcp->resp_body_out, hcp->resp_body_out.pair);
  if (hcp->nxb) {
    nxb_empty(hcp->nxb);
    nxp_free(hcp->nxb_pool, hcp->nxb);
    hcp->nxb=0;
  }
}

void nxd_http_client_proto_connect(nxd_http_client_proto* hcp, nxe_loop* loop) {
  hcp->state=HCP_CONNECTING;
  nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
}

void nxd_http_client_proto_start_request(nxd_http_client_proto* hcp, nxweb_http_request* req) {

  nxweb_log_debug("nxd_http_client_proto_start_request");

  //nxweb_http_request* resp=&hcp->resp;
  hcp->nxb=nxp_alloc(hcp->nxb_pool);
  nxb_init(hcp->nxb, NXWEB_CONN_NXB_SIZE);

  hcp->req=req;
  if (!req->nxb) req->nxb=hcp->nxb;
  if (!req->host) req->host=hcp->host;

  nxe_loop* loop=hcp->data_in.super.loop;
  hcp->req_headers_ptr=_nxweb_prepare_client_request_headers(req);
  //nxweb_log_error("REQ: %s", hcp->req_headers_ptr);
  if (!hcp->req_body_in.pair && req->content && req->content_length>0) {
    nxd_obuffer_init(&hcp->ob, req->content, req->content_length);
    nxe_connect_streams(loop, &hcp->ob.data_out, &hcp->req_body_in);
  }
  assert(hcp->state==HCP_CONNECTING || hcp->state==HCP_IDLE);
  if (hcp->state==HCP_IDLE) {
    hcp->state=HCP_SENDING_HEADERS;
    nxe_unset_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hcp->timer_keep_alive);
    nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hcp->timer_write);
  }
  hcp->req_body_sending_started=0;
  hcp->receiving_100_continue=0;
  hcp->request_complete=0;
  hcp->response_body_complete=0;
  hcp->queued_error_message.l=0;
  nxe_istream_set_ready(loop, &hcp->data_out);
}

void nxd_http_client_proto_rearm(nxd_http_client_proto* hcp) {

  nxweb_log_debug("nxd_http_client_proto_rearm");

  assert(hcp->nxb);
  nxb_empty(hcp->nxb);
  nxp_free(hcp->nxb_pool, hcp->nxb);
  hcp->nxb=0;
  memset(&hcp->_req, 0, sizeof(hcp->_req));
  hcp->_req.nxb=hcp->nxb;
  hcp->_req.host=0;
  hcp->resp.nxb=hcp->nxb;
  while (hcp->events_pub.sub) nxe_unsubscribe(&hcp->events_pub, hcp->events_pub.sub);
  if (hcp->req_body_in.pair) nxe_disconnect_streams(hcp->req_body_in.pair, &hcp->req_body_in);
  if (hcp->resp_body_out.pair) nxe_disconnect_streams(&hcp->resp_body_out, hcp->resp_body_out.pair);
  nxe_ostream_unset_ready(&hcp->req_body_in);
  nxe_istream_unset_ready(&hcp->resp_body_out);
  nxe_ostream_unset_ready(&hcp->data_in);
  nxe_istream_unset_ready(&hcp->data_out);
}
