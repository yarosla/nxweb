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

#ifndef NXWEB_H
#define	NXWEB_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _FILE_OFFSET_BITS 64

#define MEM_GUARD 64
#define nx_alloc(size) memalign(MEM_GUARD, (size)+MEM_GUARD)

#define REVISION VERSION

#include "nxweb_config.h"

#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>

#include "nx_buffer.h"
#include "nx_file_reader.h"
#include "nx_event.h"
#include "misc.h"
#include "nx_workers.h"
#include "nxjson.h"

#ifdef	__cplusplus
extern "C" {
#endif

struct stat;

enum nxweb_timers {
  NXWEB_TIMER_KEEP_ALIVE,
  NXWEB_TIMER_READ,
  NXWEB_TIMER_WRITE,
  NXWEB_TIMER_BACKEND,
  NXWEB_TIMER_100CONTINUE,
  NXWEB_TIMER_ACCEPT_RETRY
};

typedef struct nx_simple_map_entry {
  const char* name;
  const char* value;
  struct nx_simple_map_entry* next;
} nx_simple_map_entry, nxweb_http_header, nxweb_http_parameter, nxweb_http_cookie;

enum nxweb_chunked_decoder_state_code {CDS_CR1=-2, CDS_LF1=-1, CDS_SIZE=0, CDS_LF2, CDS_DATA};

typedef struct nxweb_chunked_decoder_state {
  enum nxweb_chunked_decoder_state_code state;
  unsigned short final_chunk:1;
  unsigned short monitor_only:1;
  nxe_ssize_t chunk_bytes_left;
} nxweb_chunked_decoder_state;

typedef struct nxweb_chunked_encoder_state {
  unsigned short final_chunk:1;
  unsigned short header_prepared:1;
  nxe_ssize_t chunk_size;
  nxe_ssize_t pos;
  char buf[8]; // "\r\n0000\r\n"
} nxweb_chunked_encoder_state;

struct nxweb_http_server_connection;
struct nxweb_http_request;
struct nxweb_http_response;
struct nxweb_http_request_data;

typedef void (*nxweb_http_request_data_finalizer)(struct nxweb_http_server_connection* conn, struct nxweb_http_request* req, struct nxweb_http_response* resp, nxe_data data);

typedef struct nxweb_http_request_data {
  nxe_data key;
  nxe_data value;
  nxweb_http_request_data_finalizer finalize;
  struct nxweb_http_request_data* next;
} nxweb_http_request_data;

typedef struct nxweb_log_fragment {
  struct nxweb_log_fragment* prev;
  int type;
  int length;
  char content[];
} nxweb_log_fragment;

typedef struct nxweb_http_request {

  nxb_buffer* nxb; // use this for per-request memory allocation

  // booleans
  unsigned http11:1;
  unsigned head_method:1;
  unsigned get_method:1;
  unsigned post_method:1;
  unsigned other_method:1;
  unsigned accept_gzip_encoding:1;
  unsigned expect_100_continue:1;
  unsigned chunked_encoding:1;
  unsigned chunked_content_complete:1;
  unsigned keep_alive:1;
  unsigned sending_100_continue:1;
  unsigned x_forwarded_ssl:1;
  unsigned templates_no_parse:1;
  unsigned buffering_to_memory:1;

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
  const char* accept_encoding;
  const char* range;
  const char* path_info; // points right after uri_handler's prefix

  time_t if_modified_since;

  const char* x_forwarded_for;
  const char* x_forwarded_host;

  nxweb_http_header* headers;
  nxweb_http_parameter* parameters;
  nxweb_http_cookie* cookies;

  struct nxweb_http_request* parent_req; // for subrequests
  uint64_t uid; // unique request id

  struct nxweb_filter_data* filter_data[NXWEB_MAX_FILTERS];

  nxweb_chunked_decoder_state cdstate;

  nxweb_http_request_data* data_chain;
  nxweb_log_fragment* access_log;
  nxe_time_t received_time;

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
  unsigned chunked_encoding:1; // only used in proxy's client_proto; set content_length=-1 for chunked encoding
  unsigned chunked_autoencode:1;
  unsigned gzip_encoded:1;
  unsigned ssi_on:1;
  unsigned templates_on:1;
  unsigned no_cache:1;
  unsigned cache_private:1;

  int run_filter_idx;

  // Building response:
  const char* status;
  const char* content_type;
  const char* content_charset;
  const char* cache_control;
  const char* etag;
  nxweb_http_header* headers;
  const char* extra_raw_headers;
  time_t date;
  time_t last_modified;
  time_t expires;
  time_t max_age; // delta seconds

  int status_code;

  nxweb_chunked_decoder_state cdstate;
  nxweb_chunked_encoder_state cestate;

  const char* cache_key;
  const struct nxweb_mime_type* mtype;

  const char* sendfile_path;
  int sendfile_fd;
  off_t sendfile_offset;
  off_t sendfile_end;
  struct stat sendfile_info;

  nxe_istream* content_out;

  nxe_size_t bytes_sent;

} nxweb_http_response;

typedef struct nxweb_mime_type {
  const char* ext; // must be lowercase
  const char* mime;
  unsigned charset_required:1;
  unsigned gzippable:1;
  unsigned image:1;
  unsigned ssi_on:1;
  unsigned templates_on:1;
} nxweb_mime_type;

#include "nxd.h"
#include "http_server.h"

extern const unsigned char PIXEL_GIF[43]; // transparent pixel


static inline char nx_tolower(char c) {
  return c>='A' && c<='Z' ? c+('a'-'A') : c;
}

static inline void nx_strtolower(char* dst, const char* src) { // dst==src is OK for inplace tolower
  while ((*dst++=nx_tolower(*src++))) ;
}

static inline void nx_strntolower(char* dst, const char* src, int len) { // dst==src is OK for inplace tolower
  while (len-- && (*dst++=nx_tolower(*src++))) ;
}

static inline char nx_toupper(char c) {
  return c>='a' && c<='z' ? c-('a'-'A') : c;
}

static inline void nx_strtoupper(char* dst, const char* src) { // dst==src is OK for inplace tolower
  while ((*dst++=nx_toupper(*src++))) ;
}

static inline void nx_strntoupper(char* dst, const char* src, int len) { // dst==src is OK for inplace tolower
  while (len-- && (*dst++=nx_toupper(*src++))) ;
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

static inline int nx_strncasecmp(const char* s1, const char* s2, int len) {
  const unsigned char* p1=(const unsigned char*)s1;
  const unsigned char* p2=(const unsigned char*)s2;
  int result;

  if (p1==p2 || len<=0) return 0;

  while ((result=nx_tolower(*p1)-nx_tolower(*p2++))==0)
    if (!len-- || *p1++=='\0') break;

  return result;
}

static inline nx_simple_map_entry* nx_simple_map_find(nx_simple_map_entry* map, const char* name) {
  while (map) {
    if (strcmp(map->name, name)==0) return map;
    map=map->next;
  }
  return 0;
}

static inline const char* nx_simple_map_get(nx_simple_map_entry* map, const char* name) {
  nx_simple_map_entry* e=nx_simple_map_find(map, name);
  return e? e->value : 0;
}

static inline nx_simple_map_entry* nx_simple_map_find_nocase(nx_simple_map_entry* map, const char* name) {
  while (map) {
    if (nx_strcasecmp(map->name, name)==0) return map;
    map=map->next;
  }
  return 0;
}

static inline const char* nx_simple_map_get_nocase(nx_simple_map_entry* map, const char* name) {
  nx_simple_map_entry* e=nx_simple_map_find_nocase(map, name);
  return e? e->value : 0;
}

static inline nx_simple_map_entry* nx_simple_map_add(nx_simple_map_entry* map, nx_simple_map_entry* new_entry) {
  new_entry->next=map;
  return new_entry; // returns pointer to new map
}

static inline nx_simple_map_entry* nx_simple_map_remove(nx_simple_map_entry** map, const char* name) {
  nx_simple_map_entry* me=*map;
  nx_simple_map_entry* prev=0;
  while (me) {
    if (strcmp(me->name, name)==0) {
      if (prev) prev->next=me->next;
      else *map=me->next;
      return me;
    }
    prev=me;
    me=me->next;
  }
  return 0; // returns pointer to removed entry; modifies map
}

static inline nx_simple_map_entry* nx_simple_map_remove_nocase(nx_simple_map_entry** map, const char* name) {
  nx_simple_map_entry* me=*map;
  nx_simple_map_entry* prev=0;
  while (me) {
    if (nx_strcasecmp(me->name, name)==0) {
      if (prev) prev->next=me->next;
      else *map=me->next;
      return me;
    }
    prev=me;
    me=me->next;
  }
  return 0; // returns pointer to removed entry; modifies map
}

static inline nx_simple_map_entry* nx_simple_map_itr_begin(nx_simple_map_entry* map) {
  return map;
}

static inline nx_simple_map_entry* nx_simple_map_itr_next(nx_simple_map_entry* itr) {
  return itr->next;
}


int nxweb_listen(const char* host_and_port, int backlog);
int nxweb_listen_ssl(const char* host_and_port, int backlog, _Bool secure, const char* cert_file, const char* key_file, const char* dh_params_file, const char* cipher_priority_string);
int nxweb_setup_http_proxy_pool(int idx, const char* host_and_port);
void nxweb_set_timeout(enum nxweb_timers timer_idx, nxe_time_t timeout);
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

static inline int nxweb_url_prefix_match(const char* url, int url_len, const char* prefix, int prefix_len) {
  if (url_len<prefix_len) return 0;
  char endc=url[prefix_len];
  if (endc && endc!='/' && endc!='?' && endc!=';') return 0;
  if (prefix_len==1) return 1; // prefix=="/" && endc test passed => match
  if (url[1]!=prefix[1]) return 0; // test first char after slash to avoid unnecessary strncmp() call (might speed it up)
  return !strncmp(url, prefix, prefix_len);
}

static inline int nxweb_vhost_match(const char* host, int host_len, const char* vhost_suffix, int vhost_suffix_len) {
  if (*vhost_suffix=='.') {
    if (vhost_suffix_len==host_len+1) return !strncmp(host, vhost_suffix+1, host_len);
    if (vhost_suffix_len<=host_len) return !strncmp(host+(host_len-vhost_suffix_len), vhost_suffix, vhost_suffix_len);
    return 0;
  }
  else {
    return vhost_suffix_len==host_len && !strncmp(host, vhost_suffix, host_len);
  }
}

const nxweb_mime_type* nxweb_get_default_mime_type();
void nxweb_set_default_mime_type(const nxweb_mime_type* mtype);
const nxweb_mime_type* nxweb_get_mime_type(const char* type_name);
const nxweb_mime_type* nxweb_get_mime_type_by_ext(const char* fpath_or_ext);

char* nxweb_url_decode(char* src, char* dst); // can do it inplace (dst=0)

void nxweb_set_response_status(nxweb_http_response* resp, int code, const char* message);
void nxweb_set_response_content_type(nxweb_http_response* resp, const char* content_type);
void nxweb_set_response_charset(nxweb_http_response* resp, const char* charset);
void nxweb_add_response_header(nxweb_http_response* resp, const char* name, const char* value);
void nxweb_add_response_header_safe(nxweb_http_response* resp, const char* name, const char* value);

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

void nxweb_send_redirect(nxweb_http_response* resp, int code, const char* location, int secure);
void nxweb_send_redirect2(nxweb_http_response *resp, int code, const char* location, const char* location_path_info, int secure);
void nxweb_send_http_error(nxweb_http_response* resp, int code, const char* message);
int nxweb_send_file(nxweb_http_response *resp, char* fpath, const struct stat* finfo, int gzip_encoded,
        off_t offset, size_t size, const nxweb_mime_type* mtype, const char* charset); // finfo and mtype could be null => autodetect
void nxweb_send_data(nxweb_http_response *resp, const void* data, size_t size, const char* content_type);

int nxweb_format_http_time(char* buf, struct tm* tm); // eg. Tue, 24 Jan 2012 13:05:54 GMT
int nxweb_format_iso8601_time(char* buf, struct tm* tm); // YYYY-MM-DDTHH:MM:SS
time_t nxweb_parse_http_time(const char* str);
int nxweb_remove_dots_from_uri_path(char* path);

void nxweb_set_request_data(nxweb_http_request* req, nxe_data key, nxe_data value, nxweb_http_request_data_finalizer finalize);
nxweb_http_request_data* nxweb_find_request_data(nxweb_http_request* req, nxe_data key);
nxe_data nxweb_get_request_data(nxweb_http_request* req, nxe_data key);

typedef struct nxweb_composite_stream_node {
  nxd_streamer_node snode;
  struct nxweb_composite_stream_node* next;
  int fd;
  nxweb_http_server_connection* subconn;
  union {
    nxd_obuffer ob;
    nxd_fbuffer fb;
  } buffer;
} nxweb_composite_stream_node;

typedef struct nxweb_composite_stream {
  nxd_streamer strm;
  nxweb_http_server_connection* conn;
  nxweb_http_request* req;
  nxweb_composite_stream_node* first_node;
} nxweb_composite_stream;

nxweb_composite_stream* nxweb_composite_stream_init(nxweb_http_server_connection* conn, nxweb_http_request* req);
void nxweb_composite_stream_append_bytes(nxweb_composite_stream* cs, const char* bytes, int length);
void nxweb_composite_stream_append_fd(nxweb_composite_stream* cs, int fd, off_t offset, off_t end);
void nxweb_composite_stream_append_subrequest(nxweb_composite_stream* cs, const char* host, const char* url);
void nxweb_composite_stream_start(nxweb_composite_stream* cs, nxweb_http_response* resp);
void nxweb_composite_stream_close(nxweb_composite_stream* cs); // call this right after appending last node

void nxweb_access_log_restart(); // specify access log file path in nxweb_server_config.access_log_fpath
void nxweb_access_log_stop();
void nxweb_access_log_thread_flush(); // flush net_thread's access log
void nxweb_access_log_add_frag(nxweb_http_request* req, nxweb_log_fragment* frag); // collect info to log
void nxweb_access_log_write(nxweb_http_request* req); // write request's log record to thread's buffer
void nxweb_access_log_on_request_received(nxweb_http_server_connection* conn, nxweb_http_request* req);
void nxweb_access_log_on_request_complete(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp);
void nxweb_access_log_on_proxy_response(nxweb_http_request* req, nxd_http_proxy* hpx, nxweb_http_response* proxy_resp);

// Internal use only:
char* _nxweb_find_end_of_http_headers(char* buf, int len, char** start_of_body);
int _nxweb_parse_http_request(nxweb_http_request* req, char* headers, char* end_of_headers);
void _nxweb_decode_chunked_request(nxweb_http_request* req);
nxe_ssize_t _nxweb_decode_chunked(char* buf, nxe_size_t buf_len);
nxe_ssize_t _nxweb_verify_chunked(const char* buf, nxe_size_t buf_len);
int _nxweb_decode_chunked_stream(nxweb_chunked_decoder_state* decoder_state, char* buf, nxe_size_t* buf_len);
void _nxweb_encode_chunked_init(nxweb_chunked_encoder_state* encoder_state);
int _nxweb_encode_chunked_stream(nxweb_chunked_encoder_state* encoder_state, nxe_size_t* offered_size, void** send_ptr, nxe_size_t* send_size, nxe_flags_t* flags);
void _nxweb_encode_chunked_advance(nxweb_chunked_encoder_state* encoder_state, nxe_ssize_t pos_delta);
int _nxweb_encode_chunked_is_complete(nxweb_chunked_encoder_state* encoder_state);
void _nxweb_register_printf_extensions();
nxweb_http_response* _nxweb_http_response_init(nxweb_http_response* resp, nxb_buffer* nxb, nxweb_http_request* req);
void _nxweb_add_extra_response_headers(nxb_buffer* nxb, nxweb_http_header *headers);
void _nxweb_prepare_response_headers(nxe_loop* loop, nxweb_http_response* resp);
const char* _nxweb_prepare_client_request_headers(nxweb_http_request *req);
int _nxweb_parse_http_response(nxweb_http_response* resp, char* headers, char* end_of_headers);
void _nxb_append_escape_url(nxb_buffer* nxb, const char* url);
void _nxb_append_encode_file_path(nxb_buffer* nxb, const char* path);

// built-in handlers:
extern nxweb_handler nxweb_http_proxy_handler;
extern nxweb_handler nxweb_sendfile_handler;
extern nxweb_handler nxweb_host_redirect_handler;
#ifdef WITH_PYTHON
extern nxweb_handler nxweb_python_handler;
#endif

// built-in filters:
nxweb_filter* nxweb_file_cache_filter_setup(const char* cache_dir);
#ifdef WITH_ZLIB
nxweb_filter* nxweb_gzip_filter_setup(int compression_level, const char* cache_dir);
#endif
#ifdef WITH_IMAGEMAGICK
nxweb_filter* nxweb_image_filter_setup(const char* cache_dir, nxweb_image_filter_cmd* allowed_cmds, const char* sign_key);
nxweb_filter* nxweb_draw_filter_setup(const char* font_file);
#endif
extern nxweb_filter ssi_filter;
extern nxweb_filter templates_filter;

#include "templates.h"

#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_H */

