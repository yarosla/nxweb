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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <fcntl.h>


void nxweb_access_log_restart() { // specify access log file path in nxweb_server_config.access_log_fpath
  pthread_mutex_lock(&nxweb_server_config.access_log_start_mux);
  if (nxweb_server_config.access_log_fd) {
    close(nxweb_server_config.access_log_fd);
    nxweb_server_config.access_log_fd=0;
  }
  if (nxweb_server_config.access_log_fpath) {
    nxweb_server_config.access_log_fd=open(nxweb_server_config.access_log_fpath, O_WRONLY|O_APPEND|O_CREAT, 0664);
    if (nxweb_server_config.access_log_fd==-1) {
      nxweb_server_config.access_log_fd=0;
      nxweb_log_error("can't open access log file %s; error %d", nxweb_server_config.access_log_fpath, errno);
    }
  }
  pthread_mutex_unlock(&nxweb_server_config.access_log_start_mux);
}

void nxweb_access_log_stop() {
  pthread_mutex_lock(&nxweb_server_config.access_log_start_mux);
  if (nxweb_server_config.access_log_fd) {
    close(nxweb_server_config.access_log_fd);
    nxweb_server_config.access_log_fd=0;
  }
  pthread_mutex_unlock(&nxweb_server_config.access_log_start_mux);
}

void nxweb_access_log_thread_flush() { // flush net_thread's access log
  nxweb_net_thread_data* tdata=_nxweb_net_thread_data;
  char* block=tdata->access_log_block;
  int size=NXWEB_ACCESS_LOG_BLOCK_SIZE - tdata->access_log_block_avail;
  if (!block) return;
  assert(size);
  tdata->access_log_block=0;

  pthread_mutex_lock(&nxweb_server_config.access_log_start_mux);
  int fd=nxweb_server_config.access_log_fd;
  if (fd) {
    if (flock(fd, LOCK_EX)==0) {
      if (write(fd, block, size)!=size) {
        nxweb_log_error("can't write to access log; error %d", errno);
      }
      flock(fd, LOCK_UN);
    }
    else {
      nxweb_log_error("can't lock access log file for flushing; error %d", errno);
    }
  }
  pthread_mutex_unlock(&nxweb_server_config.access_log_start_mux);
  nx_free(block);
}

void nxweb_access_log_add_frag(nxweb_http_request* req, nxweb_log_fragment* frag) { // collect info to log
  if (!nxweb_server_config.access_log_fd) return;
  frag->prev=req->access_log;
  req->access_log=frag;
}

void nxweb_access_log_write(nxweb_http_request* req) { // write request's log record to thread's buffer
  if (!req->access_log) return;
  nxweb_net_thread_data* tdata=_nxweb_net_thread_data;
  // calculate record size
  int rec_size=0;
  int i, num_frags;

#define MAX_FRAGS 100

  nxweb_log_fragment* frags[MAX_FRAGS];
  nxweb_log_fragment* frag;
  for (i=0, frag=req->access_log; frag && i<MAX_FRAGS; frag=frag->prev, i++) {
    rec_size+=frag->length+1; // + space-delimiter
    frags[i]=frag;
  }
  num_frags=i;
  if (rec_size >= NXWEB_ACCESS_LOG_BLOCK_SIZE) {
    frag=frags[num_frags-1];
    nxweb_log_error("access log record_size is too large: %d \"%.*s\"", rec_size, frag->length, frag->content);
    return;
  }
  if (rec_size > tdata->access_log_block_avail) {
    // flush block
    nxweb_access_log_thread_flush();
    assert(!tdata->access_log_block);
  }
  if (!tdata->access_log_block) {
    // allocate new block
    tdata->access_log_block=nx_alloc(NXWEB_ACCESS_LOG_BLOCK_SIZE);
    tdata->access_log_block_avail=NXWEB_ACCESS_LOG_BLOCK_SIZE;
    tdata->access_log_block_ptr=tdata->access_log_block;
  }

  for (i=num_frags-1; i>=0; i--) {
    frag=frags[i];
    memcpy(tdata->access_log_block_ptr, frag->content, frag->length);
    tdata->access_log_block_ptr+=frag->length;
    *tdata->access_log_block_ptr++=' ';
    tdata->access_log_block_avail-=frag->length+1;
  }
  assert(tdata->access_log_block_avail>=0);
  tdata->access_log_block_ptr[-1]='\n'; // replace last space with LF
}

#define BUILD_FRAG_BEGIN \
  if (!nxweb_server_config.access_log_fd) return; \
  int size; \
  nxb_buffer* nxb=req->nxb; \
  nxb_start_stream(nxb); \
  nxb_blank(nxb, offsetof(nxweb_log_fragment, content))

