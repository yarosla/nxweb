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

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/sendfile.h>

static nxe_size_t sock_data_recv_read(nxe_istream* is, nxe_ostream* os, void* ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxe_fd_source* fs=(nxe_fd_source*)((char*)is-offsetof(nxe_fd_source, data_is));
  //nxd_server_socket* ss=(nxd_server_socket*)((char*)is-offsetof(nxe_fd_source, data_is)-offsetof(nxd_server_socket, fs));

  nxweb_log_debug("sock_data_recv_read");

  if (size>0) {
    nxe_ssize_t bytes_received=read(fs->fd, ptr, size);
    if (bytes_received<0) {
      nxe_istream_unset_ready(is);
      if (errno!=EAGAIN) nxe_publish(&fs->data_error, (nxe_data)NXE_ERROR);
      return 0;
    }
    if (bytes_received<size) {
      nxe_istream_unset_ready(is);
      if (bytes_received==0) {
        nxe_publish(&fs->data_error, (nxe_data)NXE_RDCLOSED);
        return 0;
      }
    }
    return bytes_received;
  }
  return 0;
}

static nxe_ssize_t sock_data_send_write(nxe_ostream* os, nxe_istream* is, int sfd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxe_fd_source* fs=(nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os));

  nxweb_log_debug("sock_data_send_write");

  if (size>0) {
    nxe_loop* loop=os->super.loop;
    int fd=fs->fd;
    if (!loop->batch_write_fd) {
      _nxweb_batch_write_begin(fd);
      loop->batch_write_fd=fd;
    }
    nxe_ssize_t bytes_sent=sfd? sendfile(fd, sfd, &ptr.offs, size) : write(fd, ptr.cptr, size);
    if (bytes_sent<0) {
      nxe_ostream_unset_ready(os);
      if (errno!=EAGAIN) nxe_publish(&fs->data_error, (nxe_data)NXE_ERROR);
      return 0;
    }
    if (bytes_sent<size) {
      nxe_ostream_unset_ready(os);
      if (bytes_sent==0 && !sfd) {
        nxe_publish(&fs->data_error, (nxe_data)NXE_WRITTEN_NONE);
        return 0;
      }
    }
    return bytes_sent;
  }
  return 0;
}

static void sock_data_send_shutdown(nxe_ostream* os) {
  nxe_fd_source* fs=(nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os));
  shutdown(fs->fd, SHUT_WR);
}

static const nxe_istream_class sock_data_recv_class={.read=sock_data_recv_read};
static const nxe_ostream_class sock_data_send_class={.write=sock_data_send_write,
        .shutdown=sock_data_send_shutdown};

static void socket_shutdown(nxd_socket* sock) {
  //nxweb_log_error("socket_shutdown %p", sock);
  shutdown(sock->fs.fd, SHUT_WR);
}

static const nxd_socket_class socket_class={.shutdown=socket_shutdown, .finalize=nxd_socket_finalize};

void nxd_socket_init(nxd_socket* ss) {
  memset(ss, 0, sizeof(nxd_socket));
  ss->cls=&socket_class;
  nxe_init_fd_source(&ss->fs, 0, &sock_data_recv_class, &sock_data_send_class, NXE_PUB_DEFAULT);
/*
  ss->fs.data_is.super.cls.is_cls=&sock_data_recv_class;
  ss->fs.data_os.super.cls.os_cls=&sock_data_send_class;
  ss->fs.data_error.super.cls.pub_cls=NXE_PUB_DEFAULT;
*/
}

void nxd_socket_finalize(nxd_socket* ss, int good) {
  if (ss->fs.data_is.super.loop) nxe_unregister_fd_source(&ss->fs); // this also disconnects streams and unsubscribes subscribers
  nx_file_reader_finalize(&ss->fr);
  //nxweb_log_error("nxd_socket_finalize %p %d", ss, good);
  if (good) _nxweb_close_good_socket(ss->fs.fd);
  else _nxweb_close_bad_socket(ss->fs.fd);
}