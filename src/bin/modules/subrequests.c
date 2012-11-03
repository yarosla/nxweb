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

struct subreq_data {
  nxd_streamer strm;
  nxd_streamer_node sn1;
  nxd_streamer_node sn2;
  nxd_streamer_node sn3;
  nxd_streamer_node sn4;
  nxd_obuffer ob1;
  nxd_obuffer ob2;
  nxd_fbuffer fb;
  int fd;
};

static const char subreq_handler_key; // variable's address matters
#define SUBREQ_HANDLER_KEY ((nxe_data)&subreq_handler_key)

static void subreq_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_http_request_data* req_data) {
  struct subreq_data* srdata=req_data->value.ptr;

  if (srdata->fd>0) close(srdata->fd);
  nxd_streamer_finalize(&srdata->strm);
  nxd_fbuffer_finalize(&srdata->fb);
}

static void subreq_on_response_ready(nxe_data data) {
  nxweb_http_server_connection* subconn=(nxweb_http_server_connection*)data.ptr;
  nxweb_http_server_connection* conn=subconn->parent;
  if (subconn->subrequest_failed) {
    nxweb_log_error("subreq failed");
    return;
  }
  nxweb_log_error("subreq response ready");
  nxweb_http_request* req=&conn->hsp.req;
  struct subreq_data* srdata=nxweb_get_request_data(req, SUBREQ_HANDLER_KEY).ptr;
  assert(srdata);
  nxe_connect_streams(subconn->tdata->loop, subconn->hsp.resp->content_out, &srdata->sn4.data_in);
  nxd_streamer_add_node(&srdata->strm, &srdata->sn4, 1);
}

static nxweb_result subreq_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  struct subreq_data* srdata=nxb_alloc_obj(req->nxb, sizeof(struct subreq_data));
  nxd_streamer_init(&srdata->strm);
  nxd_streamer_node_init(&srdata->sn1);
  nxd_streamer_node_init(&srdata->sn2);
  nxd_streamer_node_init(&srdata->sn3);
  nxd_streamer_node_init(&srdata->sn4);
  nxd_streamer_add_node(&srdata->strm, &srdata->sn1, 0);
  nxd_streamer_add_node(&srdata->strm, &srdata->sn3, 0);
  nxd_streamer_add_node(&srdata->strm, &srdata->sn2, 0);

  nxd_obuffer_init(&srdata->ob1, "[test1]", sizeof("[test1]")-1);
  nxe_connect_streams(conn->tdata->loop, &srdata->ob1.data_out, &srdata->sn1.data_in);

  nxd_obuffer_init(&srdata->ob2, "[test2]", sizeof("[test2]")-1);
  nxe_connect_streams(conn->tdata->loop, &srdata->ob2.data_out, &srdata->sn2.data_in);

  srdata->fd=open("www/root/index.htm", O_RDONLY|O_NONBLOCK);
  if (srdata->fd!=-1) {
    nxd_fbuffer_init(&srdata->fb, srdata->fd, 0, 15);
    nxe_connect_streams(conn->tdata->loop, &srdata->fb.data_out, &srdata->sn3.data_in);
  }
  else {
    nxweb_log_error("nxd_http_server_proto_start_sending_response(): can't open %s", resp->sendfile_path);
  }

  nxd_streamer_start(&srdata->strm);

  resp->content_out=&srdata->strm.data_out;
  resp->content_length=sizeof("[test1]")-1+sizeof("[test2]")-1+15+20;

  nxweb_set_request_data(req, SUBREQ_HANDLER_KEY, (nxe_data)(void*)srdata, subreq_finalize);

  nxweb_http_server_subrequest_start(conn, subreq_on_response_ready, 0, "/benchmark-inprocess");

  return NXWEB_OK;
}

NXWEB_HANDLER(subreq, "/subreq", .on_request=subreq_on_request,
        .flags=NXWEB_HANDLE_ANY, .priority=100);
