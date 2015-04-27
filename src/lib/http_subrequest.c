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

#include "nxweb/nxweb.h"

#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>

static void nxweb_composite_stream_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data);

nxweb_composite_stream* nxweb_composite_stream_init(nxweb_http_server_connection* conn, nxweb_http_request* req) {
  nxweb_composite_stream* cs=nxb_calloc_obj(req->nxb, sizeof(nxweb_composite_stream));
  cs->req=req;
  cs->conn=conn;
  nxd_streamer_init(&cs->strm);
  nxweb_set_request_data(req, (nxe_data)0, (nxe_data)(void*)cs, nxweb_composite_stream_finalize);
  return cs;
}

static nxweb_composite_stream_node* nxweb_composite_stream_append_node(nxweb_composite_stream* cs) {
  nxweb_composite_stream_node* csn=cs->first_node;
  if (csn) {
    while (csn->next) csn=csn->next;
    csn->snode.final=0; // not final anymore
    csn->next=nxb_calloc_obj(cs->req->nxb, sizeof(nxweb_composite_stream_node));;
    csn=csn->next;
  }
  else {
    cs->first_node=nxb_calloc_obj(cs->req->nxb, sizeof(nxweb_composite_stream_node));;
    csn=cs->first_node;
  }
  nxd_streamer_node_init(&csn->snode);
  nxd_streamer_add_node(&cs->strm, &csn->snode, 0);
  return csn;
}

void nxweb_composite_stream_append_bytes(nxweb_composite_stream* cs, const char* bytes, int length) {
  if (length) {
    nxweb_composite_stream_node* csn=nxweb_composite_stream_append_node(cs);
    nxd_obuffer_init(&csn->buffer.ob, bytes, length);
    nxe_connect_streams(cs->conn->tdata->loop, &csn->buffer.ob.data_out, &csn->snode.data_in);
  }
}

void nxweb_composite_stream_append_fd(nxweb_composite_stream* cs, int fd, off_t offset, off_t end) {
  if (fd!=-1) {
    nxweb_composite_stream_node* csn=nxweb_composite_stream_append_node(cs);
    csn->fd=fd;
    nxd_fbuffer_init(&csn->buffer.fb, fd, offset, end);
    nxe_connect_streams(cs->conn->tdata->loop, &csn->buffer.fb.data_out, &csn->snode.data_in);
  }
  else {
    nxweb_log_error("nxweb_composite_stream_append_fd(): invalid fd");
  }
}

static void nxweb_composite_stream_subrequest_on_response_ready(nxweb_http_server_connection* subconn, nxe_data data) {
  nxweb_http_server_connection* conn=subconn->parent;
  nxweb_http_request* req=&conn->hsp.req;
  nxweb_composite_stream* cs=data.ptr;
  assert(cs);
  nxweb_composite_stream_node* csn=cs->first_node;
  while (csn && csn->subconn!=subconn) csn=csn->next; // find corresponding node
  assert(csn);
  int status=subconn->hsp.resp->status_code;
  if (!subconn->subrequest_failed && (!status || status==200)) {
    if (!subconn->hsp.resp->content_length) {
      // connect zero-length stream
      nxd_obuffer_init(&csn->buffer.ob, "", 0);
      nxe_connect_streams(conn->tdata->loop, &csn->buffer.ob.data_out, &csn->snode.data_in);
    }
    else {
      nxe_connect_streams(subconn->tdata->loop, subconn->hsp.resp->content_out, &csn->snode.data_in);
    }
  }
  else {
    // this might happen after first successful call to on_response_ready()
    if (csn->snode.data_in.pair) {
      // response streaming have already started
      nxweb_log_error("subrequest failed after response streaming started: %s%s ref: %s", subconn->hsp.req.host, subconn->hsp.req.uri, req->uri);
      nxweb_http_server_connection_finalize(cs->conn, 0);
    }
    else {
      nxd_obuffer_init(&csn->buffer.ob, "<!--[ssi error]-->", sizeof("<!--[ssi error]-->")-1);
      nxe_connect_streams(conn->tdata->loop, &csn->buffer.ob.data_out, &csn->snode.data_in);
      nxweb_log_warning("subrequest failed: %s%s ref: %s", subconn->hsp.req.host, subconn->hsp.req.uri, req->uri);
    }
  }
}

void nxweb_composite_stream_append_subrequest(nxweb_composite_stream* cs, const char* host, const char* url) {
  nxweb_composite_stream_node* csn=nxweb_composite_stream_append_node(cs);

  csn->subconn=nxweb_http_server_subrequest_start(cs->conn, nxweb_composite_stream_subrequest_on_response_ready, (nxe_data)(void*)cs, host, url);
  if (!csn->subconn) {
    nxweb_log_error("nxweb_http_server_subrequest_start failed: %s %s %d", host, url, (int)cs->conn->connection_closing);
    // append bytes instead
    nxd_obuffer_init(&csn->buffer.ob, "<!--[subrequest failed]-->", sizeof("<!--[subrequest failed]-->")-1);
    nxe_connect_streams(cs->conn->tdata->loop, &csn->buffer.ob.data_out, &csn->snode.data_in);
  }
}

void nxweb_composite_stream_close(nxweb_composite_stream* cs) { // call this right after appending last node
  nxd_streamer_close(&cs->strm);
}

void nxweb_composite_stream_start(nxweb_composite_stream* cs, nxweb_http_response* resp) {
  resp->content_out=&cs->strm.data_out;
  resp->content_length=-1; // chunked encoding
  resp->chunked_autoencode=1;
  nxd_streamer_start(&cs->strm);
}

static void nxweb_composite_stream_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data) {
  nxweb_composite_stream* cs=data.ptr;
  nxweb_composite_stream_node* csn=cs->first_node;
  while (csn) {
    if (csn->fd) {
      close(csn->fd);
      nxd_fbuffer_finalize(&csn->buffer.fb);
    }
    csn=csn->next;
  }
  nxd_streamer_finalize(&cs->strm);
}
