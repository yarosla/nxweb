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

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <sys/stat.h>

void nxweb_open_log_file(const char* log_file);
void nxweb_continue_as_daemon(const char* work_dir, const char* log_file);
void nxweb_create_pid_file(const char* pid_file);
int nxweb_relauncher(void (*main_func)(), const char* pid_file);
int nxweb_shutdown_daemon(const char* work_dir, const char* pid_file);
int nxweb_run_daemon(const char* work_dir, const char* log_file, const char* pid_file, void (*main_func)());
int nxweb_run_normal(const char* work_dir, const char* log_file, const char* pid_file, void (*main_func)());

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

#endif // MISC_H_INCLUDED
