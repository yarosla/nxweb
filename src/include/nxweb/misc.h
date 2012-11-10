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

#include <unistd.h>
#include <sys/stat.h>

void nxweb_open_log_file(const char* log_file);
void nxweb_continue_as_daemon(const char* work_dir, const char* log_file);
void nxweb_create_pid_file(const char* pid_file, pid_t pid);
int nxweb_relauncher(void (*main_func)(), const char* pid_file);
int nxweb_shutdown_daemon(const char* work_dir, const char* pid_file);
int nxweb_run_daemon(const char* work_dir, const char* log_file, const char* pid_file, void (*main_func)());
int nxweb_run_normal(const char* work_dir, const char* log_file, const char* pid_file, void (*main_func)());
int nxweb_drop_privileges(const char* group_name, const char* user_name);

void nxweb_die(const char* fmt, ...) __attribute__((format (printf, 1, 2)));
void nxweb_log_error(const char* fmt, ...) __attribute__((format (printf, 1, 2)));

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

#endif // MISC_H_INCLUDED
