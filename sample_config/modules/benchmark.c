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
  if (nxweb_get_request_parameter(req, "core")) {
    nxweb_log_error("initiating segfault");
    char* p=0;
    *p='!';
  }

  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(benchmark_inprocess, .on_request=benchmark_on_request, .flags=NXWEB_HANDLE_GET);
NXWEB_DEFINE_HANDLER(benchmark_inworker, .on_request=benchmark_inworker_on_request, .flags=NXWEB_HANDLE_GET|NXWEB_INWORKER);
NXWEB_DEFINE_HANDLER(test, .on_request=test_on_request, .flags=NXWEB_HANDLE_GET|NXWEB_HANDLE_POST|NXWEB_PARSE_PARAMETERS);
