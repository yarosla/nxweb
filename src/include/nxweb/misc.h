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

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"

void nxweb_open_log_file(char const* log_file, gid_t i, uid_t i1);
void nxweb_continue_as_daemon(char const* work_dir, char const* log_file, gid_t i, uid_t i1);
void nxweb_create_pid_file(char const* pid_file, __pid_t pid, gid_t i, uid_t i1);
int nxweb_relauncher(void (*main_func)(), const char* pid_file);
int nxweb_shutdown_daemon(const char* work_dir, const char* pid_file);
int nxweb_run_daemon(char const* work_dir, char const* log_file, char const* pid_file, void (* main_func)(), gid_t i, uid_t i1);
int nxweb_run_normal(char const* work_dir, char const* log_file, char const* pid_file, void (* main_func)(), gid_t i, uid_t i1);
int nxweb_drop_privileges(gid_t gid, uid_t uid);
uid_t nxweb_get_uid_by_name(const char* user_name);
gid_t nxweb_get_gid_by_name(const char* group_name);

typedef struct nxweb_main_args_t {
  const char* group_name;
  const char* user_name;
  const char* http_listening_host_and_port;
  const char* https_listening_host_and_port;
  const char* config_file;
  const char* config_targets[16]; // up to 15 targets in command line
  const char* python_root;
  const char* python_wsgi_app;
  const char* python_virtualenv_path;
  gid_t group_gid;
  uid_t user_uid;
  _Bool error_log_level_set:1;
} nxweb_main_args_t;

extern nxweb_main_args_t nxweb_main_args;

int nxweb_main_stub(int argc, char** argv, void (*server_main)());

void nxweb_die(const char* fmt, ...) __attribute__((format (printf, 1, 2)));
void nxweb_log_error(const char* fmt, ...) __attribute__((format (printf, 1, 2)));
void nxweb_log_warning(const char* fmt, ...) __attribute__((format (printf, 1, 2)));
void nxweb_log_info(const char* fmt, ...) __attribute__((format (printf, 1, 2)));

#ifdef ENABLE_LOG_DEBUG

void nxweb_log_debug(const char* fmt, ...) __attribute__((format (printf, 1, 2)));

#define nxweb_activate_log_debug(cond, msg) ({ \
    if (nxweb_error_log_level!=NXWEB_LOG_DEBUG && (cond)) { \
      nxweb_error_log_level=NXWEB_LOG_DEBUG; \
      nxweb_log_error("log_debug activated: " msg); \
    } \
})

#else

#define nxweb_log_debug(...)
#define nxweb_activate_log_debug(cond, msg)

#endif

#define NXWEB_LOG_NONE 0
#define NXWEB_LOG_ERROR 1
#define NXWEB_LOG_WARNING 2
#define NXWEB_LOG_INFO 3
#define NXWEB_LOG_DEBUG 4

extern int nxweb_error_log_level;

int _nxweb_set_non_block(int fd);
int _nxweb_setup_listening_socket(int fd);
int _nxweb_setup_client_socket(int fd);
void _nxweb_batch_write_begin(int fd);
void _nxweb_batch_write_end(int fd);
void _nxweb_close_good_socket(int fd);
void _nxweb_close_bad_socket(int fd);
int _nxweb_bind_socket(const char *host_and_port, int backlog);
struct addrinfo* _nxweb_resolve_host(const char *host_and_port, int passive); // passive for bind(); active for connect()
void _nxweb_free_addrinfo(struct addrinfo* ai);
void _nxweb_sleep_us(int us);

char* nxweb_trunc_space(char* str);

int nxweb_mkpath(char* file_path, mode_t mode); // ensure all dirs exist to the file path specified

static inline char* uint_to_decimal_string(unsigned long n, char* buf, int buf_size) {
  char* p=buf+buf_size;
  *--p='\0';
  if (!n) {
    *--p='0';
    return p;
  }
  while (n) {
    *--p=n%10+'0';
    n=n/10;
  }
  return p;
}

static inline char* uint_to_decimal_string_zeropad(unsigned long n, char* buf, int num_digits, int null_terminate) {
  char* p=buf+num_digits;
  if (null_terminate) *p='\0';
  while (num_digits--) {
    *--p=n%10+'0';
    n=n/10;
  }
  return buf;
}

static inline char HEX_DIGIT(char n) { n&=0xf; return n<10? n+'0' : n-10+'A'; }

static inline char* uint_to_hex_string_zeropad(unsigned long n, char* buf, int num_digits, int null_terminate) {
  char* p=buf+num_digits;
  if (null_terminate) *p='\0';
  while (num_digits--) {
    *--p=HEX_DIGIT(n);
    n>>=4;
  }
  return buf;
}

static inline char* uint64_to_hex_string_zeropad(uint64_t n, char* buf, int num_digits, int null_terminate) {
  char* p=buf+num_digits;
  if (null_terminate) *p='\0';
  while (num_digits--) {
    *--p=HEX_DIGIT(n);
    n>>=4;
  }
  return buf;
}

#ifdef	__cplusplus
}
#endif

#endif // MISC_H_INCLUDED
