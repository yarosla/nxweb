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

#include "nxweb/nxweb.h"

// Note: see config.h for most nxweb #defined parameters

#define NXWEB_DEFAULT_CHARSET "utf-8"
#define NXWEB_DEFAULT_INDEX_FILE "index.htm"

// All paths are relative to working directory:
#define SSL_CERT_FILE "ssl/server_cert.pem"
#define SSL_KEY_FILE "ssl/server_key.pem"
#define SSL_DH_PARAMS_FILE "ssl/dh.pem"

#define SSL_PRIORITIES "NORMAL:+VERS-TLS-ALL:+COMP-ALL:-CURVE-ALL:+CURVE-SECP256R1"

// Command-line defaults (parsed by nxweb_main_stub()):
nxweb_main_args_t nxweb_main_args={
  .user_name=0,
  .group_name=0,
  .port=8055,
  .ssl_port=8056
};

static void server_config() {

  // Bind listening interfaces:
  char host_and_port[64];
  if (nxweb_main_args.listening_interface_ip) {
    snprintf(host_and_port, sizeof(host_and_port), "%s:%d", nxweb_main_args.listening_interface_ip, nxweb_main_args.port);
  }
  else {
    snprintf(host_and_port, sizeof(host_and_port), ":%d", nxweb_main_args.port);
  }
  if (nxweb_listen(host_and_port, 4096)) return; // simulate normal exit so nxweb is not respawned
#ifdef WITH_SSL
  char ssl_host_and_port[64];
  if (nxweb_main_args.listening_interface_ip) {
    snprintf(ssl_host_and_port, sizeof(ssl_host_and_port), "%s:%d", nxweb_main_args.listening_interface_ip, nxweb_main_args.ssl_port);
  }
  else {
    snprintf(ssl_host_and_port, sizeof(ssl_host_and_port), ":%d", nxweb_main_args.ssl_port);
  }
  if (nxweb_listen_ssl(ssl_host_and_port, 1024, 1, SSL_CERT_FILE, SSL_KEY_FILE, SSL_DH_PARAMS_FILE, SSL_PRIORITIES)) return; // simulate normal exit so nxweb is not respawned
#endif // WITH_SSL

  // Drop privileges:
  if (nxweb_drop_privileges(nxweb_main_args.group_name, nxweb_main_args.user_name)==-1) return;

  // Setup proxies:
  nxweb_setup_http_proxy_pool(0, "localhost:8000"); // backend1
  nxweb_setup_http_proxy_pool(1, "localhost:8080"); // backend2

  ////////////////////
  // Setup handlers:

  extern nxweb_handler hello_handler;
  extern nxweb_handler benchmark_handler;
  extern nxweb_handler benchmark_handler_inworker;
  extern nxweb_handler test_handler;
  extern nxweb_handler upload_handler;
  extern nxweb_handler subreq_handler;
  extern nxweb_handler tmpl_handler;
  extern nxweb_handler curtime_handler;

  // These are benchmarking handlers (see modules/benchmark.c):
  NXWEB_HANDLER_SETUP(benchmark, "/benchmark-inprocess", &benchmark_handler, .priority=100);
  NXWEB_HANDLER_SETUP(benchmark_inworker, "/benchmark-inworker", &benchmark_handler_inworker, .priority=100);
  NXWEB_HANDLER_SETUP(test, "/test", &test_handler, .priority=900);

  // This is sample handler (see modules/hello.c):
  NXWEB_HANDLER_SETUP(hello, "/hello", &hello_handler, .priority=1000, .filters={
  #ifdef WITH_ZLIB
    nxweb_gzip_filter_setup(4, "www/cache/gzip/hello"),
  #endif
  });

  // This handler proxies requests to backend with index 0 (see proxy setup further below):
  NXWEB_PROXY_SETUP(backend1, "/backend1", .priority=10000, .idx=0, .uri="",
        .filters={
            nxweb_file_cache_filter_setup("www/cache/proxy"), &templates_filter, &ssi_filter
         });

  // This handler proxies requests to backend with index 1 (see proxy setup further below):
  NXWEB_PROXY_SETUP(backend2, "/backend2", .priority=10000, .idx=1, .uri="",
        .filters={
            nxweb_file_cache_filter_setup("www/cache/proxy"), &templates_filter, &ssi_filter
         });

  // This is sample handler (see modules/upload.c):
  NXWEB_HANDLER_SETUP(upload, "/upload", &upload_handler, .priority=200000);

  // These are sample handlers (see modules/subrequests.c):
  NXWEB_HANDLER_SETUP(subreq, "/subreq", &subreq_handler, .priority=200000);
  NXWEB_HANDLER_SETUP(tmpl, "/tmpl", &tmpl_handler, .priority=200000);
  NXWEB_HANDLER_SETUP(curtime, "/curtime", &curtime_handler, .priority=200000,
                      .filters={ nxweb_file_cache_filter_setup("www/cache/curtime") });
#ifdef WITH_IMAGEMAGICK
  extern nxweb_handler captcha_handler;
  NXWEB_HANDLER_SETUP(captcha, "/captcha", &captcha_handler, .priority=200000,
                      .filters={ nxweb_draw_filter_setup("www/fonts/Sansation/Sansation_Bold.ttf") });
#endif

  // This handler serves static files from $(work_dir)/www/root directory:
  NXWEB_SENDFILE_SETUP(sendfile1, 0, .priority=900000,
        .dir="www/root", .memcache=1,
        .charset=NXWEB_DEFAULT_CHARSET, .index_file=NXWEB_DEFAULT_INDEX_FILE,
        .filters={
          &templates_filter,
          &ssi_filter,
#ifdef WITH_IMAGEMAGICK
          nxweb_image_filter_setup("www/cache/img", 0, 0),
#endif
#ifdef WITH_ZLIB
          nxweb_gzip_filter_setup(4, "www/cache/gzip"),
#endif
         });

#ifdef WITH_PYTHON
  NXWEB_PYTHON_SETUP(python, "/py", .priority=950000,
        .filters={
            nxweb_file_cache_filter_setup("www/cache/python"), &templates_filter, &ssi_filter
         });
#endif

  // set error log verbosity: INFO=most verbose, WARNING, ERROR, NONE
  nxweb_error_log_level=NXWEB_LOG_INFO;

  // Override default timers (if needed):
  //nxweb_set_timeout(NXWEB_TIMER_KEEP_ALIVE, 120);

  // Go!
  nxweb_run();
}

int main(int argc, char** argv) {
  return nxweb_main_stub(argc, argv, server_config);
}
