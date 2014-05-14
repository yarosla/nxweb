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

#ifndef NXWEB_NXD_H
#define	NXWEB_NXD_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <assert.h>

#include "nx_event.h"

struct nxd_socket;

typedef struct nxd_socket_class {
  void (*shutdown)(struct nxd_socket* sock);
  void (*finalize)(struct nxd_socket* sock, int good);
} nxd_socket_class;

typedef struct nxd_socket {
  const nxd_socket_class* cls;
  nxe_fd_source fs;
  nx_file_reader fr;
} nxd_socket;

void nxd_socket_init(nxd_socket* ss);
void nxd_socket_finalize(nxd_socket* ss, int good);

#ifdef WITH_SSL

#include <gnutls/gnutls.h>

typedef struct nxd_ssl_socket { // extends nxd_socket
  const nxd_socket_class* cls;
  nxe_fd_source fs;
  nxe_istream handshake_stub_is;
  nxe_ostream handshake_stub_os;
  nxe_istream* saved_is;
  nxe_ostream* saved_os;
  gnutls_session_t session;
  nxe_ssize_t buffered_size;
  _Bool handshake_started:1;
  _Bool handshake_complete:1;
  _Bool handshake_failed:1;
} nxd_ssl_socket;

void nxd_ssl_server_socket_init(nxd_ssl_socket* ss, gnutls_certificate_credentials_t x509_cred,
        gnutls_priority_t priority_cache, gnutls_datum_t* session_ticket_key);
void nxd_ssl_server_socket_finalize(nxd_ssl_socket* ss, int good);

int nxd_ssl_socket_init_server_parameters(gnutls_certificate_credentials_t* x509_cred,
        gnutls_dh_params_t* dh_params, gnutls_priority_t* priority_cache, gnutls_datum_t* session_ticket_key,
        const char* cert_file, const char* key_file, const char* dh_file, const char* cipher_priority_string);
void nxd_ssl_socket_finalize_server_parameters(gnutls_certificate_credentials_t x509_cred, gnutls_dh_params_t dh_params,
        gnutls_priority_t priority_cache, gnutls_datum_t* session_ticket_key);

int nxd_ssl_socket_global_init(void);
void nxd_ssl_socket_global_finalize(void);

#endif // WITH_SSL



typedef struct nxd_ibuffer {
  nxb_buffer* nxb;
  nxe_ostream data_in;
  nxe_publisher data_complete;
  char* data_ptr;
  int data_size;
  int max_data_size;
} nxd_ibuffer;

void nxd_ibuffer_init(nxd_ibuffer* ib, nxb_buffer* nxb, int max_data_size);
void nxd_ibuffer_make_room(nxd_ibuffer* ib, int data_size);
char* nxd_ibuffer_get_result(nxd_ibuffer* ib, int* size);


typedef struct nxd_obuffer {
  nxe_istream data_out;
  const char* data_ptr;
  int data_size;
} nxd_obuffer;

void nxd_obuffer_init(nxd_obuffer* ob, const void* data_ptr, int data_size);


typedef struct nxd_rbuffer {
  nxe_istream data_out;
  nxe_ostream data_in;
  char* start_ptr;
  char* end_ptr;
  const char* read_ptr;
  char* write_ptr;
  _Bool last_write:1;
  _Bool eof:1;
} nxd_rbuffer;

void nxd_rbuffer_init(nxd_rbuffer* rb, void* buf, int size);

static inline void nxd_rbuffer_init_ptr(nxd_rbuffer* rb, char* buf, int size) {
  rb->read_ptr=
  rb->start_ptr=
  rb->write_ptr=buf;
  rb->end_ptr=buf+size;
  rb->last_write=0;
}

static inline int nxd_rbuffer_is_empty(nxd_rbuffer* rb) {
  return rb->read_ptr==rb->write_ptr && !rb->last_write;
}

static inline int nxd_rbuffer_is_full(nxd_rbuffer* rb) {
  return rb->read_ptr==rb->write_ptr && rb->last_write;
}

