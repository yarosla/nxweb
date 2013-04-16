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

static nxweb_result host_redirect_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (req->parent_req) // this is subrequest
    return NXWEB_NEXT;

  const char* host_wanted=conn->handler->host;
  assert(host_wanted); // host must be set to desired host and port

  if (req->host && !strcmp(req->host, host_wanted)) // this is what we want
    return NXWEB_NEXT; // pass to next handler

  resp->host=host_wanted;
  nxweb_send_redirect2(resp, 301, req->uri, 0, conn->secure);
  return NXWEB_ERROR;
}

nxweb_handler nxweb_host_redirect_handler={.on_select=host_redirect_on_select,
        .flags=NXWEB_HANDLE_ANY};
