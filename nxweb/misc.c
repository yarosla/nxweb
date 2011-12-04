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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "nxweb_internal.h"

void nxweb_die(const char* fmt, ...) {
  va_list ap;
  fprintf(stderr, "FATAL: ");
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(EXIT_FAILURE);
}

static const char* get_current_time(char* buf, int max_buf_size) {
  time_t t;
  struct tm tm;
  time(&t);
  localtime_r(&t, &tm);
  strftime(buf, max_buf_size, "%F %T", &tm); // %F=%Y-%m-%d %T=%H:%M:%S
  return buf;
}

void nxweb_log_error(const char* fmt, ...) {
  char cur_time[32];
  va_list ap;

  get_current_time(cur_time, sizeof(cur_time));
  flockfile(stderr);
  fprintf(stderr, "%s [%u:%p]: ", cur_time, getpid(), (void*)pthread_self());
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
  funlockfile(stderr);
}

int _nxweb_set_non_block(int fd) {
  int flags=fcntl(fd, F_GETFL);
  if (flags<0) return flags;
  if (fcntl(fd, F_SETFL, flags|=O_NONBLOCK)<0) return -1;
  return 0;
}

int _nxweb_setup_listening_socket(int fd) {

//  int rcvbuf, sndbuf;
//  socklen_t sz;
//  sz=sizeof(int);
//  if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void*)&rcvbuf, &sz)) return -1;
//  sz=sizeof(int);
//  if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void*)&sndbuf, &sz)) return -1;
//
//  nxweb_log_error("rcvbuf=%d, sndbuf=%d", rcvbuf, sndbuf);

//  int timeout=1;
//  if (setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &timeout, sizeof(timeout))) return -1;
  return 0;
}

int _nxweb_setup_client_socket(int fd) {

//  int rcvbuf=4096, sndbuf=131072;
//  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf))) return -1;
//  if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf))) return -1;

//  nxweb_log_error("rcvbuf=%d, sndbuf=%d", rcvbuf, sndbuf);

//  struct linger linger;
//  linger.l_onoff=1;
//  linger.l_linger=10; // timeout for completing writes
//  if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger))) return -1;

  // can use TCP_NODELAY or TCP_CORK, or both
  int nodelay=1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay))) return -1;
  return 0;
}

void _nxweb_batch_write_begin(int fd) {
  int cork=1; // do not send partial frames
  setsockopt(fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));
}

void _nxweb_batch_write_end(int fd) {
  int cork=0; // flush unsent packets
  setsockopt(fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));
}

void _nxweb_close_good_socket(int fd) {
//  struct linger linger;
//  linger.l_onoff=1;
//  linger.l_linger=10; // timeout for completing writes
//  setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
//  shutdown(fd, SHUT_RDWR);
  close(fd);
}

void _nxweb_close_bad_socket(int fd) {
  struct linger linger;
  linger.l_onoff=1;
  linger.l_linger=0; // timeout for completing writes
  setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
  close(fd);
}

int _nxweb_bind_socket(int port) {
  int listen_fd;
  struct sockaddr_in listen_addr;
  int reuseaddr_on=1;
  listen_fd=socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd<0) {
    nxweb_log_error("listen failed");
    return -1;
  }
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on))==-1) {
    nxweb_log_error("setsockopt failed");
    return -1;
  }
  memset(&listen_addr, 0, sizeof(listen_addr));
  listen_addr.sin_family=AF_INET;
  listen_addr.sin_addr.s_addr=INADDR_ANY;
  listen_addr.sin_port=htons(port);
  if (bind(listen_fd, (struct sockaddr*)&listen_addr, sizeof(listen_addr))<0) {
    nxweb_log_error("bind failed");
    return -1;
  }
  if (listen(listen_fd, 8192)<0) {
    nxweb_log_error("listen failed");
    return -1;
  }
  if (_nxweb_set_non_block(listen_fd)<0) {
    nxweb_log_error("failed to set server socket to non-blocking");
    return -1;
  }
  if (_nxweb_setup_listening_socket(listen_fd)<0) {
    nxweb_log_error("failed to setup listening socket");
    return -1;
  }
  return listen_fd;
}
