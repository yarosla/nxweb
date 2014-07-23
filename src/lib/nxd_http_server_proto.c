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

#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>

static const char* response_100_continue = "HTTP/1.1 100 Continue\r\n\r\n";

static void nxd_http_server_proto_start_sending_response(nxd_http_server_proto* hsp, nxweb_http_response* resp);

void _nxweb_call_request_finalizers(nxd_http_server_proto* hsp) {
  // this could be called more than once, be ready for it

  nxweb_log_debug("_nxweb_call_request_finalizers");

  // it is not very good that we access higher level (connection) object here but...
  nxweb_http_server_connection* conn=(nxweb_http_server_connection*)((char*)hsp-offsetof(nxweb_http_server_connection, hsp));

  nxweb_http_server_connection_finalize_subrequests(conn, 0);

  nxweb_http_request* req=&hsp->req;
  nxweb_http_response* resp=hsp->resp;

  if (resp && resp->content_out && resp->content_out->pair) {
    nxe_disconnect_streams(resp->content_out, resp->content_out->pair);
  }

  if (req->access_log) {
    nxweb_server_config.access_log_on_request_complete(conn, req, resp);
    nxweb_access_log_write(req);
    req->access_log=0;
  }

  if (hsp->req_finalize) {
    hsp->req_finalize(hsp, hsp->req_data);
    hsp->req_finalize=0; // call no more
    hsp->req_data=0;
  }
  nxweb_http_request_data* rdata=hsp->req.data_chain;
  while (rdata) {
    if (rdata->finalize) {
      rdata->finalize(conn, req, resp, rdata->value);
      rdata->finalize=0; // call no more
    }
    rdata=rdata->next;
  }
  if (conn->handler && conn->handler->num_filters) {
    // finalize filters
    int i;
    nxweb_filter* filter;
    nxweb_filter_data* fdata;
    nxweb_filter** filters=conn->handler->filters;
    const int num_filters=conn->handler->num_filters;
    for (i=0; i<num_filters; i++) {
      filter=filters[i];
      fdata=req->filter_data[i];
      if (fdata && filter->finalize)
        filter->finalize(filter, conn, req, resp, fdata);
      req->filter_data[i]=0; // call no more
    }
  }
  if (conn->handler && conn->handler->on_complete) {
    conn->handler->on_complete(conn, req, resp);
    conn->handler->on_complete=0; // call no more
  }
  conn->handler=0;
  conn->handler_param=(nxe_data)0;
}

static void request_cleanup(nxe_loop* loop, nxd_http_server_proto* hsp) {

  nxweb_log_debug("request_cleanup");

  // get ready for new request:
  //  - release per-request resources
  //  - initialize variables
  _nxweb_call_request_finalizers(hsp);
  nxd_fbuffer_finalize(&hsp->fb);
  if (hsp->resp && hsp->resp->sendfile_fd) {
    close(hsp->resp->sendfile_fd);
  }
  nxb_empty(hsp->nxb);
  nxp_free(hsp->nxb_pool, hsp->nxb);
  hsp->nxb=0;

  hsp->request_count++;
  hsp->state=HSP_WAITING_FOR_REQUEST;
  hsp->headers_bytes_received=0;
  if (hsp->resp && hsp->resp->keep_alive) {
    nxe_ostream_set_ready(loop, &hsp->data_in);
    nxe_set_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hsp->timer_keep_alive);
  }
  else {
    nxe_ostream* os=hsp->data_out.pair;
    if (os && OSTREAM_CLASS(os)->shutdown) OSTREAM_CLASS(os)->shutdown(os);
    nxe_set_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hsp->timer_keep_alive); // do not rely on other end properly reacting to shutdown
  }
  memset(&hsp->req, 0, sizeof(nxweb_http_request));
  memset(&hsp->_resp, 0, sizeof(nxweb_http_response));
  hsp->resp=0;
}

static void request_complete(nxe_loop* loop, nxd_http_server_proto* hsp) {

  nxweb_log_debug("request_complete");

  nxe_istream_unset_ready(&hsp->data_out);
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);
  if (hsp->resp_body_in.pair) nxe_disconnect_streams(hsp->resp_body_in.pair, &hsp->resp_body_in);
  if (hsp->req_body_out.pair) nxe_disconnect_streams(&hsp->req_body_out, hsp->req_body_out.pair);
  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_COMPLETE);
}

