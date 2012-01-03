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

#include <stddef.h>
#include <sys/stat.h>

#include "revision.h"
#include "config.h"
#include "nx_event.h"
#include "nx_buffer.h"
#include "misc.h"

typedef struct nx_simple_map_entry {
  const char* name;
  const char* value;
} nx_simple_map_entry, nxweb_http_header, nxweb_http_parameter, nxweb_http_cookie;

enum nxweb_conn_state {
  NXWEB_CS_WAITING_FOR_REQUEST,
  NXWEB_CS_RECEIVING_HEADERS,
  NXWEB_CS_RECEIVING_BODY,
  NXWEB_CS_HANDLING,
  NXWEB_CS_SENDING_HEADERS,
  NXWEB_CS_SENDING_BODY,
  NXWEB_CS_CLOSING,
  NXWEB_CS_TIMEOUT,
  NXWEB_CS_ERROR
};

enum nxweb_response_state {
  NXWEB_RS_INITIAL=0,
  NXWEB_RS_WRITING_RESPONSE_HEADERS,
  NXWEB_RS_WRITING_RESPONSE_BODY
};

typedef enum nxweb_uri_handler_phase {
  NXWEB_PH_HEADERS=10,
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

typedef struct nxweb_chunked_decoder_state {
  enum {CDS_CR1=-2, CDS_LF1=-1, CDS_SIZE=0, CDS_LF2, CDS_DATA} state;
  unsigned short final_chunk:1;
  long chunk_bytes_left;
} nxweb_chunked_decoder_state;

typedef struct nxweb_request {

  // Parsed HTTP request info:
  char* request_body;
  char* method;
  char* uri;
  char* http_version;
  char* host;
  char* cookie;
  char* user_agent;
  char* content_type;
  long content_length; // -1 = unspecified: chunked or until close
  long content_received;
  char* transfer_encoding;
  char* range;
  const char* path_info; // points right after uri_handler's prefix

  nxweb_http_header* headers;
  nxweb_http_parameter* parameters;
  nxweb_http_cookie* cookies;

  void* user_data;

  char* chunk_buffer;
  char* chunk_buffer_end;
  char* chunk_read_ptr;
  char* chunk_write_ptr;

  char* out_headers;
  char* out_body;
  int out_body_length;

  // Building response:
  const char* response_status;
  const char* response_content_type;
  const char* response_content_charset;
  nxweb_http_header* response_headers;
  time_t response_last_modified;

  int response_code;

  int sendfile_fd;
  off_t sendfile_offset;

  enum nxweb_response_state rstate;

  nxweb_chunked_decoder_state cdstate;

  const struct nxweb_uri_handler* handler;
  const struct nxweb_module* const* handler_module;
  enum nxweb_result handler_result;

  // booleans
  unsigned http11:1;
  unsigned head_method:1;
  unsigned get_method:1;
  unsigned post_method:1;
  unsigned expect_100_continue:1;
  unsigned chunked_request:1;
  unsigned receive_in_chunks:1;
  unsigned send_in_chunks:1;
  unsigned send_in_chunks_complete:1;
  unsigned chunk_buffer_last_write:1;
  unsigned keep_alive:1;
  unsigned sending_100_continue:1;
} nxweb_request;

typedef struct nxweb_connection {

  char remote_addr[16]; // 255.255.255.255

  nxe_loop* loop;

  nxe_timer timer_keep_alive;
  nxe_timer timer_read;
  nxe_timer timer_write;

  int request_count; // number of requests served by this connection
  enum nxweb_conn_state cstate;

  nxe_event_async worker_evt;

  nxweb_request req;

  nxb_buffer iobuf;
  char buf[5120];
} nxweb_connection;

#define NXWEB_REQUEST_CONNECTION(r) ((nxweb_connection*)((char*)(r)-offsetof(nxweb_connection, req)))
#define NXWEB_CONNECTION_EVENT(c) ((nxe_event*)((char*)(c)-sizeof(nxe_event)))

typedef struct nxweb_uri_handler {
  const char* uri_prefix;
  nxweb_result (*callback)(nxweb_uri_handler_phase phase, nxweb_request* req);
  enum nxweb_uri_handler_flags flags;
  int (*on_recv_body_chunk)(nxweb_request* req, void* ptr, int size);
  int (*on_send_body_chunk)(nxweb_request* req, void* ptr, int size);
  void (*on_cancel_request)(nxweb_request* req);
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

void nxweb_handler_ready_to_recieve(nxweb_request* req); // used by handlers accepting request body in parts
void nxweb_handler_ready_to_send(nxweb_request* req); // used by handlers sending response body in parts

const char* nxweb_get_request_header(nxweb_request *req, const char* name);
const char* nxweb_get_request_parameter(nxweb_request *req, const char* name);
const char* nxweb_get_request_cookie(nxweb_request *req, const char* name);

static inline void* nxweb_request_user_data_alloc(nxweb_request *req, int size) {
  return nxb_alloc_obj(&NXWEB_REQUEST_CONNECTION(req)->iobuf, size);
}

static inline void* nxweb_request_user_data_calloc(nxweb_request *req, int size) {
  return nxb_calloc_obj(&NXWEB_REQUEST_CONNECTION(req)->iobuf, size);
}

void nxweb_send_http_error(nxweb_request *req, int code, const char* message);
void nxweb_send_redirect(nxweb_request *req, int code, const char* location);
int nxweb_send_file(nxweb_request *req, const char* fpath, struct stat* finfo, off_t offset, size_t end_offset, const char* charset);
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
