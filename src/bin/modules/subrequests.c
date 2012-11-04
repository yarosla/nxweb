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

  nxweb_composite_stream* cs=nxweb_composite_stream_init(conn, req);
  nxweb_composite_stream_append_bytes(cs, "[test1]", sizeof("[test1]")-1);
  int fd=open("www/root/index.htm", O_RDONLY|O_NONBLOCK);
  nxweb_composite_stream_append_fd(cs, fd, 0, 15); // fd will be auto-closed
  nxweb_composite_stream_append_subrequest(cs, 0, "/8777/");
  nxweb_composite_stream_append_bytes(cs, "[test2]", sizeof("[test2]")-1);

  nxweb_composite_stream_start(cs, resp);

  return NXWEB_OK;
}

NXWEB_HANDLER(subreq, "/subreq", .on_request=subreq_on_request,
        .flags=NXWEB_HANDLE_ANY, .priority=100);
