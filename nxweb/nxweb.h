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

#ifndef NXWEB_H
#define	NXWEB_H

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#define MEM_GUARD 64
#define nx_alloc(size) memalign(MEM_GUARD, (size)+MEM_GUARD)

#define REVISION "3.0.0-dev"

#include "config.h"

#include <stdlib.h>

#include "nx_buffer.h"
#include "nx_file_reader.h"
#include "nx_event.h"
#include "misc.h"
#include "nx_workers.h"

#ifdef	__cplusplus
extern "C" {
#endif

struct stat;

enum nxweb_timers {
  NXWEB_TIMER_KEEP_ALIVE,
  NXWEB_TIMER_READ,
  NXWEB_TIMER_WRITE,
  NXWEB_TIMER_BACKEND,
  NXWEB_TIMER_100CONTINUE
};

typedef struct nx_simple_map_entry {
  const char* name;
  const char* value;
} nx_simple_map_entry, nxweb_http_header, nxweb_http_parameter, nxweb_http_cookie;

enum nxweb_chunked_decoder_state_code {CDS_CR1=-2, CDS_LF1=-1, CDS_SIZE=0, CDS_LF2, CDS_DATA};

typedef struct nxweb_chunked_decoder_state {
  enum nxweb_chunked_decoder_state_code state;
  unsigned short final_chunk:1;
  unsigned short monitor_only:1;
  nxe_ssize_t chunk_bytes_left;
} nxweb_chunked_decoder_state;

typedef struct nxweb_http_request {

  nxb_buffer* nxb;

  // booleans
  unsigned http11:1;
  unsigned head_method:1;
  unsigned get_method:1;
  unsigned post_method:1;
  unsigned other_method:1;
  unsigned expect_100_continue:1;
  unsigned chunked_encoding:1;
  unsigned chunked_content_complete:1;
  unsigned keep_alive:1;
  unsigned sending_100_continue:1;
  unsigned x_forwarded_ssl:1;

  // Parsed HTTP request info:
  const char* method;
  const char* uri;
  const char* http_version;
  const char* host;
  // const char* remote_addr; - get it from connection struct
  const char* cookie;
  const char* user_agent;
  const char* content_type;
  const char* content;
  nxe_ssize_t content_length; // -1 = unspecified: chunked or until close
  nxe_size_t content_received;
  const char* transfer_encoding;
  const char* range;
  const char* path_info; // points right after uri_handler's prefix

  const char* x_forwarded_for;
  const char* x_forwarded_host;

  nxweb_http_header* headers;
  nxweb_http_parameter* parameters;
  nxweb_http_cookie* cookies;

  nxweb_chunked_decoder_state cdstate;

} nxweb_http_request;

typedef struct nxweb_http_response {
  nxb_buffer* nxb;

  const char* host;
  const char* raw_headers;
  const char* content;
  nxe_ssize_t content_length;
  nxe_size_t content_received;

  unsigned keep_alive:1;
  unsigned http11:1;
  unsigned chunked_encoding:1;
  //unsigned chunked_content_complete:1;

  // Building response:
  const char* status;
  const char* content_type;
  const char* content_charset;
  nxweb_http_header* headers;
  time_t last_modified;

  int status_code;

  nxweb_chunked_decoder_state cdstate;

  int sendfile_fd;
  off_t sendfile_offset;
  off_t sendfile_end;

} nxweb_http_response;

typedef struct nxweb_mime_type {
  const char* ext;
  const char* mime;
  unsigned charset_required:1;
} nxweb_mime_type;

#include "nxd.h"
#include "http_server.h"

extern const unsigned char PIXEL_GIF[43]; // transparent pixel


static inline char nx_tolower(char c) {
  return c>='A' && c<='Z' ? c+('a'-'A') : c;
}

static inline int nx_strcasecmp(const char* s1, const char* s2) {
  const unsigned char* p1=(const unsigned char*)s1;
  const unsigned char* p2=(const unsigned char*)s2;
  int result;

  if (p1==p2) return 0;

  while ((result=nx_tolower(*p1)-nx_tolower(*p2++))==0)
    if (*p1++=='\0') break;

  return result;
}

static inline const char* nx_simple_map_get(nx_simple_map_entry map[], const char* name) {
  int i;
  for (i=0; map[i].name; i++) {
    if (strcmp(map[i].name, name)==0) return map[i].value;
  }
  return 0;
}

static inline const char* nx_simple_map_get_nocase(nx_simple_map_entry map[], const char* name) {
  int i;
  for (i=0; map[i].name; i++) {
    if (nx_strcasecmp(map[i].name, name)==0) return map[i].value;
  }
  return 0;
}

static inline void nx_simple_map_add(nx_simple_map_entry map[], const char* name, const char* value, int max_entries) {
  int i;
  for (i=0; i<max_entries-1; i++) {
    if (!map[i].name) {
      map[i].name=name;
      map[i].value=value;
      return;
    }
  }
}

int nxweb_listen(const char* host_and_port, int backlog, _Bool secure, const char* cert_file, const char* key_file, const char* dh_params_file);
int nxweb_setup_http_proxy_pool(int idx, const char* host_and_port);
void nxweb_run();

void nxweb_parse_request_parameters(nxweb_http_request *req, int preserve_uri); // Modifies conn->uri and request_body content (does url_decode inplace)
void nxweb_parse_request_cookies(nxweb_http_request *req); // Modifies conn->cookie content (does url_decode inplace)

static inline const char* nxweb_get_request_header(nxweb_http_request *req, const char* name) {
  return req->headers? nx_simple_map_get_nocase(req->headers, name) : 0;
}

static inline const char* nxweb_get_request_parameter(nxweb_http_request *req, const char* name) {
  return req->parameters? nx_simple_map_get(req->parameters, name) : 0;
}

static inline const char* nxweb_get_request_cookie(nxweb_http_request *req, const char* name) {
  return req->cookies? nx_simple_map_get(req->cookies, name) : 0;
}

static inline int nxweb_url_prefix_match(const char* url, const char* prefix, int prefix_len) {
  return !strncmp(url, prefix, prefix_len) && (!url[prefix_len] || url[prefix_len]=='/' || url[prefix_len]=='?' || url[prefix_len]==';');
}

const nxweb_mime_type* nxweb_get_mime_type(const char* type_name);
const nxweb_mime_type* nxweb_get_mime_type_by_ext(const char* fpath_or_ext);

char* nxweb_url_decode(char* src, char* dst); // can do it inplace (dst=0)

void nxweb_set_response_status(nxweb_http_response* resp, int code, const char* message);
void nxweb_set_response_content_type(nxweb_http_response* resp, const char* content_type);
void nxweb_set_response_charset(nxweb_http_response* resp, const char* charset);
void nxweb_add_response_header(nxweb_http_response* resp, const char* name, const char* value);

static inline void nxweb_response_make_room(nxweb_http_response* resp, int min_size) {
  nxb_make_room(resp->nxb, min_size);
}
static inline void nxweb_response_printf(nxweb_http_response* resp, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  nxb_printf_va(resp->nxb, fmt, ap);
  va_end(ap);
}
static inline void nxweb_response_append_str(nxweb_http_response* resp, const char* str) {
  nxb_append_str(resp->nxb, str);
}
static inline void nxweb_response_append_data(nxweb_http_response* resp, const void* data, int size) {
  nxb_append(resp->nxb, data, size);
}
static inline void nxweb_response_append_char(nxweb_http_response* resp, char c) {
  nxb_append_char(resp->nxb, c);
}
static inline void nxweb_response_append_uint(nxweb_http_response* resp, unsigned long n) {
  nxb_append_uint(resp->nxb, n);
}

void nxweb_send_redirect(nxweb_http_response* resp, int code, const char* location);
void nxweb_send_http_error(nxweb_http_response* resp, int code, const char* message);
int nxweb_send_file(nxweb_http_response *resp, const char* fpath, struct stat* finfo, off_t offset, size_t size, const char* charset);
void nxweb_send_data(nxweb_http_response *resp, const void* data, size_t size, const char* content_type);

// Internal use only:
char* _nxweb_find_end_of_http_headers(char* buf, int len, char** start_of_body);
int _nxweb_parse_http_request(nxweb_http_request* req, char* headers, char* end_of_headers);
void _nxweb_decode_chunked_request(nxweb_http_request* req);
nxe_ssize_t _nxweb_decode_chunked(char* buf, nxe_size_t buf_len);
nxe_ssize_t _nxweb_verify_chunked(const char* buf, nxe_size_t buf_len);
int _nxweb_decode_chunked_stream(nxweb_chunked_decoder_state* decoder_state, char* buf, nxe_size_t* buf_len);
void _nxweb_register_printf_extensions();
nxweb_http_response* _nxweb_http_response_init(nxweb_http_response* resp, nxb_buffer* nxb, nxweb_http_request* req);
void _nxweb_prepare_response_headers(nxe_loop* loop, nxweb_http_response* resp);
const char* _nxweb_prepare_client_request_headers(nxweb_http_request *req);
int _nxweb_parse_http_response(nxweb_http_response* resp, char* headers, char* end_of_headers);

#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_H */