static inline char* nxd_rbuffer_get_write_ptr(nxd_rbuffer* rb, nxe_size_t* size) {
  *size=nxd_rbuffer_is_full(rb)? 0 :
    (rb->read_ptr > rb->write_ptr ? rb->read_ptr - rb->write_ptr : rb->end_ptr - rb->write_ptr);
  return rb->write_ptr;
}

static inline const char* nxd_rbuffer_get_read_ptr(nxd_rbuffer* rb, nxe_size_t* size, nxe_flags_t* flags) {
  *size=nxd_rbuffer_is_empty(rb)? 0 :
    (rb->write_ptr > rb->read_ptr ? rb->write_ptr - rb->read_ptr : rb->end_ptr - rb->read_ptr);
  if (rb->eof && (!*size || rb->write_ptr > rb->read_ptr || rb->write_ptr == rb->start_ptr)) *flags|=NXEF_EOF;
  return rb->read_ptr;
}

void nxd_rbuffer_read(nxd_rbuffer* rb, int size);
void nxd_rbuffer_write(nxd_rbuffer* rb, int size);


typedef struct nxd_fbuffer {
  nxe_istream data_out;
  int fd;
  off_t offset;
  off_t end;
  nx_file_reader fr;
} nxd_fbuffer;

void nxd_fbuffer_init(nxd_fbuffer* fb, int fd, off_t offset, off_t end);
void nxd_fbuffer_finalize(nxd_fbuffer* fb);


typedef struct nxd_fwbuffer {
  nxe_ostream data_in;
  int fd;
  int error;            // errno
  nxe_size_t size;      // total bytes received via data_in
  nxe_size_t max_size;  // max bytes to store in file
} nxd_fwbuffer;

void nxd_fwbuffer_init(nxd_fwbuffer* fwb, int fd, nxe_size_t max_size);
void nxd_fwbuffer_finalize(nxd_fwbuffer* fwb);


typedef struct nxd_streamer_node {
  unsigned final:1;
  unsigned complete:1;
  struct nxd_streamer* strm;
  nxe_ostream data_in;
  struct nxd_streamer_node* next;
} nxd_streamer_node;

typedef struct nxd_streamer {
  nxe_istream data_out;
  nxd_streamer_node* head;
  nxd_streamer_node* current;
  unsigned running:1;
  unsigned force_eof:1;
} nxd_streamer;

void nxd_streamer_init(nxd_streamer* strm);
void nxd_streamer_add_node(nxd_streamer* strm, nxd_streamer_node* snode, int final);
void nxd_streamer_start(nxd_streamer* strm);
void nxd_streamer_finalize(nxd_streamer* strm);
void nxd_streamer_node_init(nxd_streamer_node* snode);
void nxd_streamer_node_finalize(nxd_streamer_node* snode);
void nxd_streamer_node_start(nxd_streamer_node* snode);
void nxd_streamer_close(nxd_streamer* strm);


struct nxd_http_server_proto;

typedef struct nxd_http_server_proto_class {
  void (*finalize)(struct nxd_http_server_proto* hsp);
  void (*start_sending_response)(struct nxd_http_server_proto* hsp, struct nxweb_http_response* resp);
  void (*start_receiving_request_body)(struct nxd_http_server_proto* hsp);
  void (*connect_request_body_out)(struct nxd_http_server_proto* hsp, nxe_ostream* is);
  nxe_ostream* (*get_request_body_out_pair)(struct nxd_http_server_proto* hsp);
  void (*request_cleanup)(nxe_loop* loop, struct nxd_http_server_proto* hsp);
} nxd_http_server_proto_class;

enum nxd_http_server_proto_state {
  HSP_WAITING_FOR_REQUEST=0,
  HSP_RECEIVING_HEADERS,
  HSP_RECEIVING_BODY,
  HSP_HANDLING,
  HSP_SENDING_HEADERS,
  HSP_SENDING_BODY
};

