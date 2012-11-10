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

static const char nxweb_composite_stream_key; // variable's address only matters
#define NXWEB_COMPOSITE_STREAM_KEY ((nxe_data)&nxweb_composite_stream_key)

static void nxweb_composite_stream_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data);

nxweb_composite_stream* nxweb_composite_stream_init(nxweb_http_server_connection* conn, nxweb_http_request* req) {
  nxweb_composite_stream* cs=nxb_calloc_obj(req->nxb, sizeof(nxweb_composite_stream));
  cs->req=req;
  cs->conn=conn;
  nxd_streamer_init(&cs->strm);
  nxweb_set_request_data(req, NXWEB_COMPOSITE_STREAM_KEY, (nxe_data)(void*)cs, nxweb_composite_stream_finalize);
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
  nxweb_composite_stream_node* csn=nxweb_composite_stream_append_node(cs);
  nxd_obuffer_init(&csn->buffer.ob, bytes, length);
  nxe_connect_streams(cs->conn->tdata->loop, &csn->buffer.ob.data_out, &csn->snode.data_in);
}

void nxweb_composite_stream_append_fd(nxweb_composite_stream* cs, int fd, off_t offset, off_t end) {
  nxweb_composite_stream_node* csn=nxweb_composite_stream_append_node(cs);
  if (fd!=-1) {
    csn->fd=fd;
    nxd_fbuffer_init(&csn->buffer.fb, fd, offset, end);
    nxe_connect_streams(cs->conn->tdata->loop, &csn->buffer.fb.data_out, &csn->snode.data_in);
  }
  else {
    nxweb_log_error("nxweb_composite_stream_append_fd(): invalid fd");
  }
}

static void nxweb_composite_stream_subrequest_on_response_ready(nxe_data data) {
  nxweb_http_server_connection* subconn=(nxweb_http_server_connection*)data.ptr;
  nxweb_http_server_connection* conn=subconn->parent;
  nxweb_http_request* req=&conn->hsp.req;
  nxweb_composite_stream* cs=nxweb_get_request_data(req, NXWEB_COMPOSITE_STREAM_KEY).ptr;
  assert(cs);
  nxweb_composite_stream_node* csn=cs->first_node;
  while (csn && csn->subconn!=subconn) csn=csn->next; // find corresponding node
  assert(csn);
  int status=subconn->hsp.resp->status_code;
  if (!subconn->subrequest_failed && (!status || status==200)) {
    nxe_connect_streams(subconn->tdata->loop, subconn->hsp.resp->content_out, &csn->snode.data_in);
  }
  else {
    nxd_obuffer_init(&csn->buffer.ob, "<!--[ssi error]-->", sizeof("<!--[ssi error]-->")-1);
    nxe_connect_streams(conn->tdata->loop, &csn->buffer.ob.data_out, &csn->snode.data_in);
    nxweb_log_error("subrequest failed: %s%s", subconn->hsp.req.host, subconn->hsp.req.uri);
    return;
  }
}

void nxweb_composite_stream_append_subrequest(nxweb_composite_stream* cs, const char* host, const char* url) {
  nxweb_composite_stream_node* csn=nxweb_composite_stream_append_node(cs);

  csn->subconn=nxweb_http_server_subrequest_start(cs->conn, nxweb_composite_stream_subrequest_on_response_ready, host, url);
}

void nxweb_composite_stream_close(nxweb_composite_stream* cs) { // call this right after appending last node
  nxweb_composite_stream_node* csn=cs->first_node;
  if (csn) {
    while (csn->next) csn=csn->next;
    csn->snode.final=1;
  }
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
