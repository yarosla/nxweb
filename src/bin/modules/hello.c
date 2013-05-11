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

static nxweb_result hello_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  nxweb_response_append_str(resp, "<html><head><title>Hello, nxweb!</title></head><body>");

  /// Special %-printf-conversions implemeted by nxweb:
  //    %H - html-escape string
  //    %U - url-encode string
  nxweb_response_printf(resp, "<p>Received request:</p>\n<blockquote>"
           "request_id=%016" PRIx64 "<br/>\n"
           "remote_addr=%H<br/>\n"
           "method=%H<br/>\n"
           "uri=%H<br/>\n"
           "path_info=%H<br/>\n"
           "http_version=%H<br/>\n"
           "http11=%d<br/>\n"
           "ssl=%d<br/>\n"
           "keep_alive=%d<br/>\n"
           "host=%H<br/>\n"
           "cookie=%H<br/>\n"
           "user_agent=%H<br/>\n"
           "content_type=%H<br/>\n"
           "content_length=%ld<br/>\n"
           "content_received=%ld<br/>\n"
           "transfer_encoding=%H<br/>\n"
           "accept_encoding=%H<br/>\n"
           "request_body=%H</blockquote>\n",
           req->uid,
           conn->remote_addr,
           req->method,
           req->uri,
           req->path_info,
           req->http_version,
           req->http11,
           conn->secure,
           req->keep_alive,
           req->host,
           req->cookie,
           req->user_agent,
           req->content_type,
           req->content_length,
           req->content_received,
           req->transfer_encoding,
           req->accept_encoding,
           req->content
          );

  if (req->headers) {
    nxweb_response_append_str(resp, "<h3>Headers:</h3>\n<ul>\n");
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->headers); itr; itr=nx_simple_map_itr_next(itr)) {
      nxweb_response_printf(resp, "<li>%H=%H</li>\n", itr->name, itr->value);
    }
    nxweb_response_append_str(resp, "</ul>\n");
  }

  if (req->parameters) {
    nxweb_response_append_str(resp, "<h3>Parameters:</h3>\n<ul>\n");
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->parameters); itr; itr=nx_simple_map_itr_next(itr)) {
      nxweb_response_printf(resp, "<li>%H=%H</li>\n", itr->name, itr->value);
    }
    nxweb_response_append_str(resp, "</ul>\n");
  }

  if (req->cookies) {
    nxweb_response_append_str(resp, "<h3>Cookies:</h3>\n<ul>\n");
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->cookies); itr; itr=nx_simple_map_itr_next(itr)) {
      nxweb_response_printf(resp, "<li>%H=%H</li>\n", itr->name, itr->value);
    }
    nxweb_response_append_str(resp, "</ul>\n");
  }

  nxweb_response_append_str(resp, "</body></html>");

  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(hello, .on_request=hello_on_request,
        .flags=NXWEB_HANDLE_ANY|NXWEB_PARSE_PARAMETERS|NXWEB_PARSE_COOKIES);