static void data_in_do_read(nxe_ostream* os, nxe_istream* is) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)os-offsetof(nxd_http_server_proto, data_in));
  nxe_loop* loop=os->super.loop;

  nxweb_log_debug("data_in_do_read");

  if (hsp->state==HSP_WAITING_FOR_REQUEST) {
    hsp->nxb=nxp_alloc(hsp->nxb_pool);
    nxb_init(hsp->nxb, NXWEB_CONN_NXB_SIZE);
    nxb_make_room(hsp->nxb, NXWEB_MAX_REQUEST_HEADERS_SIZE);
    nxe_unset_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hsp->timer_keep_alive);
    nxe_set_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
    hsp->state=HSP_RECEIVING_HEADERS;
  }

  if (hsp->state==HSP_RECEIVING_HEADERS) {
    int size;
    nxe_flags_t flags=0;
    void* ptr=nxb_get_room(hsp->nxb, &size);
    int bytes_received=ISTREAM_CLASS(is)->read(is, os, ptr, size, &flags);
    if (bytes_received) {
      nxb_blank_fast(hsp->nxb, bytes_received);
      int read_buf_size;
      char* read_buf=nxb_get_unfinished(hsp->nxb, &read_buf_size);
      hsp->headers_bytes_received=read_buf_size;
      char* end_of_headers;
      char* start_of_body;
      if ((end_of_headers=_nxweb_find_end_of_http_headers(read_buf, read_buf_size, &start_of_body))) {
        nxb_finish_stream(hsp->nxb, 0);
        hsp->req.nxb=hsp->nxb;
        hsp->req.uid=nxweb_generate_unique_id();
        if (_nxweb_parse_http_request(&hsp->req, read_buf, end_of_headers)) {
          // bad request
          nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
          nxe_ostream_unset_ready(os);
          nxweb_http_response* resp=_nxweb_http_response_init(&hsp->_resp, hsp->nxb, 0);
          nxweb_send_http_error(resp, 400, "Bad Request");
          resp->keep_alive=0; // close connection
          hsp->cls->start_sending_response(hsp, resp);
          return;
        }
        char* read_buf_end=read_buf+read_buf_size;
        if (start_of_body<read_buf_end) {
          hsp->first_body_chunk=start_of_body;
          hsp->first_body_chunk_end=read_buf_end;
        }
        else {
          hsp->first_body_chunk=0;
          hsp->first_body_chunk_end=0;
          if (hsp->req.expect_100_continue) {
            hsp->req.sending_100_continue=1;
            hsp->resp_headers_ptr=response_100_continue;
            nxe_istream_set_ready(loop, &hsp->data_out);
          }
        }
        nxe_ostream_unset_ready(os);
        hsp->resp=_nxweb_http_response_init(&hsp->_resp, hsp->nxb, &hsp->req);
        nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_RECEIVED);
        if (hsp->req.content_length) { // is body expected?
          hsp->state=HSP_RECEIVING_BODY;
          nxe_istream_set_ready(loop, &hsp->req_body_out);
        }
        else {
          nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
          hsp->state=HSP_HANDLING;
        }
      }
      else {
        if (read_buf_size>=NXWEB_MAX_REQUEST_HEADERS_SIZE) {
          // bad request (too large)
          nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
          nxe_ostream_unset_ready(os);
          nxweb_http_response* resp=_nxweb_http_response_init(&hsp->_resp, hsp->nxb, 0);
          nxweb_send_http_error(resp, 400, "Bad Request");
          resp->keep_alive=0; // close connection
          hsp->cls->start_sending_response(hsp, resp);
        }
      }
    }
  }
  else if (hsp->state==HSP_RECEIVING_BODY) {
    nxe_ostream* next_os=hsp->req_body_out.pair;
    if (next_os) {
      if (next_os->ready) OSTREAM_CLASS(next_os)->do_read(next_os, &hsp->req_body_out);
      if (!next_os->ready) {
        nxe_ostream_unset_ready(os);
        nxe_istream_set_ready(loop, &hsp->req_body_out); // get notified when next_os becomes ready again
      }
    }
    else {
      nxweb_log_error("no connected device for hsp->req_body_out");
      nxe_ostream_unset_ready(os);
    }
  }
}

