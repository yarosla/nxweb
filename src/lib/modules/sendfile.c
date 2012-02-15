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

#include <unistd.h>
#include <pthread.h>
#include <sys/fcntl.h>

#define MAX_PATH 1024

static nxweb_result sendfile_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_NEXT; // do not respond to POST requests, etc.

  nxweb_handler* handler=conn->handler;
  const char* document_root=handler->dir;
  assert(document_root);

  assert(resp->cache_key);
  const char* fpath=resp->cache_key;
  int rlen=resp->cache_key_root_len;
  const char* path_info=fpath+rlen;

  if (stat(fpath, &resp->sendfile_info)==-1) {
    return NXWEB_NEXT;
  }

  if (S_ISDIR(resp->sendfile_info.st_mode)) {
    nxweb_send_redirect2(resp, 302, path_info, "/");
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
/*
  if (!S_ISREG(finfo.st_mode)) { // symlink or ...?
    nxweb_send_http_error(resp, 403, "Forbidden");
    nxweb_start_sending_response(conn, resp);
    return NXWEB_ERROR;
  }
*/

  if (req->if_modified_since && resp->sendfile_info.st_mtime<=req->if_modified_since) {
    resp->status_code=304;
    resp->status="Not Modified";
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }

  int result=nxweb_send_file(resp, (char*)fpath, rlen, &resp->sendfile_info, 0, 0, 0, resp->mtype, handler->charset);
  if (result!=0) { // should not happen
    nxweb_log_error("sendfile: [%s] stat() was OK, but open() failed", fpath);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    return NXWEB_ERROR;
  }

  nxweb_start_sending_response(conn, resp);
  return NXWEB_OK;
}

static nxweb_result sendfile_generate_cache_key(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_NEXT; // do not respond to POST requests, etc.

  nxweb_handler* handler=conn->handler;
  const char* document_root=handler->dir;
  assert(document_root);
  assert(handler->index_file);

  char fpath[MAX_PATH];
  int rlen=strlen(document_root);
  assert(rlen<sizeof(fpath));
  strcpy(fpath, document_root);
  char* path_info=fpath+rlen;
  const char* q=strchr(req->path_info, '?');
  int plen=q? q-req->path_info : strlen(req->path_info);
  if (rlen+plen>sizeof(fpath)-64) { // leave room for index file name etc.
    nxweb_send_http_error(resp, 414, "Request-URI Too Long");
    return NXWEB_ERROR;
  }
  strncat(path_info, req->path_info, plen);
  nxweb_url_decode(path_info, 0);
  plen=strlen(path_info);
  if (plen>0 && path_info[plen-1]=='/') { // directory index
    strcat(path_info+plen, handler->index_file);
  }

  if (nxweb_remove_dots_from_uri_path(path_info)) {
    //nxweb_send_http_error(resp, 404, "Not Found");
    return NXWEB_NEXT;
  }
  resp->cache_key=nxb_copy_obj(req->nxb, fpath, strlen(fpath)+1);
  resp->cache_key_root_len=rlen;
  return NXWEB_OK;
}

static nxweb_result sendfile_on_serve_from_cache(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_NEXT; // do not respond to POST requests, etc.

  struct stat* finfo=&resp->sendfile_info;

  if (S_ISDIR(finfo->st_mode)) {
    nxweb_send_redirect2(resp, 302, resp->cache_key+resp->cache_key_root_len, "/");
    return NXWEB_OK;
  }
/*
  if (!S_ISREG(finfo->st_mode)) { // symlink or ...?
    nxweb_send_http_error(resp, 403, "Forbidden");
    return NXWEB_OK;
  }
*/

  int result=nxweb_send_file(resp, (char*)resp->cache_key, resp->cache_key_root_len, finfo, 0, 0, 0, resp->mtype, conn->handler->charset);
  if (result!=0) { // should not happen
    nxweb_log_error("sendfile: [%s] stat() was OK, but open() failed", resp->cache_key);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    return NXWEB_ERROR;
  }

  return NXWEB_OK;
}

nxweb_handler sendfile_handler={.on_select=sendfile_on_select,
        .on_generate_cache_key=sendfile_generate_cache_key, .on_serve_from_cache=sendfile_on_serve_from_cache,
        .flags=NXWEB_HANDLE_GET};
