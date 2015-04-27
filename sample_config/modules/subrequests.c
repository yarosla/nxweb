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

#include <fcntl.h>

static nxweb_result subreq_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  nxweb_composite_stream* cs=nxweb_composite_stream_init(conn, req);
  nxweb_composite_stream_append_bytes(cs, "[test1]", sizeof("[test1]")-1);
  int fd=open("www/root/index.htm", O_RDONLY|O_NONBLOCK);
  nxweb_composite_stream_append_fd(cs, fd, 0, 15); // fd will be auto-closed
  nxweb_composite_stream_append_subrequest(cs, 0, "/8777/");
  nxweb_composite_stream_append_bytes(cs, "[test2]", sizeof("[test2]")-1);
  nxweb_composite_stream_close(cs);

  nxweb_composite_stream_start(cs, resp);

  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(subreq, .on_request=subreq_on_request, .flags=NXWEB_HANDLE_ANY);

static nxweb_result curtime_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  resp->last_modified=nxe_get_current_http_time(conn->tdata->loop);
  const char* max_age_str=nxweb_get_request_parameter(req, "max_age");
  int max_age=0;
  if (max_age_str) {
    max_age=atoi(max_age_str);
    resp->max_age=max_age;
  }

  nxweb_response_append_str(resp, nxe_get_current_http_time_str(conn->tdata->loop));
  nxweb_response_append_str(resp, " [max-age=");
  nxweb_response_append_uint(resp, max_age);
  nxweb_response_append_str(resp, "]");

  return NXWEB_OK;
}

static nxweb_result curtime_generate_cache_key(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_OK; // do not cache POST requests, etc.
  nxb_start_stream(req->nxb);
  _nxb_append_encode_file_path(req->nxb, req->host);
  if (conn->secure) nxb_append_str(req->nxb, "_s");
  _nxb_append_encode_file_path(req->nxb, req->uri);
  nxb_append_char(req->nxb, '\0');
  resp->cache_key=nxb_finish_stream(req->nxb, 0);
  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(curtime, .on_request=curtime_on_request,
        .on_generate_cache_key=curtime_generate_cache_key,
        .flags=NXWEB_HANDLE_GET|NXWEB_PARSE_PARAMETERS);


#ifdef WITH_IMAGEMAGICK

static nxweb_result captcha_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");
  nxweb_add_response_header(resp, "X-NXWEB-Draw", "7425");
  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(captcha, .on_request=captcha_on_request,
        .flags=NXWEB_HANDLE_GET|NXWEB_PARSE_PARAMETERS);

#endif