static void data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)is-offsetof(nxd_http_server_proto, data_out));
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("data_out_do_write");

  if (hsp->req.sending_100_continue) {
    if (hsp->resp_headers_ptr && *hsp->resp_headers_ptr) {
      int size=strlen(hsp->resp_headers_ptr);
      nxe_flags_t flags=NXEF_EOF;
      nxe_ssize_t bytes_sent=OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)hsp->resp_headers_ptr, size, &flags);
      hsp->resp_headers_ptr+=bytes_sent;
      if (bytes_sent==size) {
        hsp->req.sending_100_continue=0;
        if (hsp->state==HSP_SENDING_HEADERS) hsp->resp_headers_ptr=hsp->resp->raw_headers;
        else {
          nxe_istream_unset_ready(is);
          return;
        }
      }
      else {
        return;
      }
    }
  }

  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);
  //nxweb_log_error("conn hsp=%p write timer restarted", hsp);
  nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);

  if (hsp->state==HSP_SENDING_HEADERS) {
    if (hsp->resp_headers_ptr && *hsp->resp_headers_ptr) {
      int size=strlen(hsp->resp_headers_ptr);
      nxe_flags_t flags=NXEF_EOF;
      nxe_ssize_t bytes_sent=OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)hsp->resp_headers_ptr, size, &flags);
      hsp->resp_headers_ptr+=bytes_sent;
      if (bytes_sent<size) return;
      hsp->resp_headers_ptr=0;
    }
    if (!hsp->resp->content_length || hsp->req.head_method) {
      request_complete(loop, hsp);
      return;
    }
    hsp->state=HSP_SENDING_BODY;
  }

  if (hsp->state==HSP_SENDING_BODY) {
    nxe_istream* prev_is=hsp->resp_body_in.pair;
    if (prev_is) {
      if (prev_is->ready) {
        hsp->resp_body_in.ready=1;
        ISTREAM_CLASS(prev_is)->do_write(prev_is, &hsp->resp_body_in);
      }
      if (!prev_is->ready) {
        nxe_istream_unset_ready(is);
        nxe_ostream_set_ready(loop, &hsp->resp_body_in); // get notified when prev_is becomes ready again
      }
    }
    else {
      nxweb_log_error("no connected device for hsp->resp_body_in");
      nxe_istream_unset_ready(is);
    }
  }
  else {
    // wrong state
    nxweb_log_error("called data_out_do_write() at wrong HSP state %d", hsp->state);
    nxe_istream_unset_ready(is);
  }
}

static inline int is_request_body_complete(nxd_http_server_proto* hsp) {
  return (hsp->req.content_length > 0 && hsp->req.content_received >= hsp->req.content_length)
          || (hsp->req.chunked_content_complete);
}

