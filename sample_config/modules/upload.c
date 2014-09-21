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

#include <fcntl.h>

#define MAX_UPLOAD_SIZE 16000

static const char upload_handler_key; // variable's address only matters
#define UPLOAD_HANDLER_KEY ((nxe_data)&upload_handler_key)

static void upload_request_data_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data) {
  nxd_fwbuffer* fwb=data.ptr;
  if (fwb && fwb->fd) {
    close(fwb->fd);
    fwb->fd=0;
  }
}

static nxweb_result upload_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  nxweb_response_append_str(resp, "<html><head><title>Upload Module</title></head><body>\n");

  nxd_fwbuffer* fwb=nxweb_get_request_data(req, UPLOAD_HANDLER_KEY).ptr;

  if (fwb) {
    nxe_size_t stored_size;
    if (fwb->size > fwb->max_size) {
      stored_size=fwb->max_size;
      nxweb_response_printf(resp, "<p>POST content (%ld bytes) stored in file 'upload.tmp' (truncated to %ld bytes)</p>\n", fwb->size, stored_size);
    }
    else {
      stored_size=fwb->size;
      nxweb_response_printf(resp, "<p>POST content (%ld bytes) stored in file 'upload.tmp'</p>\n", stored_size);
    }
  }

  nxweb_response_printf(resp, "<form method='post' enctype='multipart/form-data'>File(s) to upload: <input type='file' multiple name='uploadedfile' /> <input type='submit' value='Go!' /></form>\n");

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
           "content_length=%ld<br/>\n"
           "content_received=%ld<br/>\n"
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
           req->content_received,
           req->transfer_encoding,
           req->accept_encoding,
           req->content
          );

  if (req->headers) {
    nxweb_response_append_str(resp, "<h3>Headers:</h3>\n<ul>\n");
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->headers); itr; itr=nx_simple_map_itr_next(itr)) {
      nxweb_response_printf(resp, "<li>%H=%H</li>\n", itr->name, itr->value);
    }
    nxweb_response_append_str(resp, "</ul>\n");
  }

  nxweb_response_append_str(resp, "</body></html>");

  return NXWEB_OK;
}

static nxweb_result upload_on_post_data(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (req->content_length>MAX_UPLOAD_SIZE) {
    nxweb_send_http_error(resp, 413, "Request Entity Too Large");
    resp->keep_alive=0; // close connection
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
  nxd_fwbuffer* fwb=nxb_alloc_obj(req->nxb, sizeof(nxd_fwbuffer));
  nxweb_set_request_data(req, UPLOAD_HANDLER_KEY, (nxe_data)(void*)fwb, upload_request_data_finalize);
  int fd=open("upload.tmp", O_WRONLY|O_CREAT|O_TRUNC, 0664);
  nxd_fwbuffer_init(fwb, fd, MAX_UPLOAD_SIZE);
  conn->hsp.cls->connect_request_body_out(&conn->hsp, &fwb->data_in);
  conn->hsp.cls->start_receiving_request_body(&conn->hsp);
  return NXWEB_OK;
}

static nxweb_result upload_on_post_data_complete(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  // It is not strictly necessary to close the file here
  // as we are closing it anyway in request data finalizer.
  // Releasing resources in finalizer is the proper way of doing this
  // as any other callbacks might not be invoked under error conditions.
  nxd_fwbuffer* fwb=nxweb_get_request_data(req, UPLOAD_HANDLER_KEY).ptr;
  close(fwb->fd);
  fwb->fd=0;
  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(upload,
        .on_request=upload_on_request,
        .on_post_data=upload_on_post_data,
        .on_post_data_complete=upload_on_post_data_complete,
        .flags=NXWEB_HANDLE_ANY);
