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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>

#include "nxweb/nxweb.h"

#define INDEX_FILE "index.htm"

static const char* document_root="html"; // relative to work-dir

static int remove_dots_from_uri_path(char* path) {
  if (!*path) return 0; // end of path
  if (*path!='/') return -1; // invalid path
  while (1) {
    if (path[1]=='.' && path[2]=='.' && (path[3]=='/' || path[3]=='\0')) { // /..(/.*)?$
      memmove(path, path+3, strlen(path+3)+1);
      return 1;
    }
    char* p1=strchr(path+1, '/');
    if (!p1) return 0;
    if (!remove_dots_from_uri_path(p1)) return 0;
    memmove(path, p1, strlen(p1)+1);
  }
}

static nxweb_result send_file(nxweb_uri_handler_phase phase, nxweb_request *req) {
  if (phase!=NXWEB_PH_CONTENT) return NXWEB_OK;

  char fpath[2048];
  int rlen=strlen(document_root);
  strcpy(fpath, document_root);
  char* p=fpath+rlen;
  const char* q=strchr(req->path_info, '?');
  int plen=q? q-req->path_info : strlen(req->path_info);
  if (rlen+plen>sizeof(fpath)-21) { // leave room for index file name
    nxweb_send_http_error(req, 414, "Request-URI Too Long");
    return NXWEB_OK;
  }
  strncat(p, req->path_info, plen);
  nxweb_url_decode(p, 0);
  plen=strlen(p);
  if (plen>0 && p[plen-1]=='/') { // directory index
    strcat(p+plen, INDEX_FILE);
  }

  if (remove_dots_from_uri_path(p)) {
    nxweb_send_http_error(req, 404, "Not Found");
    return NXWEB_OK;
  }

  struct stat finfo;
  if (stat(fpath, &finfo)==-1) {
    return NXWEB_NEXT; // no such file; skip to next handler
  }
  if (S_ISDIR(finfo.st_mode)) {
    strcat(p, "/");
    nxweb_send_redirect(req, 302, p);
    return NXWEB_OK;
  }
  if (!S_ISREG(finfo.st_mode)) { // symlink or ...?
    nxweb_send_http_error(req, 403, "Forbidden");
    return NXWEB_OK;
  }

  if (strcasecmp(req->method, "GET")) {
    nxweb_send_http_error(req, 405, "Method Not Allowed");
    return NXWEB_OK;
  }

  nxweb_set_response_charset(req, "utf-8"); // set default charset for text files

  int result=nxweb_send_file(req, fpath, &finfo);
  if (result!=0) { // should not happen
    nxweb_log_error("[%s] stat() was OK, but open() failed", fpath);
    nxweb_send_http_error(req, 500, "Internal Server Error");
    return NXWEB_OK;
  }

  return NXWEB_OK;
}

static const nxweb_uri_handler sendfile_module_uri_handlers[] = {
  {"", send_file, NXWEB_INWORKER}, // wildcard uri
  {0, 0, 0}
};

/// Module definition
// List of active modules is maintained in modules.c file.

const nxweb_module sendfile_module = {
  .server_startup_callback=0,
  .uri_handlers=sendfile_module_uri_handlers
};