static nxe_size_t req_body_out_read(nxe_istream* is, nxe_ostream* os, void* ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)is-offsetof(nxd_http_server_proto, req_body_out));
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("req_body_out_read");

  if (hsp->state==HSP_HANDLING) {
    nxweb_log_error("hsp->state==HSP_HANDLING - req_body_out_read() should not be called");
    *flags|=NXEF_EOF;
    nxe_istream_unset_ready(is);
    return 0;
  }

  if (hsp->state!=HSP_RECEIVING_BODY) {
    nxe_istream_unset_ready(is);
    nxe_ostream_set_ready(loop, &hsp->data_in); // get notified when prev_is ready
    return 0;
  }

  nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);

  nxe_size_t bytes_received=0;
  if (hsp->first_body_chunk) {
    nxe_size_t first_body_chunk_size=hsp->first_body_chunk_end-hsp->first_body_chunk;
    if (first_body_chunk_size<=size) {
      bytes_received=first_body_chunk_size;
      memcpy(ptr, hsp->first_body_chunk, bytes_received);
      hsp->first_body_chunk=0;
      hsp->first_body_chunk_end=0;
    }
    else {
      bytes_received=size;
      memcpy(ptr, hsp->first_body_chunk, size);
      hsp->first_body_chunk+=size;
    }
    if (hsp->req.chunked_encoding) {
      int r=_nxweb_decode_chunked_stream(&hsp->req.cdstate, ptr, &bytes_received);
      if (r<0) nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_CHUNKED_ENCODING_ERROR);
      else if (r>0) hsp->req.chunked_content_complete=1;
    }
    hsp->req.content_received+=bytes_received;
    if (is_request_body_complete(hsp)) {
      nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_BODY_RECEIVED);
      hsp->state=HSP_HANDLING;
      nxe_ostream_unset_ready(&hsp->data_in);
      nxe_istream_unset_ready(is);
      *flags|=NXEF_EOF;
      return bytes_received;
    }
    ptr+=bytes_received;
    size-=bytes_received;
  }
  if (size>0) {
    nxe_istream* prev_is=hsp->data_in.pair;
    if (prev_is) {
      nxe_size_t bytes_received2=0;
      nxe_flags_t rflags=0;
      if (prev_is->ready) bytes_received2=ISTREAM_CLASS(prev_is)->read(prev_is, &hsp->data_in, ptr, size, &rflags);
      if (!prev_is->ready) {
        nxe_istream_unset_ready(is);
        nxe_ostream_set_ready(loop, &hsp->data_in); // get notified when prev_is becomes ready again
      }
      if (hsp->req.chunked_encoding) {
        int r=_nxweb_decode_chunked_stream(&hsp->req.cdstate, ptr, &bytes_received2);
        if (r<0) nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_CHUNKED_ENCODING_ERROR);
        else if (r>0) hsp->req.chunked_content_complete=1;
      }
      hsp->req.content_received+=bytes_received2;
      bytes_received+=bytes_received2;
      if (is_request_body_complete(hsp)) {
        nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_BODY_RECEIVED);
        hsp->state=HSP_HANDLING;
        nxe_ostream_unset_ready(&hsp->data_in);
        nxe_istream_unset_ready(is);
        //nxweb_log_error("request body complete");
        *flags|=NXEF_EOF;
        return bytes_received;
      }
    }
    else {
      if (!prev_is) nxweb_log_error("no connected device for hsp->data_in");
      nxe_istream_unset_ready(is);
    }
  }
  nxe_set_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
  return bytes_received;
}

static nxe_ssize_t resp_body_in_write_or_sendfile(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)os-offsetof(nxd_http_server_proto, resp_body_in));
  nxe_loop* loop=os->super.loop;

  nxweb_log_debug("resp_body_in_write_or_sendfile");

  if (hsp->state!=HSP_SENDING_BODY) {
    nxe_ostream_unset_ready(os);
    nxe_istream_set_ready(loop, &hsp->data_out); // get notified when next_os is ready
    return 0;
  }
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);
  nxe_ssize_t bytes_sent=0;
  if (size || hsp->resp->chunked_autoencode) {
    nxe_ostream* next_os=hsp->data_out.pair;
    if (next_os) {
      nxe_flags_t wflags=*flags;
      if (next_os->ready) {
        if (hsp->resp->chunked_autoencode) {
          _Bool skip=0;
          void* send_ptr;
          nxe_size_t chunk_size=size;
          nxe_size_t send_size;
          nxe_flags_t cwf=*flags;
          if (_nxweb_encode_chunked_stream(&hsp->resp->cestate, &chunk_size, &send_ptr, &send_size, &cwf)) {
            nxe_ssize_t cbs=OSTREAM_CLASS(next_os)->write(next_os, &hsp->data_out, 0, 0, (nxe_data)send_ptr, send_size, &cwf);
            _nxweb_encode_chunked_advance(&hsp->resp->cestate, cbs);
            if (cbs!=send_size) skip=1;
          }
          if (!skip && chunk_size) {
            bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &hsp->data_out, fd, fr, ptr, chunk_size, &cwf);
            _nxweb_encode_chunked_advance(&hsp->resp->cestate, bytes_sent);
            if (bytes_sent!=chunk_size) skip=1;
          }
          if (!skip) {
            // if we are here that means we either had zero-length write (with or without eof)
            // or we have successfully written out whole chunk
            cwf=chunk_size==size? *flags : 0; // if it is EOF and whole size made into chunk then send final chunk
            nxe_size_t zero_size=0; // otherwise just send out chunk trailer "\r\n"
            if (_nxweb_encode_chunked_stream(&hsp->resp->cestate, &zero_size, &send_ptr, &send_size, &cwf)) {
              nxe_ssize_t cbs=OSTREAM_CLASS(next_os)->write(next_os, &hsp->data_out, 0, 0, (nxe_data)send_ptr, send_size, &cwf);
              _nxweb_encode_chunked_advance(&hsp->resp->cestate, cbs);
              if (cbs!=send_size) skip=1;
            }
          }
        }
        else {
          bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &hsp->data_out, fd, fr, ptr, size, &wflags);
        }
      }
      if (!next_os->ready) {
        nxe_ostream_unset_ready(os);
        nxe_istream_set_ready(loop, &hsp->data_out); // get notified when next_os becomes ready again
      }
    }
    else {
      nxweb_log_error("no connected device for hsp->data_out");
      nxe_ostream_unset_ready(os);
    }
  }
  hsp->resp->bytes_sent+=bytes_sent;
  if (*flags&NXEF_EOF && bytes_sent==size && (!hsp->resp->chunked_autoencode || _nxweb_encode_chunked_is_complete(&hsp->resp->cestate))) {
    // end of response => rearm connection
    request_complete(loop, hsp);
    return bytes_sent;
  }
  nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);
  return bytes_sent;
}

