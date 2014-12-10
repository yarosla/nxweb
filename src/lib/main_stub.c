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

#include "nxweb.h"

#include <dlfcn.h>


// Utility stuff:

static void show_help(void) {
  printf( "usage:    nxweb <options>\n\n"
          " -d       run as daemon\n"
          " -s       shutdown nxweb daemon\n"
          " -w dir   set work dir        (default: ./)\n"
          " -l file  set error log file  (default: stderr or nxweb_error_log for daemon)\n"
          " -L level set error log level to INFO/WARN/ERROR/NONE (default: WARN)\n"
          " -a file  set access log file (default: none)\n"
          " -p file  set pid file        (default: none or nxweb.pid for daemon)\n"
          " -u user  set process uid\n"
          " -g group set process gid\n"
          " -H [ip]:port http listens to     (default: :8055)\n"
#ifdef WITH_SSL
          " -S [ip]:port https listens to    (default: :8056)\n"
#endif
          " -c file  load configuration file (default: nxweb_config.json)\n"
          " -T targ  set configuration target\n"
          " -P dir   set python root dir\n"
          " -W name  set python WSGI app fully qualified name\n"
          " -V path  set python virtualenv path\n"
          " -M path  load nxweb module from .so file (repeat to load several libs)\n"
          " -h       show this help\n"
          " -v       show version\n"
          "\n"
          "example:  nxweb -d -l nxweb_error_log -H :80\n\n"
         );
}

static void show_version(void) {
  printf( "NXWEB - ultra-fast and super-lightweight web server\n"
          "project page:        http://nxweb.org/\n"
          "version:             nxweb/" REVISION "\n"
          "build-date:          " __DATE__ " " __TIME__ "\n"
#ifdef WITH_ZLIB
          "gzip support:        ON\n"
#endif
#ifdef WITH_SSL
          "SSL support:         ON\n"
#endif
#ifdef WITH_IMAGEMAGICK
          "ImageMagick support: ON\n"
#endif
#ifdef WITH_PYTHON
          "Python support:      ON\n"
#endif
#ifdef NXWEB_SYSCONFDIR
          "default config dir:  " NXWEB_SYSCONFDIR "\n"
#endif
#ifdef NXWEB_LIBDIR
          "lib dir:             " NXWEB_LIBDIR "\n"
#endif
         );
}

// Command-line defaults
nxweb_main_args_t nxweb_main_args={
};

int nxweb_main_stub(int argc, char** argv, void (*server_main)()) {
  int daemon=0;
  int shutdown=0;
  const char* work_dir=0;
  const char* error_log_file=0;
  const char* access_log_file=0;
  const char* pid_file=0;

  int c;
  while ((c=getopt(argc, argv, ":hvdsw:l:a:p:u:g:H:S:c:T:P:W:V:L:M:"))!=-1) {
    switch (c) {
      case 'h':
        show_help();
        return 0;
      case 'v':
        show_version();
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
        error_log_file=optarg;
        break;
      case 'L':
        nxweb_main_args.error_log_level_set=1;
        if (!strcmp(optarg, "INFO")) nxweb_error_log_level=NXWEB_LOG_INFO;
        else if (!strcmp(optarg, "WARN")) nxweb_error_log_level=NXWEB_LOG_WARNING;
        else if (!strcmp(optarg, "ERROR")) nxweb_error_log_level=NXWEB_LOG_ERROR;
        else if (!strcmp(optarg, "NONE")) nxweb_error_log_level=NXWEB_LOG_NONE;
        else nxweb_main_args.error_log_level_set=0;
        break;
      case 'a':
        access_log_file=optarg;
        break;
      case 'p':
        pid_file=optarg;
        break;
      case 'u':
        nxweb_main_args.user_name=optarg;
        nxweb_main_args.user_uid=nxweb_get_uid_by_name(optarg);
        if (nxweb_main_args.user_uid==-1) {
          fprintf(stderr, "unknown user %s\n\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      case 'g':
        nxweb_main_args.group_name=optarg;
        nxweb_main_args.group_gid=nxweb_get_gid_by_name(optarg);
        if (nxweb_main_args.group_gid==-1) {
          fprintf(stderr, "unknown group %s\n\n", optarg);
          return EXIT_FAILURE;
        }
        break;
      case 'H':
        nxweb_main_args.http_listening_host_and_port=optarg;
        break;
      case 'S':
        nxweb_main_args.https_listening_host_and_port=optarg;
        break;
      case 'c':
        nxweb_main_args.config_file=optarg;
        break;
      case 'T':
        {
          char *tok, *p=optarg;
          int i;
          for (i=0, tok=strsep(&p, ",;/ "); tok && i<15; i++, tok=strsep(&p, ",;/ ")) {
            if (*tok) // skip empty tokens
              nxweb_main_args.config_targets[i]=tok;
          }
        }
        break;
      case 'P':
        nxweb_main_args.python_root=optarg;
        break;
      case 'W':
        nxweb_main_args.python_wsgi_app=optarg;
        break;
      case 'V':
        nxweb_main_args.python_virtualenv_path=optarg;
        break;
      case 'M':
        if (!dlopen(optarg, RTLD_NOW)) {
          fprintf(stderr, "failed to load shared library %s: %s\n\n", optarg, dlerror());
          return EXIT_FAILURE;
        }
        break;
      case '?':
      default:
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
    if (!pid_file) pid_file="nxweb.pid";
    nxweb_shutdown_daemon(work_dir, pid_file);
    return EXIT_SUCCESS;
  }

  nxweb_server_config.access_log_fpath=access_log_file;

  if (daemon) {
    if (!error_log_file) error_log_file="nxweb_error_log";
    nxweb_server_config.error_log_fpath=error_log_file;
    nxweb_run_daemon(work_dir, error_log_file, pid_file, server_main, nxweb_main_args.group_gid, nxweb_main_args.user_uid);
  }
  else {
    nxweb_server_config.error_log_fpath=error_log_file;
    nxweb_run_normal(work_dir, error_log_file, pid_file, server_main, nxweb_main_args.group_gid, nxweb_main_args.user_uid);
  }
  return EXIT_SUCCESS;
}
