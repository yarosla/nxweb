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

static nxweb_result subreq_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  nxd_streamer* strm=nxb_alloc_obj(req->nxb, sizeof(nxd_streamer));
  nxd_streamer_init(strm);
  nxd_streamer_node* sn1=nxb_alloc_obj(req->nxb, sizeof(nxd_streamer_node));
  nxd_streamer_node_init(sn1);
  nxd_streamer_node* sn2=nxb_alloc_obj(req->nxb, sizeof(nxd_streamer_node));
  nxd_streamer_node_init(sn2);
  nxd_streamer_node* sn3=nxb_alloc_obj(req->nxb, sizeof(nxd_streamer_node));
  nxd_streamer_node_init(sn3);
  nxd_streamer_add_node(strm, sn1, 0);
  nxd_streamer_add_node(strm, sn2, 0);
  nxd_streamer_add_node(strm, sn3, 1);

  nxd_obuffer* ob1=nxb_alloc_obj(req->nxb, sizeof(nxd_obuffer));
  nxd_obuffer_init(ob1, "[test1]", sizeof("[test1]")-1);
  nxe_connect_streams(conn->tdata->loop, &ob1->data_out, &sn1->data_in);

  nxd_obuffer* ob2=nxb_alloc_obj(req->nxb, sizeof(nxd_obuffer));
  nxd_obuffer_init(ob2, "[test2]", sizeof("[test2]")-1);
  nxe_connect_streams(conn->tdata->loop, &ob2->data_out, &sn2->data_in);

  resp->sendfile_fd=open("www/root/index.htm", O_RDONLY|O_NONBLOCK);
  if (resp->sendfile_fd!=-1) {
    nxd_fbuffer* fb=nxb_alloc_obj(req->nxb, sizeof(nxd_fbuffer));
    nxd_fbuffer_init(fb, resp->sendfile_fd, 0, 15);
    nxe_connect_streams(conn->tdata->loop, &fb->data_out, &sn3->data_in);
  }
  else {
    nxweb_log_error("nxd_http_server_proto_start_sending_response(): can't open %s", resp->sendfile_path);
  }

  nxd_streamer_start(strm);

  resp->content_out=&strm->data_out;
  resp->content_length=sizeof("[test1]")-1+sizeof("[test2]")-1+15;

  return NXWEB_OK;
}

nxweb_handler subreq_handler={.on_request=subreq_on_request,
        .flags=NXWEB_HANDLE_ANY};

NXWEB_SET_HANDLER(subreq, "/subreq", &subreq_handler, .priority=100);