static void data_error_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)sub-offsetof(nxd_http_server_proto, data_error));

  nxweb_log_debug("data_error_on_message");

  //nxe_loop* loop=sub->super.loop;
/*
  if (data.i==NXE_PROTO_ERROR && hsp->state==HSP_RECEIVING_HEADERS && hsp->headers_bytes_received==0) {
    // SSL protocol error (http connection attempted on SSL port?) => bad request
    nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
    nxe_ostream_unset_ready(&hsp->data_in);
    nxweb_http_response* resp=_nxweb_http_response_init(&hsp->_resp, hsp->nxb, 0);
    nxweb_send_http_error(resp, 400, "Bad Request");
    nxd_http_server_proto_start_sending_response(hsp, resp);
    return;
  }
*/
  nxe_publish(&hsp->events_pub, data);
}

static void timer_keep_alive_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)timer-offsetof(nxd_http_server_proto, timer_keep_alive));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_KEEP_ALIVE_TIMEOUT);
  nxweb_log_info("connection %p keep-alive timeout", hsp);
}

static void timer_read_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)timer-offsetof(nxd_http_server_proto, timer_read));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_READ_TIMEOUT);
  nxweb_log_warning("connection %p read timeout", hsp);
}

static void timer_write_on_timeout(nxe_timer* timer, nxe_data data) {
  nxd_http_server_proto* hsp=(nxd_http_server_proto*)((char*)timer-offsetof(nxd_http_server_proto, timer_write));
  //nxe_loop* loop=sub->super.loop;
  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_WRITE_TIMEOUT);
  nxweb_log_warning("connection %p write timeout", hsp);
}

static const nxe_ostream_class data_in_class={.do_read=data_in_do_read};
static const nxe_istream_class data_out_class={.do_write=data_out_do_write};
static const nxe_subscriber_class data_error_class={.on_message=data_error_on_message};
static const nxe_istream_class req_body_out_class={.read=req_body_out_read};
static const nxe_ostream_class resp_body_in_class={.write=resp_body_in_write_or_sendfile};
static const nxe_timer_class timer_keep_alive_class={.on_timeout=timer_keep_alive_on_timeout};
static const nxe_timer_class timer_read_class={.on_timeout=timer_read_on_timeout};
static const nxe_timer_class timer_write_class={.on_timeout=timer_write_on_timeout};

