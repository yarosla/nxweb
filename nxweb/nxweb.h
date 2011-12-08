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

#ifndef NXWEB_H_INCLUDED
#define NXWEB_H_INCLUDED

#include <obstack.h>
#include <ev.h>

#include "revision.h"
#include "config.h"

typedef struct obstack obstack;

typedef struct nx_simple_map_entry {
  const char* name;
  const char* value;
} nx_simple_map_entry, nxweb_http_header, nxweb_http_parameter, nxweb_http_cookie;

typedef struct nx_chunk {
  struct nx_chunk* next;
  struct nx_chunk* prev;
  int size;
  char data[];
} nx_chunk;

enum nxweb_conn_state {
  NXWEB_CS_INITIAL=0,
  NXWEB_CS_CLOSING,
  NXWEB_CS_TIMEOUT,
  NXWEB_CS_ERROR,
  NXWEB_CS_WAITING_FOR_REQUEST,
  NXWEB_CS_RECEIVING_HEADERS,
  NXWEB_CS_RECEIVING_BODY,
  NXWEB_CS_HANDLING,
  NXWEB_CS_SENDING_HEADERS,
  NXWEB_CS_SENDING_BODY
};

enum nxweb_response_state {
  NXWEB_RS_INITIAL=0,
  NXWEB_RS_ADDING_RESPONSE_HEADERS,
  NXWEB_RS_WRITING_RESPONSE_HEADERS,
  NXWEB_RS_WRITING_RESPONSE_BODY
};

typedef enum nxweb_uri_handler_phase {
  NXWEB_PH_CONTENT=100
} nxweb_uri_handler_phase;

typedef enum nxweb_result {
  NXWEB_OK=0,
  NXWEB_NEXT=1,
  NXWEB_BREAK=2,
  NXWEB_ERROR=-1
} nxweb_result;

enum nxweb_uri_handler_flags {
  NXWEB_INWORKER=0, // execute handler in worker thread (for lengthy or blocking operations)
  NXWEB_INPROCESS=1, // execute handler in event thread (must be fast and non-blocking!)
  NXWEB_PARSE_PARAMETERS=2, // parse query string and (url-encoded) post data before calling this handler
  NXWEB_PRESERVE_URI=4, // modifier for NXWEB_PARSE_PARAMETERS; preserver conn->uri string while parsing (allocate copy)
  NXWEB_PARSE_COOKIES=8, // parse cookie header before calling this handler
  NXWEB_HANDLE_GET=0x10,
  NXWEB_HANDLE_POST=0x20,
  NXWEB_HANDLE_ANY=0x40,
  _NXWEB_HANDLE_MASK=0x70
};

typedef struct nxweb_connection {
  int fd;
  char remote_addr[16]; // 255.255.255.255
  struct ev_loop* loop;

  struct nxweb_request* request;
  struct nxweb_net_thread* tdata;

  ev_timer watch_read_request_time;
  ev_timer watch_write_response_time;
  ev_timer watch_keep_alive_time;
  ev_io watch_socket_read;
  ev_io watch_socket_write;
  ev_async watch_async_data_ready;

  struct obstack data; // conn data obstack; can be used by handlers unless request is (adding_response_headers || writing_response_headers || writing_response_body)
  struct obstack user_data; // user data obstack; can be used by handlers; user must init it before use; we call free if it has been initialized

  int request_count; // number of requests served by this connection
  unsigned keep_alive:1;
  unsigned sending_100_continue:1;
  enum nxweb_conn_state cstate;
} nxweb_connection;

typedef struct nxweb_request {
  nxweb_connection* conn;

  // Parsed HTTP request info:
  char* request_body;
  char* method;
  char* uri;
  char* http_version;
  char* host;
  char* cookie;
  char* user_agent;
  char* content_type;
  int content_length; // -1 = unspecified: chunked or until close
  int content_received;
  char* transfer_encoding;
  const char* path_info; // points right after uri_handler's prefix

  nxweb_http_header* headers;
  nxweb_http_parameter* parameters;
  nxweb_http_cookie* cookies;

  // Building response:
  int response_code;
  const char* response_status;
  const char* response_content_type;
  const char* response_content_charset;
  nxweb_http_header* response_headers;

  char* in_headers;
  char* out_headers;
  nx_chunk* out_body_chunk;

  nx_chunk* write_chunk;
  int write_pos; // whithin out_headers or write_chunk (determined by headers_sent flag)
  int sendfile_fd;
  off_t sendfile_offset;
  size_t sendfile_length;
  time_t sendfile_last_modified;

  enum nxweb_response_state rstate;

  const struct nxweb_uri_handler* handler;
  const struct nxweb_module* const* handler_module;
  enum nxweb_result handler_result;

  // booleans
  unsigned http11:1;
  unsigned head_method:1;
  unsigned expect_100_continue:1;
  unsigned chunked_request:1;
} nxweb_request;

typedef struct nxweb_uri_handler {
  const char* uri_prefix;
  nxweb_result (*callback)(nxweb_uri_handler_phase phase, nxweb_request* req);
  enum nxweb_uri_handler_flags flags;
} nxweb_uri_handler;

typedef struct nxweb_module {
  nxweb_result (*server_startup_callback)(void);
  const nxweb_uri_handler* uri_handlers;
} nxweb_module;

typedef struct nxweb_mime_type {
  const char* ext;
  const char* mime;
  unsigned charset_required:1;
} nxweb_mime_type;

extern const nxweb_module* const nxweb_modules[];

// Public API
void nxweb_shutdown();

const char* nxweb_get_request_header(nxweb_request *req, const char* name);
const char* nxweb_get_request_parameter(nxweb_request *req, const char* name);
const char* nxweb_get_request_cookie(nxweb_request *req, const char* name);

void nxweb_send_http_error(nxweb_request *req, int code, const char* message);
void nxweb_send_redirect(nxweb_request *req, int code, const char* location);
int nxweb_send_file(nxweb_request *req, const char* fpath, struct stat* finfo);
void nxweb_set_response_status(nxweb_request *req, int code, const char* message);
void nxweb_set_response_content_type(nxweb_request *req, const char* content_type);
void nxweb_set_response_charset(nxweb_request *req, const char* charset);
void nxweb_add_response_header(nxweb_request *req, const char* name, const char* value);
void nxweb_response_make_room(nxweb_request *req, int size);
void nxweb_response_printf(nxweb_request *req, const char* fmt, ...); // __attribute__((format (printf, 2, 3)));
void nxweb_response_append(nxweb_request *req, const char* text);

const nxweb_mime_type* nxweb_get_mime_type(const char* type_name);
const nxweb_mime_type* nxweb_get_mime_type_by_ext(const char* fpath_or_ext);

char* nxweb_url_decode(char* src, char* dst); // can do it inplace (dst=0)
char* nxweb_trunc_space(char* str); // does it inplace

extern const unsigned char PIXEL_GIF[43]; // transparent pixel

extern const char* ERROR_LOG_FILE; // path to log file

#endif // NXWEB_H_INCLUDED
