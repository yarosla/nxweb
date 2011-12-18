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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "../nxweb/nxweb.h"

static nxweb_result upload_request(nxweb_uri_handler_phase phase, nxweb_request *req) {
  if (phase==NXWEB_PH_HEADERS) {
    int fd=open("upload.tmp", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd<0) {
      nxweb_log_error("Can't open file for upload %d", errno);
      return NXWEB_ERROR;
    }
    nxweb_log_error("Opened file for upload");
    req->user_data=(void*)fd;
  }
  else if (phase==NXWEB_PH_CONTENT) {
    if (req->request_body) {
      int fd=open("upload.tmp", O_CREAT|O_WRONLY|O_TRUNC, 0644);
      if (fd<0) {
        nxweb_log_error("Can't open file for upload %d", errno);
        nxweb_send_http_error(req, 500, "Internal Server Error");
        return NXWEB_OK;
      }
      if (write(fd, req->request_body, req->content_length)!=req->content_length) {
        nxweb_log_error("Can't write to upload file %d", errno);
        close(fd);
        nxweb_send_http_error(req, 500, "Internal Server Error");
        return NXWEB_OK;
      }
      close(fd);
      nxweb_log_error("Saved upload in file");
      nxweb_response_append(req, "Saved upload in file");
    }
    else if ((int)req->user_data) {
      close((int)req->user_data);
      nxweb_response_append(req, "Saved upload in file");
    }
    else {
      nxweb_response_append(req, "No file were uploaded");
    }
  }
  return NXWEB_OK;
}

static int upload_chunk(nxweb_request* req, void* ptr, int size) {
  nxweb_log_error("Received upload chunk of %d bytes", size);
  if (size) {
    if (write((int)req->user_data, ptr, size)!=size) {
      nxweb_log_error("Can't write to upload file %d", errno);
      return 0;
    }
  }
  if (req->content_length>0 && req->content_received>=req->content_length) {
    close((int)req->user_data);
  }
  return size;
}

static void upload_cancel(nxweb_request* req) {
  if ((int)req->user_data) close((int)req->user_data);
}

static const nxweb_uri_handler uri_handlers[] = {
  {.uri_prefix="/upload", .callback=upload_request, .flags=NXWEB_INPROCESS|NXWEB_HANDLE_ANY,
   .on_recv_body_chunk=upload_chunk, .on_cancel_request=upload_cancel},
  {0, 0, 0}
};

/// Module definition
// List of active modules is maintained in modules.c file.

const nxweb_module upload_module = {
  .uri_handlers=uri_handlers
};