typedef struct nxd_http_server_proto {
  const nxd_http_server_proto_class* cls;
  nxe_ostream data_in;
  nxe_istream data_out;
  nxe_subscriber data_error;
  nxe_publisher events_pub;
  nxe_istream req_body_out;
  nxe_ostream resp_body_in;
  nxe_timer timer_keep_alive;
  nxe_timer timer_read;
  nxe_timer timer_write;
  nxb_buffer* nxb;
  nxp_pool* nxb_pool;
  //char* req_headers;
  //int sock_fd;
  enum nxd_http_server_proto_state state;
  int request_count;
  int headers_bytes_received;
  //unsigned keep_alive:1;
  nxweb_http_request req;
  nxweb_http_response _resp; // embedded response
  nxweb_http_response* resp;
  const char* first_body_chunk;
  const char* first_body_chunk_end;
  const char* resp_headers_ptr;
  nxd_obuffer ob;
  nxd_fbuffer fb;
  void* req_data;
  void (*req_finalize)(struct nxd_http_server_proto* hsp, void* req_data);
} nxd_http_server_proto;

enum nxd_http_server_proto_error_code {
  NXD_HSP_KEEP_ALIVE_TIMEOUT=-27001,
  NXD_HSP_READ_TIMEOUT=-27002,
  NXD_HSP_WRITE_TIMEOUT=-27003,
  NXD_HSP_REQUEST_CHUNKED_ENCODING_ERROR=-27004,
  NXD_HSP_SHUTDOWN_CONNECTION=-27109,
  NXD_HSP_REQUEST_RECEIVED=27101,
  NXD_HSP_REQUEST_BODY_RECEIVED=27102,
  NXD_HSP_RESPONSE_READY=27103,
  NXD_HSP_SUBREQUEST_READY=27104,
  NXD_HSP_REQUEST_COMPLETE=27109
};

void nxd_http_server_proto_init(nxd_http_server_proto* hsp, nxp_pool* nxb_pool);
void nxd_http_server_proto_connect(nxd_http_server_proto* hsp, nxe_loop* loop);
void nxd_http_server_proto_subrequest_init(nxd_http_server_proto* hsp, nxp_pool* nxb_pool);
void nxweb_http_server_proto_subrequest_execute(nxd_http_server_proto* hsp, const char* host, const char* uri, nxweb_http_request* parent_req);
void nxd_http_server_proto_finish_response(nxweb_http_response* resp);
void nxd_http_server_proto_setup_content_out(nxd_http_server_proto* hsp, nxweb_http_response* resp);
void nxweb_reset_content_out(nxd_http_server_proto* hsp, nxweb_http_response* resp);

enum nxd_http_client_proto_state {
  HCP_CONNECTING=0,
  HCP_IDLE,
  HCP_SENDING_HEADERS,
  HCP_WAITING_FOR_100CONTINUE,
  HCP_SENDING_BODY,
  HCP_WAITING_FOR_RESPONSE,
  HCP_RECEIVING_HEADERS,
  HCP_RECEIVING_BODY
};

typedef struct nxd_http_client_proto {
  nxe_ostream data_in;
  nxe_istream data_out;
  nxe_subscriber data_error;
  nxe_publisher events_pub;
  nxe_ostream req_body_in;
  nxe_istream resp_body_out;
  nxe_timer timer_keep_alive;
  nxe_timer timer_read;
  nxe_timer timer_write;
  nxe_timer timer_100_continue;
  nxb_buffer* nxb;
  nxp_pool* nxb_pool;
  const char* host;
  int sock_fd;
  enum nxd_http_client_proto_state state;
  int request_count;
  unsigned chunked_do_not_decode:1;
  unsigned req_body_sending_started:1;
  unsigned response_body_complete:1;
  unsigned request_complete:1;
  unsigned receiving_100_continue:1;
  nxe_data queued_error_message;
  nxweb_http_response resp;
  nxweb_http_request _req;
  nxweb_http_request* req;
  char* first_body_chunk;
  char* first_body_chunk_end;
  const char* req_headers_ptr;
  nxd_obuffer ob;
} nxd_http_client_proto;

