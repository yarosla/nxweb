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

#define _FILE_OFFSET_BITS 64

#include "misc.h"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>


int nxweb_error_log_level=NXWEB_LOG_WARNING; // 0=nothing; 1=errors; 2=warnings; 3=info; 4=debug

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
  strftime(buf, (size_t)max_buf_size, "%F %T", &tm); // %F=%Y-%m-%d %T=%H:%M:%S
  return buf;
}

static void _nxweb_log_error(int level, const char* fmt, va_list ap) {
  //if (level>nxweb_error_log_level) return;
  char cur_time[32];
  get_current_time(cur_time, sizeof(cur_time));
  flockfile(stderr);
  fprintf(stderr, "%s %d [%u:%p]: ", cur_time, level, getpid(), (void*)pthread_self());
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  fflush(stderr);
  funlockfile(stderr);
}

void nxweb_log_error(const char* fmt, ...) {
  if (nxweb_error_log_level<NXWEB_LOG_ERROR) return;
  va_list ap;
  va_start(ap, fmt);
  _nxweb_log_error(1, fmt, ap);
  va_end(ap);
}

void nxweb_log_warning(const char* fmt, ...) {
  if (nxweb_error_log_level<NXWEB_LOG_WARNING) return;
  va_list ap;
  va_start(ap, fmt);
  _nxweb_log_error(2, fmt, ap);
  va_end(ap);
}

void nxweb_log_info(const char* fmt, ...) {
  if (nxweb_error_log_level<NXWEB_LOG_INFO) return;
  va_list ap;
  va_start(ap, fmt);
  _nxweb_log_error(3, fmt, ap);
  va_end(ap);
}

#ifdef ENABLE_LOG_DEBUG

void nxweb_log_debug(const char* fmt, ...) {
  static int count=1000; // max. number of messages to log; can't run infinitely - might fill up disk
  if (nxweb_error_log_level<NXWEB_LOG_DEBUG) return;
  va_list ap;
  va_start(ap, fmt);
  _nxweb_log_error(4, fmt, ap);
  va_end(ap);
  if (!--count) exit(1); // abnormal termination
}

#endif

int _nxweb_set_non_block(int fd) {
  int flags=fcntl(fd, F_GETFL);
  if (flags<0) return flags;
  if (fcntl(fd, F_SETFL, flags|O_NONBLOCK)<0) return -1;
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
  //nxweb_log_error("%d write_begin at %ld", fd, nxe_get_time_usec());
}

void _nxweb_batch_write_end(int fd) {
  int cork=0; // flush unsent packets
  setsockopt(fd, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork));
  //nxweb_log_error("%d write_end   at %ld", fd, nxe_get_time_usec());
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
  setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, (socklen_t)sizeof(linger));
  close(fd);
}

struct addrinfo* _nxweb_resolve_host(const char *host_and_port, int passive) {
  char* host=strdup(host_and_port);
  char* port=strchr(host, ':');
  if (port) *port++='\0';
  else port="80";

  struct addrinfo hints, *res, *res_first, *res_last;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family=PF_UNSPEC;
  hints.ai_socktype=SOCK_STREAM;
  hints.ai_flags=passive? AI_PASSIVE : 0;

  if (getaddrinfo((!*host || *host=='*')?0:host, port, &hints, &res_first)) goto ERR1;

  // search for an ipv4 address, no ipv6 yet
  res_last=0;
  for (res=res_first; res; res=res->ai_next) {
    if (res->ai_family==AF_INET) break;
    res_last=res;
  }

  if (!res) goto ERR2;
  if (res!=res_first) {
    // unlink from list and free rest
    res_last->ai_next = res->ai_next;
    freeaddrinfo(res_first);
    res->ai_next=0;
  }

  free(host);
  return res;

ERR2:
  freeaddrinfo(res_first);
ERR1:
  free(host);
  return 0;
}

void _nxweb_free_addrinfo(struct addrinfo* ai) {
  freeaddrinfo(ai);
}

int _nxweb_bind_socket(const char *host_and_port, int backlog) {
  struct addrinfo* ai=_nxweb_resolve_host(host_and_port, 1);
  if (!ai) {
    nxweb_log_error("can't resolve IP/port %d", errno);
    return -1;
  }
  int listen_fd;
  int reuseaddr_on=1;
  listen_fd=socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd<0) {
    nxweb_log_error("socket() failed %d", errno);
    return -1;
  }
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_on, sizeof(reuseaddr_on))==-1) {
    nxweb_log_error("setsockopt() failed %d", errno);
    return -1;
  }
  if (bind(listen_fd, ai->ai_addr, ai->ai_addrlen)<0) {
    nxweb_log_error("bind failed");
    return -1;
  }
  freeaddrinfo(ai);
  if (listen(listen_fd, backlog)<0) {
    nxweb_log_error("listen() failed %d", errno);
    return -1;
  }
  if (_nxweb_set_non_block(listen_fd)<0) {
    nxweb_log_error("failed to set server socket to non-blocking %d", errno);
    return -1;
  }
  if (_nxweb_setup_listening_socket(listen_fd)<0) {
    nxweb_log_error("failed to setup listening socket");
    return -1;
  }
  return listen_fd;
}

char* nxweb_trunc_space(char* str) { // does it inplace
  if (!str || !*str) return str;

  // truncate beginning of string
  register char* p=str;
  while (*p && ((unsigned char)*p)<=((unsigned char)' ')) p++;
  //if (p!=str) memmove(str, p, strlen(p)+1);
  str=p;
  if (!*p) return str; // empty string
  //while (*++p) ;
  p+=strlen(p);

  // truncate end of string
  while (((unsigned char)*--p)<=((unsigned char)' ')) *p='\0';

  return str;
}

void _nxweb_sleep_us(int us) {
  struct timespec req;
  time_t sec=us/1000000;
  us%=1000000;
  req.tv_sec=sec;
  req.tv_nsec=us*1000L;
  while (nanosleep(&req, &req)==-1) ;
}

int nxweb_mkpath(char* file_path, mode_t mode) {
  assert(file_path && *file_path);
  char* p;
  for (p=strchr(file_path+1, '/'); p; p=strchr(p+1, '/')) {
    *p='\0';
    if (mkdir(file_path, mode)==-1) {
      if (errno!=EEXIST) { *p='/'; return -1; }
    }
    *p='/';
  }
  return 0;
}