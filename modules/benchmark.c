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

#include <malloc.h>
#include <fcntl.h>
#include <pthread.h>

static nxweb_result benchmark_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  resp->content_type="text/html";
  resp->content="<p>Hello, world!</p>";
  resp->content_length=sizeof("<p>Hello, world!</p>")-1;
  return NXWEB_OK;
}

static nxweb_result benchmark_inworker_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  resp->content_type="text/html";
  resp->content="<p>Hello, world!</p>";
  resp->content_length=sizeof("<p>Hello, world!</p>")-1;

/*
  cpu_set_t cpuset;
  pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  nxweb_log_error("inworker %d %d %d %d", CPU_ISSET(0, &cpuset), CPU_ISSET(1, &cpuset), CPU_ISSET(2, &cpuset), CPU_ISSET(3, &cpuset));
*/

  return NXWEB_OK;
}

static nxweb_result test_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");
  nxweb_response_append_str(resp, "<html><head><title>nxweb test</title></head><body>TEST</body></html>");

  if (nxweb_get_request_parameter(req, "mstats")) {
    nxweb_log_error("malloc_stats()");
    malloc_stats();
  }
  if (nxweb_get_request_parameter(req, "trim")) {
    nxweb_log_error("malloc_trim(1024)");
    malloc_trim(1024);
    malloc_stats();
  }

  return NXWEB_OK;
}

nxweb_handler benchmark_handler={.on_request=benchmark_on_request, .flags=NXWEB_HANDLE_GET};
nxweb_handler benchmark_handler_inworker={.on_request=benchmark_inworker_on_request, .flags=NXWEB_HANDLE_GET|NXWEB_INWORKER};
nxweb_handler test_handler={.on_request=test_on_request, .flags=NXWEB_HANDLE_GET|NXWEB_HANDLE_POST|NXWEB_PARSE_PARAMETERS};
