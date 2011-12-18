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

#include "nxweb/nxweb.h"

static nxweb_result hello(nxweb_uri_handler_phase phase, nxweb_request *req) {
  if (phase!=NXWEB_PH_CONTENT) return NXWEB_OK;

  nxweb_set_response_content_type(req, "text/html");
  nxweb_response_make_room(req, req->content_length+2000); // optimize buffer allocation

  nxweb_response_append(req, "<html><head><title>Hello, nxweb!</title></head><body>");

  /// Special %-printf-conversions implemeted by nxweb:
  //    %H - html-escape string
  //    %U - url-encode string
  nxweb_response_printf(req, "<p>Received request:<br/>\n"
           "remote_addr=%H<br/>\n"
           "method=%H<br/>\n"
           "uri=%H<br/>\n"
           "path_info=%H<br/>\n"
           "http_version=%H<br/>\n"
           "http11=%d<br/>\n"
           "keep_alive=%d<br/>\n"
           "host=%H<br/>\n"
           "cookie=%H<br/>\n"
           "user_agent=%H<br/>\n"
           "content_type=%H<br/>\n"
           "content_length=%d<br/>\n"
           "transfer_encoding=%H<br/>\n"
           "request_body=%H</p>\n",
           NXWEB_REQUEST_CONNECTION(req)->remote_addr,
           req->method,
           req->uri,
           req->path_info,
           req->http_version,
           req->http11,
           req->keep_alive,
           req->host,
           req->cookie,
           req->user_agent,
           req->content_type,
           req->content_length,
           req->transfer_encoding,
           req->request_body
          );

  if (req->parameters) {
    nxweb_response_append(req, "<h3>Parameters:</h3>\n<ul>\n");
    int i;
    for (i=0; req->parameters[i].name; i++) {
      nxweb_response_printf(req, "<li>%H=%H</li>\n", req->parameters[i].name, req->parameters[i].value);
    }
    nxweb_response_append(req, "</ul>\n");
  }

  if (req->cookies) {
    nxweb_response_append(req, "<h3>Cookies:</h3>\n<ul>\n");
    int i;
    for (i=0; req->cookies[i].name; i++) {
      nxweb_response_printf(req, "<li>%H=%H</li>\n", req->cookies[i].name, req->cookies[i].value);
    }
    nxweb_response_append(req, "</ul>\n");
  }

  nxweb_response_append(req, "</body></html>");

  return NXWEB_OK;
}

static nxweb_result shutdown_server(nxweb_uri_handler_phase phase, nxweb_request *req) {
  if (phase!=NXWEB_PH_CONTENT) return NXWEB_OK;
  nxweb_set_response_content_type(req, "text/plain");
  nxweb_response_append(req, "bye");
  nxweb_shutdown();
  return NXWEB_OK;
}

static nxweb_result nxweb_on_server_startup() {
  // Whatever initialization code
  return NXWEB_OK;
}

static const nxweb_uri_handler hello_module_uri_handlers[] = {
  {"/hello", hello, NXWEB_INPROCESS|NXWEB_HANDLE_ANY|NXWEB_PARSE_PARAMETERS|NXWEB_PARSE_COOKIES},
  {"/shutdown", shutdown_server, NXWEB_INPROCESS|NXWEB_HANDLE_GET}, // server shutdown via http get; not good for real world
  {0, 0, 0}
};

/// Module definition
// List of active modules is maintained in modules.c file.

const nxweb_module hello_module = {
  .server_startup_callback=nxweb_on_server_startup,
  .uri_handlers=hello_module_uri_handlers
};