void nxd_http_server_proto_finalize(nxd_http_server_proto* hsp) {
  _nxweb_call_request_finalizers(hsp);
  nxe_loop* loop=hsp->data_in.super.loop;
  nxe_unset_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hsp->timer_keep_alive);
  nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
  nxe_unset_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);
  if (hsp->data_error.pub) nxe_unsubscribe(hsp->data_error.pub, &hsp->data_error);
  while (hsp->events_pub.sub) nxe_unsubscribe(&hsp->events_pub, hsp->events_pub.sub);
  if (hsp->data_in.pair) nxe_disconnect_streams(hsp->data_in.pair, &hsp->data_in);
  if (hsp->data_out.pair) nxe_disconnect_streams(&hsp->data_out, hsp->data_out.pair);
  if (hsp->resp_body_in.pair) nxe_disconnect_streams(hsp->resp_body_in.pair, &hsp->resp_body_in);
  if (hsp->req_body_out.pair) nxe_disconnect_streams(&hsp->req_body_out, hsp->req_body_out.pair);
  nxd_fbuffer_finalize(&hsp->fb);
  if (hsp->resp && hsp->resp->sendfile_fd) close(hsp->resp->sendfile_fd);
  if (hsp->nxb) {
    nxb_empty(hsp->nxb);
    nxp_free(hsp->nxb_pool, hsp->nxb);
  }
}

void nxd_http_server_proto_finish_response(nxweb_http_response* resp) {
  // make sure there is no unfinished stream left in resp->nxb
  int size;
  nxb_get_unfinished(resp->nxb, &size);
  if (size) {
    if (!resp->content && !resp->sendfile_path && !resp->sendfile_fd) {
      resp->content=nxb_finish_stream(resp->nxb, &size);
      resp->content_length=size;
    }
    else {
      nxb_unfinish_stream(resp->nxb);
    }
  }
}

void nxd_http_server_proto_setup_content_out(nxd_http_server_proto* hsp, nxweb_http_response* resp) {
  if (resp->content_out) return; // already setup

  nxweb_log_debug("nxd_http_server_proto_setup_content_out");

  nxd_http_server_proto_finish_response(resp);

  if (resp->content && resp->content_length>0) {
    nxd_obuffer_init(&hsp->ob, resp->content, resp->content_length);
    resp->content_out=&hsp->ob.data_out;
  }
  else if (resp->sendfile_fd && resp->content_length>0) {
    assert(resp->sendfile_end - resp->sendfile_offset == resp->content_length);
    assert(!hsp->fb.fd); // must not setup fbuffer twice
    nxd_fbuffer_init(&hsp->fb, resp->sendfile_fd, resp->sendfile_offset, resp->sendfile_end);
    resp->content_out=&hsp->fb.data_out;
  }
  else if (resp->sendfile_path && resp->content_length>0) {
    resp->sendfile_fd=open(resp->sendfile_path, O_RDONLY|O_NONBLOCK);
    if (resp->sendfile_fd!=-1) {
      assert(resp->sendfile_end - resp->sendfile_offset == resp->content_length);
      assert(!hsp->fb.fd); // must not setup fbuffer twice
      nxd_fbuffer_init(&hsp->fb, resp->sendfile_fd, resp->sendfile_offset, resp->sendfile_end);
      resp->content_out=&hsp->fb.data_out;
    }
    else {
      nxweb_log_error("nxd_http_server_proto_start_sending_response(): can't open %s", resp->sendfile_path);
    }
  }
}

void nxweb_reset_content_out(nxd_http_server_proto* hsp, nxweb_http_response* resp) {
  resp->content_out=0;
  resp->content=0;
  resp->content_length=0;
  resp->sendfile_path=0;
  if (resp->sendfile_fd) close(resp->sendfile_fd);
  if (hsp->fb.fd) nxd_fbuffer_finalize(&hsp->fb);
  resp->sendfile_fd=0;
  resp->chunked_autoencode=0;
  resp->chunked_encoding=0;
  resp->gzip_encoded=0;
}

