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

#include <malloc.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <utime.h>

#ifdef WITH_NETTLE
#include <nettle/sha.h>
#else
#include "deps/sha1-c/sha1.h"
#endif

typedef struct fc_filter_data {
  nxweb_filter_data fdata;
  _Bool added_ims:1; // If-Modified-Since header has been added by this filter
} fc_filter_data;

static nxweb_filter_data* fc_init(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(fc_filter_data));
  return fdata;
}

static nxweb_result fc_translate_cache_key(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  // only accept virtual key ( ) => make it NXFC-key (*)
}

static nxweb_result fc_serve_from_cache(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {

}

static nxweb_result fc_do_filter(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {

}

nxweb_filter file_cache_filter={.name="file_cache", .init=fc_init, .translate_cache_key=fc_translate_cache_key,
        .serve_from_cache=fc_serve_from_cache, .do_filter=fc_do_filter};
