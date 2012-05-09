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

#include <unistd.h>

// Note: see config.h for most nxweb #defined parameters

#define NXWEB_LISTEN_HOST_AND_PORT ":8055"
#define NXWEB_LISTEN_HOST_AND_PORT_SSL ":8056"

#define NXWEB_DEFAULT_CHARSET "utf-8"
#define NXWEB_DEFAULT_INDEX_FILE "index.htm"

// All paths are relative to working directory:
#define SSL_CERT_FILE "ssl/server_cert.pem"
#define SSL_KEY_FILE "ssl/server_key.pem"
#define SSL_DH_PARAMS_FILE "ssl/dh.pem"

#define SSL_PRIORITIES "NORMAL:+VERS-TLS-ALL:+COMP-ALL:-CURVE-ALL:+CURVE-SECP256R1"

// Setup modules & handlers (this can be done from any file linked to nxweb):

extern nxweb_handler hello_handler;
extern nxweb_handler benchmark_handler;
extern nxweb_handler benchmark_handler_inworker;
extern nxweb_handler test_handler;
extern nxweb_handler sendfile_handler;
extern nxweb_handler upload_handler;

#ifdef WITH_ZLIB
extern nxweb_filter gzip_filter;
#endif
#ifdef WITH_IMAGEMAGICK
extern nxweb_filter image_filter;
#endif

// These are benchmarking handlers (see modules/benchmark.c):
NXWEB_SET_HANDLER(benchmark, "/benchmark-inprocess", &benchmark_handler, .priority=100);
NXWEB_SET_HANDLER(benchmark_inworker, "/benchmark-inworker", &benchmark_handler_inworker, .priority=100);
NXWEB_SET_HANDLER(test, "/test", &test_handler, .priority=900);

// This is sample handler (see modules/hello.c):
NXWEB_SET_HANDLER(hello, "/hello", &hello_handler, .priority=1000, .filters={
#ifdef WITH_ZLIB
  &gzip_filter
#endif
});

// This is sample handler (see modules/upload.c):
NXWEB_SET_HANDLER(upload, "/upload", &upload_handler, .priority=1000);

// This proxies requests to backend number 0 (see proxy setup further below):
NXWEB_SET_HANDLER(java_test, "/java-test", &nxweb_http_proxy_handler, .priority=10000, .idx=0, .uri="/java-test");

// This proxies requests to backend number 1 (I have another nxweb listening at port 8777):
NXWEB_SET_HANDLER(nxweb_8777, "/8777", &nxweb_http_proxy_handler, .priority=10000, .idx=1, .uri="");

// This serves static files from $(work_dir)/www/root directory:
NXWEB_SET_HANDLER(sendfile, 0, &sendfile_handler, .priority=900000,
        .filters={
#ifdef WITH_IMAGEMAGICK
          &image_filter,
#endif
#ifdef WITH_ZLIB
          &gzip_filter
#endif
        }, .dir="www/root",
        .charset=NXWEB_DEFAULT_CHARSET, .index_file=NXWEB_DEFAULT_INDEX_FILE,
        .gzip_dir="www/cache/gzip", .img_dir="www/cache/img", .cache=1);


// Command-line options:
static const char* user_name=0;
static const char* group_name=0;
static int port=8055;
static int ssl_port=8056;


// Server main():

static void server_main() {

  // Bind listening interfaces:
  char host_and_port[32];
  snprintf(host_and_port, sizeof(host_and_port), ":%d", port);
  if (nxweb_listen(host_and_port, 4096)) return; // simulate normal exit so nxweb is not respawned
#ifdef WITH_SSL
  char ssl_host_and_port[32];
  snprintf(ssl_host_and_port, sizeof(ssl_host_and_port), ":%d", ssl_port);
  if (nxweb_listen_ssl(ssl_host_and_port, 1024, 1, SSL_CERT_FILE, SSL_KEY_FILE, SSL_DH_PARAMS_FILE, SSL_PRIORITIES)) return; // simulate normal exit so nxweb is not respawned
#endif // WITH_SSL

  // Drop privileges:
  if (nxweb_drop_privileges(group_name, user_name)==-1) return;

  // Setup proxies:
  nxweb_setup_http_proxy_pool(0, "localhost:8080");
  nxweb_setup_http_proxy_pool(1, "localhost:8777");

  // Override default timers (if needed):
  //nxweb_set_timeout(NXWEB_TIMER_KEEP_ALIVE, 120);

  // Go!
  nxweb_run();
}


// Utility stuff:

static void show_help(void) {
  printf( "usage:    nxweb <options>\n\n"
          " -d       run as daemon\n"
          " -s       shutdown nxweb\n"
          " -w dir   set work dir    (default: ./)\n"
          " -l file  set log file    (default: stderr or nxweb_error_log for daemon)\n"
          " -p file  set pid file    (default: nxweb.pid)\n"
          " -u user  set process uid\n"
          " -g group set process gid\n"
          " -P port  set http port\n"
#ifdef WITH_SSL
          " -S port  set https port\n"
#endif
          " -h       show this help\n"
          " -v       show version\n"
          "\n"
          "example:  nxweb -d -l nxweb_error_log\n\n"
         );
}

int main(int argc, char** argv) {
  int daemon=0;
  int shutdown=0;
  const char* work_dir=0;
  const char* log_file=0;
  const char* pid_file="nxweb.pid";

  int c;
  while ((c=getopt(argc, argv, ":hvdsw:l:p:u:g:P:S:"))!=-1) {
    switch (c) {
      case 'h':
        show_help();
        return 0;
      case 'v':
        printf( "NXWEB - ultra-fast and super-lightweight web server\n"
                "version:      nxweb/" REVISION "\n"
                "build-date:   " __DATE__ " " __TIME__ "\n"
                "project page: https://bitbucket.org/yarosla/nxweb/\n"
               );
        return 0;
      case 'd':
        daemon=1;
        break;
      case 's':
        shutdown=1;
        break;
      case 'w':
        work_dir=optarg;
        break;
      case 'l':
        log_file=optarg;
        break;
      case 'p':
        pid_file=optarg;
        break;
      case 'u':
        user_name=optarg;
        break;
      case 'g':
        group_name=optarg;
        break;
      case 'P':
        port=atoi(optarg);
        if (port<=0) {
          fprintf(stderr, "invalid port: %s\n\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      case 'S':
        ssl_port=atoi(optarg);
        if (ssl_port<=0) {
          fprintf(stderr, "invalid ssl port: %s\n\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      case '?':
        fprintf(stderr, "unkown option: -%c\n\n", optopt);
        show_help();
        return EXIT_FAILURE;
    }
  }

  if ((argc-optind)>0) {
    fprintf(stderr, "too many arguments\n\n");
    show_help();
    return EXIT_FAILURE;
  }

  if (shutdown) {
    nxweb_shutdown_daemon(work_dir, pid_file);
    return EXIT_SUCCESS;
  }

  if (daemon) {
    if (!log_file) log_file="nxweb_error_log";
    nxweb_run_daemon(work_dir, log_file, pid_file, server_main);
  }
  else {
    nxweb_run_normal(work_dir, log_file, pid_file, server_main);
  }
  return EXIT_SUCCESS;
}
