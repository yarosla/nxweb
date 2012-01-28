/*
 * Copyright (c) 2011 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "../nxweb/nxweb.h"

static nxweb_result hello_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  nxweb_response_append_str(resp, "<html><head><title>Hello, nxweb!</title></head><body>");

  /// Special %-printf-conversions implemeted by nxweb:
  //    %H - html-escape string
  //    %U - url-encode string
  nxweb_response_printf(resp, "<p>Received request:</p>\n<blockquote>"
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
           "content_length=%d<br/>\n"
           "transfer_encoding=%H<br/>\n"
           "accept_encoding=%H<br/>\n"
           "request_body=%H</blockquote>\n",
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
           req->transfer_encoding,
           req->accept_encoding,
           req->content
          );

  if (req->parameters) {
    nxweb_response_append_str(resp, "<h3>Parameters:</h3>\n<ul>\n");
    int i;
    for (i=0; req->parameters[i].name; i++) {
      nxweb_response_printf(resp, "<li>%H=%H</li>\n", req->parameters[i].name, req->parameters[i].value);
    }
    nxweb_response_append_str(resp, "</ul>\n");
  }

  if (req->cookies) {
    nxweb_response_append_str(resp, "<h3>Cookies:</h3>\n<ul>\n");
    int i;
    for (i=0; req->cookies[i].name; i++) {
      nxweb_response_printf(resp, "<li>%H=%H</li>\n", req->cookies[i].name, req->cookies[i].value);
    }
    nxweb_response_append_str(resp, "</ul>\n");
  }

  nxweb_response_append_str(resp, "</body></html>");

  return NXWEB_OK;
}

nxweb_handler hello_handler={.on_request=hello_on_request,
        .flags=NXWEB_HANDLE_ANY|NXWEB_PARSE_PARAMETERS|NXWEB_PARSE_COOKIES};