enum nxd_http_client_proto_error_code {
  NXD_HCP_KEEP_ALIVE_TIMEOUT=-27001,
  NXD_HCP_READ_TIMEOUT=-27002,
  NXD_HCP_WRITE_TIMEOUT=-27003,
  NXD_HCP_BAD_RESPONSE=-27004,
  NXD_HCP_CONNECT_ERROR=-27005,
  NXD_HCP_RESPONSE_CHUNKED_ENCODING_ERROR=-27006,
  NXD_HCP_NO_100CONTINUE=-27007,
  NXD_HCP_100CONTINUE_TIMEOUT=-27008,
  NXD_HCP_CONNECTED=27101,
  NXD_HCP_RESPONSE_RECEIVED=27102,
  NXD_HCP_REQUEST_COMPLETE=27103
};

void nxd_http_client_proto_init(nxd_http_client_proto* hcp, nxp_pool* nxb_pool);
void nxd_http_client_proto_finalize(nxd_http_client_proto* hcp);
void nxd_http_client_proto_connect(nxd_http_client_proto* hcp, nxe_loop* loop);
void nxd_http_client_proto_start_request(nxd_http_client_proto* hcp, nxweb_http_request* req);
void nxd_http_client_proto_rearm(nxd_http_client_proto* hcp);


typedef struct nxd_http_proxy {
  nxd_socket sock;
  nxd_http_client_proto hcp;
  nxe_subscriber events_sub; // for idle monitoring, etc.
  struct nxd_http_proxy_pool* pool;
  uint64_t uid;
  struct nxd_http_proxy* prev; // used by http_proxy_pool pool
  struct nxd_http_proxy* next;
} nxd_http_proxy;

struct addrinfo;

void nxd_http_proxy_init(nxd_http_proxy* hpx, nxp_pool* nxb_pool);
int nxd_http_proxy_connect(nxd_http_proxy* hpx, nxe_loop* loop, const char* host, struct addrinfo* saddr);
void nxd_http_proxy_finalize(nxd_http_proxy* hpx, int good);
nxweb_http_request* nxd_http_proxy_prepare(nxd_http_proxy* hpx);
void nxd_http_proxy_start_request(nxd_http_proxy* hpx, nxweb_http_request* req);

#define NXD_FREE_PROXY_POOL_INITIAL_SIZE 4
#define NXD_HTTP_PROXY_POOL_TIME_DELTA_SAMPLES 8
#define NXD_HTTP_PROXY_POOL_TIME_DELTA_NO_VALUE 1000000

typedef struct nxd_http_proxy_pool {
  nxe_loop* loop;
  const char* host;
  struct addrinfo* saddr;
  nxd_http_proxy* first;
  nxd_http_proxy* last;
  nxp_pool* free_pool;
  nxp_pool* nxb_pool;
  nxe_subscriber gc_sub;
  time_t backend_time_delta[NXD_HTTP_PROXY_POOL_TIME_DELTA_SAMPLES]; // delta seconds = (backend_date - current_date)
  int backend_time_delta_idx;
  int conn_count;
  int conn_count_max;
} nxd_http_proxy_pool;

void nxd_http_proxy_pool_init(nxd_http_proxy_pool* pp, nxe_loop* loop, nxp_pool* nxb_pool, const char* host, struct addrinfo* saddr);
nxd_http_proxy* nxd_http_proxy_pool_connect(nxd_http_proxy_pool* pp);
void nxd_http_proxy_pool_return(nxd_http_proxy* hpx, int closed);
void nxd_http_proxy_pool_finalize(nxd_http_proxy_pool* pp);
void nxd_http_proxy_pool_report_backend_time_delta(nxd_http_proxy_pool* pp, time_t delta);
time_t nxd_http_proxy_pool_get_backend_time_delta(nxd_http_proxy_pool* pp);

#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_NXD_H */

