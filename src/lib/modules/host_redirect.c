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

  const char* host=req->host;
  const char* host_wanted=conn->handler->host;
  assert(host_wanted); // host must be set to desired host and port

  if (host && !strcmp(host, host_wanted)) // this is what we want
    return NXWEB_NEXT; // pass to next handler

  if (host && conn->handler->config.cptr) { // this is what we pass
    const char* const* ph;
    for (ph=conn->handler->config.cptr; *ph; ph++) {
      if (!strcmp(host, *ph)) // matches
        return NXWEB_NEXT; // pass to next handler
    }
  }

  resp->host=host_wanted;
  nxweb_send_redirect2(resp, 301, req->uri, 0, conn->secure);
  return NXWEB_ERROR;
}

static nxweb_result host_redirect_on_config(nxweb_handler* handler, const struct nx_json* json) {
  const nx_json* pass_hosts_json=nx_json_get(json, "pass"); // list of domains to pass (no redirect)
  if (pass_hosts_json->type!=NX_JSON_NULL) {
    const char** pass_hosts=calloc(pass_hosts_json->length+1, sizeof(char*));
    int i;
    const char** ph=pass_hosts;
    for (i=0; i<pass_hosts_json->length; i++) {
      const char* host=nx_json_item(pass_hosts_json, i)->text_value;
      if (!host || !*host) continue;
      *ph++=host;
    }
    handler->config.ptr=pass_hosts;
  }
  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(host_redirect, .on_select=host_redirect_on_select, .on_config=host_redirect_on_config,
        .flags=NXWEB_HANDLE_ANY);
