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
#include <netdb.h>
#include <malloc.h>
#include <sys/mman.h>
#include <fcntl.h>

typedef struct nxweb_http_proxy_request_data {
  nxweb_http_server_connection* conn;
  nxd_http_proxy* hpx;
  nxe_subscriber proxy_events_sub;
  nxe_timer timer_backend;
  nxd_ibuffer ib;
  nxd_rbuffer rb_req;
  nxd_rbuffer rb_resp;
  int retry_count;
  char* rbuf;
  _Bool response_sending_started:1;
  _Bool proxy_request_complete:1;
  _Bool proxy_request_error:1;
} nxweb_http_proxy_request_data;

static void nxweb_http_server_proxy_events_sub_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data);

static const nxe_subscriber_class nxweb_http_server_proxy_events_sub_class={.on_message=nxweb_http_server_proxy_events_sub_on_message};

static void nxweb_http_proxy_request_finalize(nxd_http_server_proto* hsp, void* req_data) {

  nxweb_log_debug("nxweb_http_proxy_request_finalize");

  nxweb_http_proxy_request_data* rdata=req_data;
  nxweb_http_server_connection* conn=rdata->conn;
  nxe_loop* loop=conn->tdata->loop;
  nxe_unset_timer(loop, NXWEB_TIMER_BACKEND, &rdata->timer_backend);
  if (rdata->rb_resp.data_in.pair) nxe_disconnect_streams(rdata->rb_resp.data_in.pair, &rdata->rb_resp.data_in);
  if (rdata->rb_resp.data_out.pair) nxe_disconnect_streams(&rdata->rb_resp.data_out, rdata->rb_resp.data_out.pair);
  if (rdata->rb_req.data_in.pair) nxe_disconnect_streams(rdata->rb_req.data_in.pair, &rdata->rb_req.data_in);
  if (rdata->rb_req.data_out.pair) nxe_disconnect_streams(&rdata->rb_req.data_out, rdata->rb_req.data_out.pair);
  if (rdata->proxy_events_sub.pub) nxe_unsubscribe(rdata->proxy_events_sub.pub, &rdata->proxy_events_sub);
  if (rdata->hpx) {
    nxd_http_proxy_pool_return(rdata->hpx, rdata->proxy_request_error);
    rdata->hpx=0;
  }
  if (rdata->rbuf) {
    if (rdata->rbuf) nxp_free(conn->tdata->free_rbuf_pool, rdata->rbuf);
    rdata->rbuf=0;
  }
}

static nxweb_result start_proxy_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_proxy_request_data* rdata) {

  nxweb_log_debug("start_proxy_request");

  nxe_loop* loop=conn->tdata->loop;
  nxweb_handler* handler=conn->handler;
  assert(handler->idx>=0 && handler->idx<NXWEB_MAX_PROXY_POOLS);
  nxd_http_proxy* hpx=nxd_http_proxy_pool_connect(&conn->tdata->proxy_pool[handler->idx]);
  rdata->proxy_request_complete=0;
  rdata->proxy_request_error=0;
  rdata->response_sending_started=0;
  if (hpx) {
    rdata->hpx=hpx;
    nxweb_http_request* preq=nxd_http_proxy_prepare(hpx);
    if (handler->proxy_copy_host) preq->host=req->host;
    preq->method=req->method;
    preq->head_method=req->head_method;
    preq->content_length=req->content_length;
    preq->content_type=req->content_type;
    /// Do not forward Accept-Encoding header if you want to process results (eg SSI)
    // preq->accept_encoding=req->accept_encoding;
    preq->expect_100_continue=!!req->content_length;
    if (handler->uri) {
      const char* path_info=req->path_info? req->path_info : req->uri;
      if (*handler->uri) {
        char* uri=nxb_alloc_obj(conn->hsp.nxb, strlen(handler->uri)+strlen(path_info)+1);
        strcat(strcpy(uri, handler->uri), path_info);
        preq->uri=uri;
      }
      else {
        preq->uri=path_info;
      }
    }
    else {
      preq->uri=req->uri;
    }
    preq->http11=1;
    preq->keep_alive=1;
    preq->user_agent=req->user_agent;
    preq->cookie=req->cookie;
    preq->if_modified_since=req->if_modified_since + nxd_http_proxy_pool_get_backend_time_delta(hpx->pool);
    preq->x_forwarded_for=conn->remote_addr;
    preq->x_forwarded_host=req->host;
    preq->x_forwarded_ssl=nxweb_server_config.listen_config[conn->lconf_idx].secure;
    preq->uid=req->uid;
    preq->parent_req=req->parent_req;
    preq->headers=req->headers; // need to filter these???
    nxd_http_proxy_start_request(hpx, preq);
    nxe_init_subscriber(&rdata->proxy_events_sub, &nxweb_http_server_proxy_events_sub_class);
    nxe_subscribe(loop, &hpx->hcp.events_pub, &rdata->proxy_events_sub);
    nxd_rbuffer_init(&rdata->rb_resp, rdata->rbuf, NXWEB_RBUF_SIZE);
    nxe_connect_streams(loop, &hpx->hcp.resp_body_out, &rdata->rb_resp.data_in);

    if (req->content_length) { // receive body
      nxd_rbuffer_init(&rdata->rb_req, rdata->rbuf, NXWEB_RBUF_SIZE); // use same buffer area for request and response bodies, as they do not overlap in time
      conn->hsp.cls->connect_request_body_out(&conn->hsp, &rdata->rb_req.data_in);
      nxe_connect_streams(loop, &rdata->rb_req.data_out, &hpx->hcp.req_body_in);
      req->cdstate.monitor_only=1;

      conn->hsp.cls->start_receiving_request_body(&conn->hsp);
    }
    nxe_set_timer(loop, NXWEB_TIMER_BACKEND, &rdata->timer_backend);
    return NXWEB_OK;
  }
  else {
    nxweb_http_response* resp=&conn->hsp._resp;
    nxweb_send_http_error(resp, 502, "Bad Gateway");
    return NXWEB_ERROR;
  }
}

