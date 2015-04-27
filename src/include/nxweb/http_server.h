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

#ifndef NXWEB_SERVER_H
#define	NXWEB_SERVER_H

#include "misc.h"


#ifdef	__cplusplus
extern "C" {
#endif

//typedef enum nxweb_handler_event {
//  NXWEB_EV_RECEIVED_HEADERS=10,
//  NXWEB_EV_RECEIVED_BODY=20,
//  NXWEB_EV_COMPLETE=90,
//  NXWEB_EV_ERROR=-1
//} nxweb_handler_event;

typedef enum nxweb_result {
  NXWEB_OK=0,
  NXWEB_NEXT=1,
  NXWEB_ASYNC=2,
  NXWEB_DELAY=3,
  NXWEB_ERROR=-1,
  NXWEB_REVALIDATE=-2,
  NXWEB_MISS=-3
} nxweb_result;

typedef enum nxweb_handler_flags {
  NXWEB_INPROCESS=0, // execute handler in network thread (must be fast and non-blocking!)
  NXWEB_INWORKER=1, // execute handler in worker thread (for lengthy or blocking operations)
  NXWEB_PARSE_PARAMETERS=2, // parse query string and (url-encoded) post data before calling this handler
  NXWEB_PRESERVE_URI=4, // modifier for NXWEB_PARSE_PARAMETERS; preserver conn->uri string while parsing (allocate copy)
  NXWEB_PARSE_COOKIES=8, // parse cookie header before calling this handler
  NXWEB_HANDLE_GET=0x10,
  NXWEB_HANDLE_POST=0x20, // implies NXWEB_ACCEPT_CONTENT
  NXWEB_HANDLE_OTHER=0x40,
  NXWEB_HANDLE_ANY=0x70,
  NXWEB_ACCEPT_CONTENT=0x80, // handler accepts request body
  _NXWEB_HANDLE_MASK=0x70,
  _NXWEB_HOST_DEPENDENT_DIR=0x1000 // sendfile handler to use host name to build root directory
} nxweb_handler_flags;

struct nxweb_http_server_connection;

typedef nxweb_result (*nxweb_handler_callback)(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp);

struct fc_filter_data;

typedef struct nxweb_filter_data {
  const char* cache_key;
  struct fc_filter_data* fcache;
  unsigned bypass:1;
} nxweb_filter_data;

typedef struct nxweb_filter {
  const char* name;
  nxweb_filter_data* (*init)(struct nxweb_filter* fparam, struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp);
  const char* (*decode_uri)(struct nxweb_filter* fparam, struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* uri);
  nxweb_result (*translate_cache_key)(struct nxweb_filter* fparam, struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key);
  nxweb_result (*serve_from_cache)(struct nxweb_filter* fparam, struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, time_t check_time);
  nxweb_result (*do_filter)(struct nxweb_filter* fparam, struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata);
  void (*finalize)(struct nxweb_filter* fparam, struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata);
  struct nxweb_filter* next_defined;
  struct nxweb_filter* (*config)(struct nxweb_filter* base, const struct nx_json* json);
} nxweb_filter;

struct fc_filter_data* _nxweb_fc_create(nxb_buffer* nxb, const char* cache_dir);
void _nxweb_fc_init(struct fc_filter_data* fcdata, const char* cache_dir);
void _nxweb_fc_finalize(struct fc_filter_data* fcdata);
nxweb_result _nxweb_fc_serve_from_cache(struct nxweb_http_server_connection* conn, struct nxweb_http_request* req, struct nxweb_http_response* resp, const char* cache_key, struct fc_filter_data* fcdata, time_t check_time);
nxweb_result _nxweb_fc_do_filter(struct nxweb_http_server_connection* conn, struct nxweb_http_request* req, struct nxweb_http_response* resp, struct fc_filter_data* fcdata);

typedef struct nxweb_handler {
  const char* name;
  const char* prefix;
  int prefix_len;
  const char* vhost;
  int vhost_len;
  int priority;
  nxweb_handler_flags flags;

  // various handler parameters
  nxe_data param;
  const char* host;
  const char* uri;
  const char* dir;
  const char* charset;
  const char* index_file;
  nxe_ssize_t size;
  _Bool memcache:1;
  _Bool proxy_copy_host:1;
  _Bool secure_only:1;
  _Bool insecure_only:1;
  int idx;

  struct nxweb_handler* next; // next in routing list
  nxweb_filter* filters[NXWEB_MAX_FILTERS];
  int num_filters;
  nxweb_handler_callback on_generate_cache_key;
  nxweb_handler_callback on_select;
  nxweb_handler_callback on_headers;
  nxweb_handler_callback on_post_data;
  nxweb_handler_callback on_post_data_complete;
  nxweb_handler_callback on_request;
  nxweb_handler_callback on_complete;
  nxweb_handler_callback on_error;

  nxweb_result (*on_config)(struct nxweb_handler* handler, const struct nx_json* js);
  nxe_data config;

  struct nxweb_handler* next_defined; // next in chain of defined handlers
} nxweb_handler;

typedef struct nxweb_module {
  const char* name;
  int priority;
  _Bool configured:1;
  struct nxweb_module* next;
  nxweb_handler_callback request_dispatcher;
  int (*on_server_startup)();
  int (*on_thread_startup)();
  void (*on_thread_shutdown)();
  void (*on_server_shutdown)();
  void (*on_config)(const struct nx_json* js);
  void (*on_server_diagnostics)();
  void (*on_thread_diagnostics)();
} nxweb_module;

typedef struct nxweb_http_server_listening_socket {
  int idx;
  nxe_listenfd_source listen_source;
  nxe_subscriber listen_sub;
  nxe_timer accept_retry_timer;
} nxweb_http_server_listening_socket;

#define NXWEB_ACCESS_LOG_BLOCK_SIZE 32768

typedef struct nxweb_net_thread_data {
  pthread_t thread_id;
  uint8_t thread_num; // up to 256 net threads
  uint64_t unique_num;
  nxe_loop* loop;
  nxe_eventfd_source shutdown_efs;
  nxweb_http_server_listening_socket listening_sock[NXWEB_MAX_LISTEN_SOCKETS];
  nxe_subscriber shutdown_sub;
  nxe_subscriber gc_sub;
  nxw_factory workers_factory;
  nxp_pool* free_conn_pool;
  nxp_pool* free_conn_nxb_pool;
  nxp_pool* free_rbuf_pool;

  char* access_log_block;
  int access_log_block_avail;
  char* access_log_block_ptr;

  nxd_http_proxy_pool proxy_pool[NXWEB_MAX_PROXY_POOLS];

  nxe_eventfd_source diagnostics_efs;
  nxe_subscriber diagnostics_sub;
} nxweb_net_thread_data __attribute__ ((aligned(64)));

typedef struct nxweb_http_server_connection {
  nxd_http_server_proto hsp;
#ifdef WITH_SSL
  nxd_ssl_socket sock; // could be either socket or ssl_socket (ssl_socket struct is larger)
#else
  nxd_socket sock;
#endif // WITH_SSL
  nxe_subscriber events_sub;
  nxe_subscriber worker_complete;
  volatile int worker_job_done;
  char remote_addr[16]; // 255.255.255.255
  nxweb_handler* handler;
  nxe_data handler_param;
  nxweb_net_thread_data* tdata;
  int lconf_idx;
  _Bool secure:1;
  _Bool response_ready:1;
  _Bool subrequest_failed:1;
  _Bool in_worker:1;
  _Bool connection_closing:1;
  uint64_t uid; // unique connection id
  nxe_time_t connected_time;
  struct nxweb_http_server_connection* parent;
  struct nxweb_http_server_connection* subrequests;
  struct nxweb_http_server_connection* next;
  void (*on_response_ready)(struct nxweb_http_server_connection* conn, nxe_data data);
  nxe_data on_response_ready_data;
  nxd_ibuffer ib;
} nxweb_http_server_connection;

typedef struct nxweb_http_proxy_pool_config {
  const char* host;
  struct addrinfo* saddr;
} nxweb_http_proxy_pool_config;

typedef struct nxweb_server_listen_config {
  int listen_fd;
  _Bool secure:1;
#ifdef WITH_SSL
  gnutls_certificate_credentials_t x509_cred;
  gnutls_priority_t priority_cache;
  gnutls_dh_params_t dh_params;
  gnutls_datum_t session_ticket_key;
#endif // WITH_SSL
} nxweb_server_listen_config;

struct nxweb_server_config {
  int listen_config_idx;
  nxweb_server_listen_config listen_config[NXWEB_MAX_LISTEN_SOCKETS];
  nxweb_http_proxy_pool_config http_proxy_pool_config[NXWEB_MAX_PROXY_POOLS];
  nxweb_handler_callback request_dispatcher;
  nxweb_handler* handler_list;
  nxweb_handler* handlers_defined;
  nxweb_filter* filters_defined;
  nxweb_module* module_list;
  int shutdown_timeout; // time in secs to close up after SIGTERM
  char* work_dir;
  const char* access_log_fpath;
  const char* error_log_fpath;
  int access_log_fd;
  pthread_mutex_t access_log_start_mux;
  void (*access_log_on_request_received)(nxweb_http_server_connection* conn, nxweb_http_request* req);
  void (*access_log_on_request_complete)(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp);
  void (*access_log_on_proxy_response)(nxweb_http_request* req, nxd_http_proxy* hpx, nxweb_http_response* proxy_resp);
};

typedef struct nxweb_image_filter_cmd {
  char cmd;
  _Bool dont_watermark:1;
  _Bool gravity_top:1;
  _Bool gravity_right:1;
  _Bool gravity_bottom:1;
  _Bool gravity_left:1;
  int width;
  int height;
  char color[8]; // "#FF00AA\0"
  char* cmd_string;
  const char* query_string;
  const char* uri_path;
  const nxweb_mime_type* mtype;
} nxweb_image_filter_cmd;

extern struct nxweb_server_config nxweb_server_config;
extern nxweb_net_thread_data _nxweb_net_threads[];
extern int _nxweb_num_net_threads;
extern __thread struct nxweb_net_thread_data* _nxweb_net_thread_data;
extern __thread struct nxw_worker* _nxweb_worker_thread_data;

void _nxweb_register_module(nxweb_module* module);
void _nxweb_define_handler_base(nxweb_handler* handler);
void _nxweb_define_filter(nxweb_filter* filter);
void _nxweb_register_handler(nxweb_handler* handler, nxweb_handler* base);
nxweb_result _nxweb_default_request_dispatcher(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp);
void _nxweb_launch_diagnostics(void);

int nxweb_select_handler(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_handler* handler, nxe_data handler_param);

#define NXWEB_MODULE(_name, ...) \
        static nxweb_module _nxweb_ ## _name ## _module={.name=#_name, ## __VA_ARGS__}; \
        static void _nxweb_pre_load_module_ ## _name() __attribute__ ((constructor)); \
        static void _nxweb_pre_load_module_ ## _name() { \
          _nxweb_register_module(&_nxweb_ ## _name ## _module); \
        }

#define NXWEB_DEFINE_HANDLER(_name, ...) \
        nxweb_handler nxweb_ ## _name ## _handler={.name=#_name, ## __VA_ARGS__}; \
        static void _nxweb_define_handler_ ## _name() __attribute__ ((constructor)); \
        static void _nxweb_define_handler_ ## _name() { \
          _nxweb_define_handler_base(&nxweb_ ## _name ## _handler); \
        }

#define NXWEB_DEFINE_FILTER(_name, _filter) \
        static void _nxweb_define_filter_ ## _name() __attribute__ ((constructor)); \
        static void _nxweb_define_filter_ ## _name() { \
          _filter.name=#_name; \
          _nxweb_define_filter(&_filter); \
        }