#define BUILD_FRAG_END \
  nxweb_log_fragment* frag=(nxweb_log_fragment*)nxb_finish_stream(nxb, &size); \
  frag->type=0; \
  frag->prev=0; \
  frag->length=size-offsetof(nxweb_log_fragment, content); \
  nxweb_access_log_add_frag(req, frag)

void nxweb_access_log_on_request_received(nxweb_http_server_connection* conn, nxweb_http_request* req) {
  BUILD_FRAG_BEGIN;

  nxb_append_str(nxb, nxe_get_current_iso8601_time_str(conn->tdata->loop));
  nxb_append_char(nxb, ' ');
  nxb_append_uint64_hex_zeropad(nxb, conn->uid, 16);
  nxb_append_char(nxb, ' ');
  nxb_append_uint64_hex_zeropad(nxb, req->uid, 16);
  nxb_append_char(nxb, ' ');
  if (req->parent_req) {
    nxb_append_uint64_hex_zeropad(nxb, req->parent_req->uid, 16);
  }
  else {
    nxb_append_str(nxb, conn->remote_addr); // do not repeat for subrequests
  }
  nxb_append_char(nxb, ' ');
  nxb_append_str(nxb, req->method);
  nxb_append_char(nxb, '.');
  nxb_append_char(nxb, '0'+req->http11);
  nxb_append_char(nxb, ' ');
  nxb_append_str(nxb, req->host? req->host : "-");
  nxb_append_char(nxb, ' ');
  nxb_append_str(nxb, req->uri);
  nxb_append_char(nxb, ' ');
  if (!req->parent_req && req->user_agent) { // do not repeat UA for subrequests
    nxb_append(nxb, "{{ua:", 5);
    nxb_append_str(nxb, req->user_agent);
    nxb_append(nxb, "}} ", 3);
  }
  nxb_append_char(nxb, '[');
  if (req->if_modified_since) nxb_append(nxb, "Im", 2);
  if (req->accept_gzip_encoding) nxb_append(nxb, "Ag", 2);
  if (req->templates_no_parse) nxb_append(nxb, "Nt", 2);
  nxb_append_char(nxb, ']');

  BUILD_FRAG_END;
}

void nxweb_access_log_on_request_complete(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  BUILD_FRAG_BEGIN;

  nxb_append_uint(nxb, resp->status_code? resp->status_code : 200);
  nxb_append_char(nxb, ' ');
  nxb_append_uint(nxb, resp->bytes_sent);
  nxb_append(nxb, "b ", 2);
  nxe_time_t t=nxweb_get_loop_time(conn);
  nxb_append_uint(nxb, (t - req->received_time + 500)/1000); // round to milliseconds
  nxb_append(nxb, "ms ", 3);
  nxb_append_str(nxb, conn->handler && conn->handler->name? conn->handler->name : "(null)");
  nxb_append_char(nxb, ' ');
  nxb_append_char(nxb, '[');
  if (resp->gzip_encoded) nxb_append(nxb, "Gz", 2);
  if (resp->content_length<0) nxb_append(nxb, "Ch", 2); // chunked encoding
  if (resp->last_modified) nxb_append(nxb, "Lm", 2);
  nxb_append_char(nxb, ']');

  BUILD_FRAG_END;
}

void nxweb_access_log_on_proxy_response(nxweb_http_request* req, nxd_http_proxy* hpx, nxweb_http_response* proxy_resp) {
  BUILD_FRAG_BEGIN;

  nxb_append(nxb, "{{px:", 5);
  nxb_append_uint64_hex_zeropad(nxb, hpx->uid, 16);
  nxb_append_char(nxb, ' ');
  nxb_append_uint(nxb, hpx->hcp.request_count);
  nxb_append_char(nxb, '/');
  nxb_append_uint(nxb, hpx->pool->conn_count);
  nxb_append_char(nxb, '/');
  nxb_append_uint(nxb, hpx->pool->conn_count_max);
  nxb_append_char(nxb, ' ');
  nxb_append_uint(nxb, proxy_resp->status_code);
  nxb_append_char(nxb, ' ');
  nxb_append_char(nxb, '[');
  if (proxy_resp->content_length<0) nxb_append(nxb, "Ch", 2); // chunked encoding
  if (proxy_resp->last_modified) nxb_append(nxb, "Lm", 2);
  nxb_append_char(nxb, ']');
  nxb_append(nxb, "}}", 2);

  BUILD_FRAG_END;
}