static void nxd_http_server_proto_start_sending_response(nxd_http_server_proto* hsp, nxweb_http_response* resp) {
  nxweb_log_debug("nxd_http_server_proto_start_sending_response");

  if (hsp->state!=HSP_RECEIVING_HEADERS && hsp->state!=HSP_RECEIVING_BODY && hsp->state!=HSP_HANDLING) {
    nxweb_log_error("illegal state for start_sending_response()");
    return;
  }

  nxweb_http_request* req=&hsp->req;
  hsp->resp=resp;
  nxe_loop* loop=hsp->data_in.super.loop;
  nxe_unset_timer(loop, NXWEB_TIMER_READ, &hsp->timer_read);
  if (!resp->nxb) resp->nxb=hsp->nxb;

  assert(!resp->chunked_autoencode || (resp->chunked_autoencode && resp->content_length==-1));

  if (req->if_modified_since && resp->last_modified
      && resp->last_modified<=req->if_modified_since
      && resp->status_code!=304) {
    nxweb_log_info("responding with 304 Not Modified for %s", req->uri);
    nxweb_reset_content_out(hsp, resp);
    resp->status_code=304;
    resp->status="Not Modified";
  }

  if (resp->chunked_autoencode) _nxweb_encode_chunked_init(&resp->cestate);

  nxd_http_server_proto_setup_content_out(hsp, resp);

  if (resp->content_out) {
    nxe_connect_streams(loop, resp->content_out, &hsp->resp_body_in);
  }
  else if (resp->content_length) {
    nxweb_log_error("nxd_http_server_proto_start_sending_response(): no content_out stream");
  }

  if (!resp->raw_headers) _nxweb_prepare_response_headers(loop, resp);

  if (!req->sending_100_continue) {
    hsp->resp_headers_ptr=resp->raw_headers;
    nxe_istream_set_ready(loop, &hsp->data_out);
  }
  hsp->state=HSP_SENDING_HEADERS;
  nxe_set_timer(loop, NXWEB_TIMER_WRITE, &hsp->timer_write);
}

static void connect_request_body_out(nxd_http_server_proto* hsp, nxe_ostream* is) {
  nxe_connect_streams(hsp->data_in.super.loop, &hsp->req_body_out, is);
}

static nxe_ostream* get_request_body_out_pair(nxd_http_server_proto* hsp) {
  return hsp->req_body_out.pair;
}

static void start_receiving_request_body(nxd_http_server_proto* hsp) {
  nxe_ostream_set_ready(hsp->data_in.super.loop, &hsp->data_in);
}

static const nxd_http_server_proto_class http_server_proto_class={
  .finalize=nxd_http_server_proto_finalize,
  .start_sending_response=nxd_http_server_proto_start_sending_response,
  .start_receiving_request_body=start_receiving_request_body,
  .connect_request_body_out=connect_request_body_out,
  .get_request_body_out_pair=get_request_body_out_pair,
  .request_cleanup=request_cleanup
};

void nxd_http_server_proto_init(nxd_http_server_proto* hsp, nxp_pool* nxb_pool) {
  memset(hsp, 0, sizeof(nxd_http_server_proto));
  hsp->cls=&http_server_proto_class;
  hsp->nxb_pool=nxb_pool;
  hsp->data_in.super.cls.os_cls=&data_in_class;
  hsp->data_out.super.cls.is_cls=&data_out_class;
  hsp->data_out.evt.cls=NXE_EV_STREAM;
  hsp->data_error.super.cls.sub_cls=&data_error_class;
  hsp->req_body_out.super.cls.is_cls=&req_body_out_class;
  hsp->req_body_out.evt.cls=NXE_EV_STREAM;
  hsp->resp_body_in.super.cls.os_cls=&resp_body_in_class;
  hsp->events_pub.super.cls.pub_cls=NXE_PUB_DEFAULT;
  hsp->timer_keep_alive.super.cls.timer_cls=&timer_keep_alive_class;
  hsp->timer_read.super.cls.timer_cls=&timer_read_class;
  hsp->timer_write.super.cls.timer_cls=&timer_write_class;
  hsp->data_in.ready=1;
  hsp->resp_body_in.ready=1;
}

void nxd_http_server_proto_connect(nxd_http_server_proto* hsp, nxe_loop* loop) {
  hsp->state=HSP_WAITING_FOR_REQUEST;
  nxe_set_timer(loop, NXWEB_TIMER_KEEP_ALIVE, &hsp->timer_keep_alive);
}