#define NXWEB_HANDLER_SETUP(_name, _prefix, _base, ...) \
        extern nxweb_handler nxweb_ ## _base ## _handler; \
        nxweb_handler _nxweb_ ## _name ## _handler={.name=#_name, .prefix=(_prefix), ## __VA_ARGS__}; \
        _nxweb_register_handler(&_nxweb_ ## _name ## _handler, &(nxweb_ ## _base ## _handler))

#define NXWEB_SENDFILE_SETUP(_name, _prefix, ...) \
        extern nxweb_handler nxweb_sendfile_handler; \
        nxweb_handler _nxweb_ ## _name ## _handler={.name=#_name, .prefix=(_prefix), ## __VA_ARGS__}; \
        _nxweb_register_handler(&_nxweb_ ## _name ## _handler, &nxweb_sendfile_handler)

#define NXWEB_PROXY_SETUP(_name, _prefix, ...) \
        extern nxweb_handler nxweb_http_proxy_handler; \
        nxweb_handler _nxweb_ ## _name ## _handler={.name=#_name, .prefix=(_prefix), ## __VA_ARGS__}; \
        _nxweb_register_handler(&_nxweb_ ## _name ## _handler, &nxweb_http_proxy_handler)

#ifdef WITH_PYTHON
#define NXWEB_PYTHON_SETUP(_name, _prefix, ...) \
        extern nxweb_handler nxweb_python_handler; \
        nxweb_handler _nxweb_ ## _name ## _handler={.name=#_name, .prefix=(_prefix), ## __VA_ARGS__}; \
        _nxweb_register_handler(&_nxweb_ ## _name ## _handler, &nxweb_python_handler)
#endif

void nxweb_start_sending_response(nxweb_http_server_connection* conn, nxweb_http_response* resp);

void nxweb_http_server_connection_finalize(nxweb_http_server_connection* conn, int good);

nxweb_http_server_connection* nxweb_http_server_subrequest_start(nxweb_http_server_connection* parent_conn, void (*on_response_ready)(nxweb_http_server_connection* conn, nxe_data data), nxe_data on_response_ready_data, const char* host, const char* uri);
void nxweb_http_server_connection_finalize_subrequests(nxweb_http_server_connection* conn, int good);

static inline nxe_time_t nxweb_get_loop_time(nxweb_http_server_connection* conn) {
  return conn->tdata->loop->current_time;
}

static inline uint64_t nxweb_generate_unique_id() {
  nxweb_net_thread_data* tdata=_nxweb_net_thread_data;
  tdata->unique_num++;
  // unique for one nxweb instance within ~2 years time frame if called less than 68 billion times per second
  uint64_t uid=(((uint64_t)tdata->thread_num)<<56) // thread_num is highest byte
          | ((((uint64_t)tdata->loop->current_time) & (0xfffff<<20))<<16) // time value: changes ~ once per second, repeats after ~ two year's time
          | (tdata->unique_num & 0xfffffffff); // repeats after 2^36 = 68'719'476'736 generations
  return uid;
}

nxweb_result nxweb_cache_try(nxweb_http_server_connection* conn, nxweb_http_response* resp, const char* key, time_t if_modified_since, time_t revalidated_mtime);
nxweb_result nxweb_cache_store_response(nxweb_http_server_connection* conn, nxweb_http_response* resp);

#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_SERVER_H */

