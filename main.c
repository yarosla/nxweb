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

#include "nxweb/nxweb.h"

#include <unistd.h>

// Note: see config.h for most nxweb #defined parameters


// Setup modules & handlers (this can be done from any file linked to nxweb):

extern nxweb_handler hello_handler;
extern nxweb_handler benchmark_handler;
extern nxweb_handler benchmark_handler_inworker;
extern nxweb_handler test_handler;
extern nxweb_handler sendfile_handler;

extern nxweb_filter gzip_filter;
extern nxweb_filter image_filter;

// These are benchmarking handlers (see modules/benchmark.c):
NXWEB_SET_HANDLER(benchmark, "/benchmark-inprocess", &benchmark_handler, .priority=100);
NXWEB_SET_HANDLER(benchmark_inworker, "/benchmark-inworker", &benchmark_handler_inworker, .priority=100);
NXWEB_SET_HANDLER(test, "/test", &test_handler, .priority=900);

// This is sample handler (see modules/hello.c):
NXWEB_SET_HANDLER(hello, "/hello", &hello_handler, .priority=1000, .filters={&gzip_filter});

// This proxies requests to backend number 0 (see proxy setup below):
NXWEB_SET_HANDLER(java_test, "/java-test", &nxweb_http_proxy_handler, .priority=10000, .idx=0, .uri="/java-test");

// This proxies requests to backend number 1 (I have another nxweb listening at port 8777):
NXWEB_SET_HANDLER(nxweb_8777, "/8777", &nxweb_http_proxy_handler, .priority=10000, .idx=1, .uri="");

// This serves static files from $(work_dir)/html directory (see modules/sendfile.c):
NXWEB_SET_HANDLER(sendfile, 0, &sendfile_handler, .priority=900000,
        .filters={&image_filter, &gzip_filter}, .dir="html", .gzip_dir="cache/gzip", .img_dir="cache/img", .cache=1);


// Server main():

static void server_main() {

  // Add listening interfaces:
  if (nxweb_listen(NXWEB_LISTEN_HOST_AND_PORT, 4096, 0, 0, 0, 0)) return; // simulate normal exit so nxweb is not respawned
#ifdef WITH_SSL
  if (nxweb_listen(NXWEB_LISTEN_HOST_AND_PORT_SSL, 1024, 1, SSL_CERT_FILE, SSL_KEY_FILE, SSL_DH_PARAMS_FILE)) return; // simulate normal exit so nxweb is not respawned
#endif // WITH_SSL

  // Setup proxies:
  nxweb_setup_http_proxy_pool(0, "localhost:8080");
  nxweb_setup_http_proxy_pool(1, "localhost:8777");

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
  while ((c=getopt(argc, argv, ":hvdsw:l:p:"))!=-1) {
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