static void retry_proxy_request(nxweb_http_proxy_request_data* rdata) {

  nxweb_log_debug("retry_proxy_request");

  nxd_http_proxy* hpx=rdata->hpx;
  nxweb_http_server_connection* conn=rdata->conn;

  enum nxd_http_server_proto_state state=conn->hsp.state;
  assert(state==HSP_RECEIVING_HEADERS || state==HSP_RECEIVING_BODY || state==HSP_HANDLING);

  nxd_http_proxy_pool_return(hpx, 1);
  if (rdata->rb_resp.data_in.pair) nxe_disconnect_streams(rdata->rb_resp.data_in.pair, &rdata->rb_resp.data_in);
  if (rdata->rb_resp.data_out.pair) nxe_disconnect_streams(&rdata->rb_resp.data_out, rdata->rb_resp.data_out.pair);
  if (rdata->rb_req.data_in.pair) nxe_disconnect_streams(rdata->rb_req.data_in.pair, &rdata->rb_req.data_in);
  if (rdata->rb_req.data_out.pair) nxe_disconnect_streams(&rdata->rb_req.data_out, rdata->rb_req.data_out.pair);
  if (rdata->proxy_events_sub.pub) nxe_unsubscribe(rdata->proxy_events_sub.pub, &rdata->proxy_events_sub);
  rdata->hpx=0;
  rdata->retry_count++;
  start_proxy_request(conn, &conn->hsp.req, rdata);
}

static void fail_proxy_request(nxweb_http_proxy_request_data* rdata) {

  nxweb_log_debug("fail_proxy_request");

  nxweb_http_server_connection* conn=rdata->conn;
  if (rdata->response_sending_started) {
    nxweb_http_server_connection_finalize(conn, 0);
  }
  else {
    nxweb_http_response* resp=&conn->hsp._resp;
    nxweb_send_http_error(resp, 504, "Gateway Timeout");
    nxweb_start_sending_response(conn, resp);
    rdata->response_sending_started=1;
    rdata->proxy_request_complete=1; // ignore further backend errors
  }
}

static void timer_backend_on_timeout(nxe_timer* timer, nxe_data data) {

  nxweb_log_debug("timer_backend_on_timeout");

  nxweb_http_proxy_request_data* rdata=(nxweb_http_proxy_request_data*)((char*)timer-offsetof(nxweb_http_proxy_request_data, timer_backend));
  nxweb_http_server_connection* conn=rdata->conn;
  if (rdata->hpx->hcp.req_body_sending_started || rdata->response_sending_started) {
    // backend did respond in time (headers or 100-continue) but request is not done yet
    // do nothing, continue processing until parent connection times out
    nxweb_log_warning("backend connection %p timeout; backend responded; post=%d resp=%d", conn, (int)rdata->hpx->hcp.req_body_sending_started, (int)rdata->response_sending_started);
  }
  else if (rdata->retry_count>=NXWEB_PROXY_RETRY_COUNT) {
    nxweb_log_error("backend connection %p timeout; retry count exceeded", conn);
    fail_proxy_request(rdata);
  }
  else {
    nxweb_log_info("backend connection %p timeout; retrying", conn);
    retry_proxy_request(rdata);
  }
}

static const nxe_timer_class timer_backend_class={.on_timeout=timer_backend_on_timeout};

