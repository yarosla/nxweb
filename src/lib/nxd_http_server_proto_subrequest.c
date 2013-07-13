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

void _nxweb_call_request_finalizers(nxd_http_server_proto* hsp);

static void request_cleanup(nxe_loop* loop, nxd_http_server_proto* hsp) {

  nxweb_log_debug("subrequest_cleanup");

  // subrequest connections do not reuse requests
  // so no cleanup is strictly required
  // it will get finalized anyway
  // but perhaps we can free resources earlier here
  // not waiting for parent request to complete
  _nxweb_call_request_finalizers(hsp);
  nxd_fbuffer_finalize(&hsp->fb);
  if (hsp->resp && hsp->resp->sendfile_fd) {
    close(hsp->resp->sendfile_fd);
  }
  nxb_empty(hsp->nxb);
  nxp_free(hsp->nxb_pool, hsp->nxb);
  hsp->nxb=0;
}

static void request_complete(nxe_loop* loop, nxd_http_server_proto* hsp) {
  if (hsp->resp_body_in.pair) nxe_disconnect_streams(hsp->resp_body_in.pair, &hsp->resp_body_in);
  if (hsp->req_body_out.pair) nxe_disconnect_streams(&hsp->req_body_out, hsp->req_body_out.pair);
  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_COMPLETE);
}

static void subrequest_finalize(nxd_http_server_proto* hsp) {
  _nxweb_call_request_finalizers(hsp);
  nxe_loop* loop=hsp->events_pub.super.loop;
  while (hsp->events_pub.sub) nxe_unsubscribe(&hsp->events_pub, hsp->events_pub.sub);
  nxd_fbuffer_finalize(&hsp->fb);
  if (hsp->resp && hsp->resp->sendfile_fd) close(hsp->resp->sendfile_fd);
  if (hsp->nxb) {
    nxb_empty(hsp->nxb);
    nxp_free(hsp->nxb_pool, hsp->nxb);
  }
}

static void subrequest_start_sending_response(nxd_http_server_proto* hsp, nxweb_http_response* resp) {
  if (hsp->state!=HSP_RECEIVING_HEADERS && hsp->state!=HSP_RECEIVING_BODY && hsp->state!=HSP_HANDLING) {
    nxweb_log_error("illegal state for start_sending_response()");
    return;
  }

  nxweb_log_debug("subrequest_start_sending_response");

  nxweb_http_request* req=&hsp->req;
  hsp->resp=resp;
  nxe_loop* loop=hsp->events_pub.super.loop;
  if (!resp->nxb) resp->nxb=hsp->nxb;

  nxd_http_server_proto_setup_content_out(hsp, resp);

  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_RESPONSE_READY);

  if (!resp->content_out && resp->content_length) {
    nxweb_log_error("nxd_http_server_proto_subrequest_start_sending_response(): no content_out stream");
  }

  if (!resp->raw_headers) _nxweb_prepare_response_headers(loop, resp);

  hsp->state=HSP_SENDING_HEADERS;
}

static void subrequest_connect_request_body_out(nxd_http_server_proto* hsp, nxe_ostream* is) {

  nxweb_log_debug("subrequest_connect_request_body_out");

  nxe_connect_streams(hsp->events_pub.super.loop, &hsp->ob.data_out, is);
}

static nxe_ostream* subrequest_get_request_body_out_pair(nxd_http_server_proto* hsp) {
  return hsp->ob.data_out.pair;
}

static void subrequest_start_receiving_request_body(nxd_http_server_proto* hsp) {
  assert(0); // POST requests not supported for subrequests
}

static const nxd_http_server_proto_class subrequest_class={
  .finalize=subrequest_finalize,
  .start_sending_response=subrequest_start_sending_response,
  .start_receiving_request_body=subrequest_start_receiving_request_body,
  .connect_request_body_out=subrequest_connect_request_body_out,
  .get_request_body_out_pair=subrequest_get_request_body_out_pair,
  .request_cleanup=request_cleanup
};

void nxd_http_server_proto_subrequest_init(nxd_http_server_proto* hsp, nxp_pool* nxb_pool) {
  memset(hsp, 0, sizeof(nxd_http_server_proto));
  hsp->cls=&subrequest_class;
  hsp->nxb_pool=nxb_pool;
  hsp->events_pub.super.cls.pub_cls=NXE_PUB_DEFAULT;
  hsp->state=HSP_WAITING_FOR_REQUEST;
}

void nxweb_http_server_proto_subrequest_execute(nxd_http_server_proto* hsp, const char* host, const char* uri, nxweb_http_request* parent_req) {

  nxweb_log_debug("nxweb_http_server_proto_subrequest_execute %s %s", host, uri);

  hsp->nxb=nxp_alloc(hsp->nxb_pool);
  nxb_init(hsp->nxb, NXWEB_CONN_NXB_SIZE);
  hsp->state=HSP_RECEIVING_HEADERS;
  nxweb_http_request* req=&hsp->req;
  req->nxb=hsp->nxb;
  req->parent_req=parent_req;
  req->uid=nxweb_generate_unique_id();
  req->method="GET";
  req->get_method=1;
  req->host=host;
  req->http11=1;
  //req->http_version=;
  req->uri=uri;
  hsp->headers_bytes_received=1;
  hsp->resp=_nxweb_http_response_init(&hsp->_resp, hsp->nxb, &hsp->req);
  nxe_publish(&hsp->events_pub, (nxe_data)NXD_HSP_REQUEST_RECEIVED);
}
