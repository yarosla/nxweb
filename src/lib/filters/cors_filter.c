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

/*
 * Cross-Origin Resource Sharing (CORS)
 */


typedef struct nxweb_filter_cors {
  nxweb_filter base;
  const char** allow_hosts;
  const char* max_age;
  _Bool allow_credentials:1;
} nxweb_filter_cors;

typedef struct cors_filter_data {
  nxweb_filter_data fdata;
  int input_fd;
} cors_filter_data;

static nxweb_filter_data* cors_init(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(cors_filter_data));
  return fdata;
}

static void cors_finalize(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  cors_filter_data* cfdata=(cors_filter_data*)fdata;
  if (cfdata->input_fd) {
    close(cfdata->input_fd);
    cfdata->input_fd=0;
  }
}

static _Bool is_origin_allowed(const char* origin, const char* const* allowed_hosts) {
  if (!*allowed_hosts) return 1; // empty allowed host list means ANY HOST IS ALLOWED

  // skip scheme
  const char* origin_host=strchr(origin, '/');
  if (!origin_host || origin_host[1]!='/') return 0;
  origin_host+=2;
  // cut port number
  const char* p=strchr(origin_host, ':');
  int origin_host_len=p? p-origin_host : strlen(origin_host);

  const char* const* h;
  for (h=allowed_hosts; *h; h++) {
    if (nxweb_vhost_match(origin_host, origin_host_len, *h, strlen(*h))) // matches
      return 1;
  }
  return 0;
}

static nxweb_result cors_do_filter(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  if (resp->status_code && resp->status_code!=200 && resp->status_code!=404) return NXWEB_OK;
  const char* origin=nxweb_get_request_header(req, "Origin");
  if (!origin || !*origin) return NXWEB_OK; // not cross-origin request

  _Bool options_method=!nx_strcasecmp(req->method, "OPTIONS");
  nxweb_filter_cors* cors_filter=(nxweb_filter_cors*)filter;

  if (is_origin_allowed(origin, cors_filter->allow_hosts)) {
    cors_filter_data* cfdata=(cors_filter_data*)fdata;

    if (options_method) {
      // replace response with empty one
      resp->status_code=200;
      resp->ssi_on=0;
      resp->templates_on=0;
      resp->gzip_encoded=0;
      resp->chunked_encoding=0;
      resp->chunked_autoencode=0;
      resp->no_cache=1;
      resp->expires=0;
      resp->max_age=0;
      // reset previous response content
      resp->sendfile_path=0;
      if (resp->sendfile_fd) {
        // save it to close on finalize
        cfdata->input_fd=resp->sendfile_fd;
        resp->sendfile_fd=0;
      }
      //resp->content_type="application/octet-stream";
      resp->content_length=0;
      resp->content=0;
      resp->last_modified=0;
    }

    const char* methods=nxweb_get_request_header(req, "Access-Control-Request-Method");
    const char* headers=nxweb_get_request_header(req, "Access-Control-Request-Headers");

    nxweb_add_response_header(resp, "Access-Control-Allow-Origin", origin);
    if (options_method) {
      if (methods)
        nxweb_add_response_header(resp, "Access-Control-Allow-Methods", methods);
      nxweb_add_response_header(resp, "Access-Control-Allow-Headers", headers? headers:"content-type");
      if (cors_filter->allow_credentials)
        nxweb_add_response_header(resp, "Access-Control-Allow-Credentials", "true");
      if (cors_filter->max_age)
        nxweb_add_response_header(resp, "Access-Control-Max-Age", cors_filter->max_age);
    }
  }

  return NXWEB_OK;
}

static nxweb_filter* cors_config(nxweb_filter* base, const nx_json* json) {
  nxweb_filter_cors* f=calloc(1, sizeof(nxweb_filter_cors)); // NOTE this will never be freed
  *f=*(nxweb_filter_cors*)base;
  const nx_json* allow_hosts_json=nx_json_get(json, "allow_hosts"); // list of hosts to allow CORS
  if (allow_hosts_json->type!=NX_JSON_NULL) {
    const char** allow_hosts=calloc(allow_hosts_json->length+1, sizeof(char*));
    int i;
    const char** ph=allow_hosts;
    for (i=0; i<allow_hosts_json->length; i++) {
      const char* host=nx_json_item(allow_hosts_json, i)->text_value;
      if (!host || !*host) continue;
      *ph++=host;
    }
    f->allow_hosts=allow_hosts;
  }
  const char* max_age=nx_json_get(json, "max_age")->text_value;
  if (max_age) f->max_age=max_age;
  f->allow_credentials=!!nx_json_get(json, "allow_credentials")->int_value;
  return (nxweb_filter*)f;
}

static nxweb_filter_cors cors_filter={.base={
        .config=cors_config,
        .init=cors_init, .finalize=cors_finalize,
        .do_filter=cors_do_filter}};

NXWEB_DEFINE_FILTER(cors, cors_filter.base);
