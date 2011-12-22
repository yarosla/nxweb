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

typedef struct upload_info {
  int fd;
  const char* p;
} upload_info;

static nxweb_result upload_request(nxweb_uri_handler_phase phase, nxweb_request *req) {
  if (phase==NXWEB_PH_HEADERS) {
    int fd=open("upload.tmp", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd<0) {
      nxweb_log_error("Can't open file for upload %d", errno);
      return NXWEB_ERROR;
    }
    nxweb_log_error("Opened file for upload");
    upload_info* ui=nxweb_request_user_data_calloc(req, sizeof(upload_info));
    ui->fd=fd;
    req->user_data=ui;
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
    else if (req->user_data) {
      upload_info* ui=req->user_data;
      if (ui->fd) close(ui->fd);
      nxweb_response_append(req, "Saved upload in file");
    }
    else {
      nxweb_response_append(req, "No file were uploaded");
    }
  }
  return NXWEB_OK;
}

static int upload_chunk(nxweb_request* req, void* ptr, int size) {
  upload_info* ui=req->user_data;
  nxweb_log_error("Received upload chunk of %d bytes", size);
  if (size) {
    if (write(ui->fd, ptr, size)!=size) {
      nxweb_log_error("Can't write to upload file %d", errno);
      return 0;
    }
  }
  if (req->content_length>0 && req->content_received>=req->content_length) {
    close(ui->fd);
  }
  return size;
}

static void upload_cancel(nxweb_request* req) {
  upload_info* ui=req->user_data;
  if (ui && ui->fd) close(ui->fd);
}

static nxweb_result download_request(nxweb_uri_handler_phase phase, nxweb_request *req) {
  if (phase==NXWEB_PH_CONTENT) {
    int fd=open("upload.tmp", O_RDONLY);
    if (fd<0) {
      nxweb_log_error("Can't open file for download %d", errno);
      return NXWEB_ERROR;
    }
    struct stat finfo;
    fstat(fd, &finfo);
    nxweb_log_error("Opened file for download");
    upload_info* ui=nxweb_request_user_data_calloc(req, sizeof(upload_info));
    ui->fd=fd;
    req->user_data=ui;
    req->out_body_length=-1; //finfo.st_size;
    //req->response_content_type="application/octet-stream";
    req->response_last_modified=finfo.st_mtime;
  }
  return NXWEB_OK;
}

static inline char HEX_DIGIT(char n) { n&=0xf; return n<10? n+'0' : n-10+'A'; }

static void wrap_chunk(char* ptr, uint32_t size) {
  char* p;
  uint32_t n=size;
  int i;
  for (i=8, p=ptr+8; i--; ) {
    *--p=HEX_DIGIT(n);
    n>>=4;
  }
  ptr[8]='\r';
  ptr[9]='\n';
  ptr[10+size]='\r';
  ptr[10+size+1]='\n';
}

static const char* term="0\r\n\r\n";

static int download_chunk(nxweb_request* req, void* ptr, int size) {
  upload_info* ui=req->user_data;
  if (size) {
    if (ui->p) { // sending terminator
      char* p=ptr;
      char* end=ptr+size;
      int cnt=0;
      while (p<end && *ui->p) { *p++=*ui->p++; cnt++; }
      if (!ui->p) req->send_in_chunks_complete=1;
      return cnt;
    }
    uint32_t cnt=read(ui->fd, ptr+10, size-12);
    if (cnt<0) {
      nxweb_log_error("Can't read download file %d", errno);
      return 0;
    }
    wrap_chunk(ptr, cnt);
    cnt+=12;
    if (cnt<size) {
      ui->p=term;
      close(ui->fd);
      ui->fd=0;
      char* p=ptr+cnt;
      char* end=ptr+size;
      while (p<end && *ui->p) { *p++=*ui->p++; cnt++; }
      if (!ui->p) req->send_in_chunks_complete=1;
    }
    nxweb_log_error("Sending download chunk of %d bytes", cnt);
    return cnt;
  }
  return 0;
}

static void download_cancel(nxweb_request* req) {
  upload_info* ui=req->user_data;
  if (ui && ui->fd) close(ui->fd);
}

static const nxweb_uri_handler uri_handlers[] = {
  {.uri_prefix="/upload", .callback=upload_request, .flags=NXWEB_INPROCESS|NXWEB_HANDLE_POST,
   .on_recv_body_chunk=upload_chunk, .on_cancel_request=upload_cancel},
  {.uri_prefix="/download", .callback=download_request, .flags=NXWEB_INPROCESS|NXWEB_HANDLE_GET,
   .on_send_body_chunk=download_chunk, .on_cancel_request=download_cancel},
  {0, 0, 0}
};

/// Module definition
// List of active modules is maintained in modules.c file.

const nxweb_module upload_module = {
  .uri_handlers=uri_handlers
};