static nxweb_result nxweb_http_proxy_handler_on_headers(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {

  nxweb_log_debug("nxweb_http_proxy_handler_on_headers");

  nxweb_http_proxy_request_data* rdata=nxb_alloc_obj(conn->hsp.nxb, sizeof(nxweb_http_proxy_request_data));
  nxe_loop* loop=conn->tdata->loop;
  memset(rdata, 0, sizeof(nxweb_http_proxy_request_data));
  rdata->conn=conn;
  conn->hsp.req_data=rdata;
  conn->hsp.req_finalize=nxweb_http_proxy_request_finalize;
  rdata->rbuf=nxp_alloc(conn->tdata->free_rbuf_pool);
  rdata->timer_backend.super.cls.timer_cls=&timer_backend_class;
  return start_proxy_request(conn, req, rdata);
}

static nxweb_result proxy_generate_cache_key(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_OK; // do not cache POST requests, etc.
  nxb_start_stream(req->nxb);
  _nxb_append_encode_file_path(req->nxb, req->host);
  if (conn->secure) nxb_append_str(req->nxb, "_s");
  _nxb_append_encode_file_path(req->nxb, req->uri);
  nxb_append_char(req->nxb, '\0');
  resp->cache_key=nxb_finish_stream(req->nxb, 0);
  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(http_proxy, .on_headers=nxweb_http_proxy_handler_on_headers,
        .on_generate_cache_key=proxy_generate_cache_key,
        .flags=NXWEB_HANDLE_ANY|NXWEB_ACCEPT_CONTENT);

static void nxweb_http_server_proxy_events_sub_on_message(nxe_subscriber* sub, nxe_publisher* pub, nxe_data data) {

  nxweb_log_debug("nxweb_http_server_proxy_events_sub_on_message");

  nxweb_http_proxy_request_data* rdata=(nxweb_http_proxy_request_data*)((char*)sub-offsetof(nxweb_http_proxy_request_data, proxy_events_sub));
  nxweb_http_server_connection* conn=rdata->conn;
  nxe_loop* loop=sub->super.loop;
  if (data.i==NXD_HCP_RESPONSE_RECEIVED) {
    nxe_unset_timer(loop, NXWEB_TIMER_BACKEND, &rdata->timer_backend);
    //nxweb_http_request* req=&conn->hsp.req;
    nxd_http_proxy* hpx=rdata->hpx;
    nxweb_http_response* presp=&hpx->hcp.resp;
    nxweb_http_response* resp=&conn->hsp._resp;
    resp->status=presp->status;
    resp->status_code=presp->status_code;
    resp->content_type=presp->content_type;
    resp->content_length=presp->content_length;
    if (resp->content_length<0) resp->chunked_autoencode=1; // re-encode chunked or until-close content
    resp->ssi_on=presp->ssi_on;
    resp->templates_on=presp->templates_on;
    resp->headers=presp->headers;
    time_t backend_time_delta=presp->date? presp->date-nxe_get_current_http_time(loop) : 0;
    nxd_http_proxy_pool_report_backend_time_delta(hpx->pool, backend_time_delta);
    backend_time_delta=nxd_http_proxy_pool_get_backend_time_delta(hpx->pool);
    resp->date=presp->date? presp->date-backend_time_delta : 0;
    resp->last_modified=presp->last_modified? presp->last_modified-backend_time_delta : 0;
    resp->expires=presp->expires? presp->expires-backend_time_delta : 0;
    resp->cache_control=presp->cache_control;
    resp->max_age=presp->max_age;
    resp->no_cache=presp->no_cache;
    resp->cache_private=presp->cache_private;
    resp->content_out=&rdata->rb_resp.data_out;
    nxweb_start_sending_response(conn, resp);
    rdata->response_sending_started=1;
    nxweb_server_config.access_log_on_proxy_response(&conn->hsp.req, hpx, presp);
    //nxweb_log_error("proxy request [%d] start sending response", conn->hpx->hcp.request_count);
  }
  else if (data.i==NXD_HCP_REQUEST_COMPLETE) {
    rdata->proxy_request_complete=1;
    //nxweb_log_error("proxy request [%d] complete", conn->hpx->hcp.request_count);
  }
  else if (data.i<0) {
    rdata->proxy_request_error=1;
    if (/*rdata->hpx->hcp.request_complete ||*/ rdata->proxy_request_complete) {
      nxweb_log_warning("proxy request conn=%p rc=%d retry=%d error=%d; error after request_complete; ignored", conn, rdata->hpx->hcp.request_count, rdata->retry_count, data.i);
      // ignore errors after request_complete; keep connection running
    }
    else if (rdata->response_sending_started) {
      nxweb_log_error("proxy request conn=%p rc=%d retry=%d error=%d; error while response_sending_started but backend request not complete; request failed",
                      conn, rdata->hpx->hcp.request_count, rdata->retry_count, data.i);
      // ignore errors after response_sending_started; keep connection running
      fail_proxy_request(rdata);
    }
    else {
      nxe_unset_timer(loop, NXWEB_TIMER_BACKEND, &rdata->timer_backend);
      if (rdata->retry_count>=NXWEB_PROXY_RETRY_COUNT || rdata->hpx->hcp.req_body_sending_started /*|| rdata->response_sending_started*/) {
        nxweb_log_error("proxy request conn=%p rc=%d retry=%d error=%d; failed", conn, rdata->hpx->hcp.request_count, rdata->retry_count, data.i);
        fail_proxy_request(rdata);
      }
      else {
        if (rdata->retry_count || !(data.i==NXE_ERROR || data.i==NXE_HUP || data.i==NXE_RDHUP || data.i==NXE_RDCLOSED)) {
          nxweb_log_warning("proxy request conn=%p rc=%d retry=%d error=%d; retrying", conn, rdata->hpx->hcp.request_count, rdata->retry_count, data.i);
        }
        retry_proxy_request(rdata);
      }
    }
  }
}
