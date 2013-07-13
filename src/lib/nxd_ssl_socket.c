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

#ifdef WITH_SSL

#include <stdio.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <gnutls/gnutls.h>

static gnutls_dh_params_t dh_params_generated=0;

int nxd_ssl_socket_global_init(void) __attribute__ ((constructor));
void nxd_ssl_socket_global_finalize(void) __attribute__ ((destructor));

#define GNUTLS_DEBUG_LEVEL 0

#if GNUTLS_DEBUG_LEVEL

static void _gnutls_log_func(int level, const char* msg) {
  char cur_time[32];
  time_t t;
  struct tm tm;
  time(&t);
  localtime_r(&t, &tm);
  strftime(cur_time, sizeof(cur_time), "%F %T", &tm); // %F=%Y-%m-%d %T=%H:%M:%S
  fprintf(stderr, "%s [GNUTLS:%d]: %s", cur_time, level, msg);
  fflush(stderr);
}

#endif // GNUTLS_DEBUG_LEVEL

int nxd_ssl_socket_global_init(void) {
  // this must be called once in the program
  //gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
  gnutls_global_init();

#if GNUTLS_DEBUG_LEVEL
  gnutls_global_set_log_level(GNUTLS_DEBUG_LEVEL);
  gnutls_global_set_log_function(_gnutls_log_func);
#endif // GNUTLS_DEBUG_LEVEL

  //printf("gnutls global initialization complete\n");
  return 0;
}

void nxd_ssl_socket_global_finalize(void) {
  if (dh_params_generated) gnutls_dh_params_deinit(dh_params_generated);
  gnutls_global_deinit();
  //gcry_control(GCRYCTL_FINALIZE);
  //printf("gnutls deinitialized\n");
}

int nxd_ssl_socket_init_server_parameters(gnutls_certificate_credentials_t* x509_cred,
        gnutls_dh_params_t* dh_params, gnutls_priority_t* priority_cache, gnutls_datum_t* session_ticket_key,
        const char* cert_file, const char* key_file, const char* dh_params_file,
        const char* cipher_priority_string) {
  int ret;

  gnutls_certificate_allocate_credentials(x509_cred);
/*
  ret=gnutls_certificate_set_x509_trust_file(*x509_cred, CAFILE, GNUTLS_X509_FMT_PEM);
  if (ret<0) {
    nxweb_log_error("gnutls_certificate_set_x509_trust_file() failed %d", ret);
    return -1;
  }
  ret=gnutls_certificate_set_x509_crl_file(*x509_cred, CRLFILE, GNUTLS_X509_FMT_PEM);
  if (ret<0) {
    nxweb_log_error("gnutls_certificate_set_x509_crl_file() failed %d", ret);
    return -1;
  }
*/
  ret=gnutls_certificate_set_x509_key_file(*x509_cred, cert_file, key_file, GNUTLS_X509_FMT_PEM);
  if (ret<0) {
    nxweb_log_error("No certificate or key were found");
    return -1;
  }

  ret=gnutls_dh_params_init(dh_params);
  if (ret<0) {
    nxweb_log_error("gnutls_dh_params_init() failed %d", ret);
    return -1;
  }

  if (dh_params_file) {
    int fd=open(dh_params_file, O_RDONLY);
    if (fd<0) {
      nxweb_log_error("can't open dh_params_file %s", dh_params_file);
      return -1;
    }
    gnutls_datum_t d;
    d.size=lseek(fd, 0, SEEK_END);
    if (d.size==-1) {
      nxweb_log_error("can't read dh_params_file %s", dh_params_file);
      return -1;
    }
    lseek(fd, 0, SEEK_SET);
    void* buf=nx_alloc(d.size);
    d.size=read(fd, buf, d.size);
    d.data=buf;
    ret=gnutls_dh_params_import_pkcs3(*dh_params, &d, GNUTLS_X509_FMT_PEM);
    if (ret<0) {
      nxweb_log_error("gnutls_dh_params_import_pkcs3() failed %d", ret);
      return -1;
    }
    nx_free(buf);
    close(fd);
  }
  else {
    if (!dh_params_generated) {
      gnutls_dh_params_init(&dh_params_generated);
      /* Generate Diffie-Hellman parameters - for use with DHE
       * kx algorithms. When short bit length is used, it might
       * be wise to regenerate parameters often.
       */
      int bits=gnutls_sec_param_to_pk_bits(GNUTLS_PK_DH, GNUTLS_SEC_PARAM_NORMAL);
      ret=gnutls_dh_params_generate2(dh_params_generated, bits);
      if (ret<0) {
        nxweb_log_error("gnutls_dh_params_generate2() failed %d", ret);
        return -1;
      }
    }
    gnutls_dh_params_cpy(*dh_params, dh_params_generated);
  }

  if (!cipher_priority_string) cipher_priority_string="NORMAL:%COMPAT";
  ret=gnutls_priority_init(priority_cache, cipher_priority_string, 0); // "PERFORMANCE:%SERVER_PRECEDENCE" or "NORMAL:%COMPAT"
  if (ret<0) {
    nxweb_log_error("gnutls_priority_init() failed %d (check priority string syntax)", ret);
    return -1;
  }
  gnutls_certificate_set_dh_params(*x509_cred, *dh_params);

  gnutls_session_ticket_key_generate(session_ticket_key);

  return 0;
}

void nxd_ssl_socket_finalize_server_parameters(gnutls_certificate_credentials_t x509_cred, gnutls_dh_params_t dh_params, gnutls_priority_t priority_cache, gnutls_datum_t* session_ticket_key) {
  gnutls_certificate_free_credentials(x509_cred);
  gnutls_dh_params_deinit(dh_params);
  gnutls_priority_deinit(priority_cache);
  gnutls_free(session_ticket_key->data);
  session_ticket_key->data=0;
}

#if (__SIZEOF_POINTER__==8)
typedef uint64_t int_to_ptr;
#else
typedef uint32_t int_to_ptr;
#endif

/*
static const char *bin2hex(const void *bin, size_t bin_size) { // not optimized! use for debug only
  static __thread char printable[110];
  const unsigned char *_bin=bin;
  char *print;
  size_t i;

  if (bin_size>50) bin_size=50;

  print=printable;
  for (i=0; i<bin_size; i++) {
    sprintf(print, "%.2x ", _bin[i]);
    print+=2;
  }

  return printable;
}
*/

static int do_handshake(nxd_ssl_socket* ss) {

  nxweb_log_debug("ssl do_handshake");

  nxe_loop* loop=ss->fs.data_is.super.loop;
  assert(!ss->handshake_complete && !ss->handshake_failed);
  if (!ss->handshake_started) {
    gnutls_transport_set_ptr(ss->session, (gnutls_transport_ptr_t)(int_to_ptr)ss->fs.fd);
    ss->handshake_started=1;

    ss->saved_is=ss->fs.data_os.pair;
    if (ss->saved_is) nxe_disconnect_streams(ss->fs.data_os.pair, &ss->fs.data_os);
    nxe_connect_streams(loop, &ss->handshake_stub_is, &ss->fs.data_os);
    nxe_istream_set_ready(loop, &ss->handshake_stub_is);

    ss->saved_os=ss->fs.data_is.pair;
    if (ss->saved_os) nxe_disconnect_streams(&ss->fs.data_is, ss->fs.data_is.pair);
    nxe_connect_streams(loop, &ss->fs.data_is, &ss->handshake_stub_os);
    nxe_ostream_set_ready(loop, &ss->handshake_stub_os);
  }

  int ret=gnutls_handshake(ss->session);
  if (ret==GNUTLS_E_SUCCESS) {
    ss->handshake_complete=1;

    nxe_istream_unset_ready(&ss->handshake_stub_is);
    nxe_disconnect_streams(&ss->handshake_stub_is, &ss->fs.data_os);
    if (ss->saved_is) nxe_connect_streams(loop, ss->saved_is, &ss->fs.data_os);

    nxe_ostream_unset_ready(&ss->handshake_stub_os);
    nxe_disconnect_streams(&ss->fs.data_is, &ss->handshake_stub_os);
    if (ss->saved_os) nxe_connect_streams(loop, &ss->fs.data_is, ss->saved_os);

/*
    char sid[128];
    size_t sid_len=sizeof(sid);
    gnutls_session_get_id(ss->session, sid, &sid_len);
    nxweb_log_error("gnutls successful handshake resumed=%d id=%s", gnutls_session_is_resumed(ss->session), bin2hex(sid, sid_len));
*/
    return 0;
  }
  else if (ret!=GNUTLS_E_AGAIN) {
    if (ret==GNUTLS_E_PREMATURE_TERMINATION || ret==GNUTLS_E_UNEXPECTED_PACKET_LENGTH) {
      nxe_publish(&ss->fs.data_error, (nxe_data)NXE_RDCLOSED);
    }
    else if (ret==GNUTLS_E_UNEXPECTED_PACKET) { // http connection attempted on SSL port?
      nxe_publish(&ss->fs.data_error, (nxe_data)NXE_PROTO_ERROR);
      nxweb_log_error("gnutls handshake error %d sock=%p (http connection attempted on SSL port?)", ret, ss);
    }
    else {
      nxe_publish(&ss->fs.data_error, (nxe_data)NXE_ERROR);
      nxweb_log_error("gnutls handshake error %d sock=%p", ret, ss);
    }
    ss->handshake_failed=1;
    return -1;
  }
  return 1;
}

static void handshake_stub_is_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_ssl_socket* ss=(nxd_ssl_socket*)((char*)is-offsetof(nxd_ssl_socket, handshake_stub_is));

  nxweb_log_debug("ssl handshake_stub_is_do_write");

  // continue handshake
  if (ss->handshake_failed || do_handshake(ss)) {
    nxe_ostream_unset_ready(os);
  }
}

static void handshake_stub_os_do_read(nxe_ostream* os, nxe_istream* is) {
  nxd_ssl_socket* ss=(nxd_ssl_socket*)((char*)os-offsetof(nxd_ssl_socket, handshake_stub_os));

  nxweb_log_debug("ssl handshake_stub_os_do_read");

  // continue handshake
  if (ss->handshake_failed || do_handshake(ss)) {
    nxe_istream_unset_ready(is);
  }
}

static nxe_size_t sock_data_recv_read(nxe_istream* is, nxe_ostream* os, void* ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxe_fd_source* fs=(nxe_fd_source*)((char*)is-offsetof(nxe_fd_source, data_is));
  nxd_ssl_socket* ss=(nxd_ssl_socket*)((char*)is-offsetof(nxe_fd_source, data_is)-offsetof(nxd_ssl_socket, fs));

  nxweb_log_debug("ssl sock_data_recv_read");

  if (!ss->handshake_complete) {
    if (do_handshake(ss)) {
      nxe_istream_unset_ready(is);
      return 0;
    }
  }

  if (size>0) {
    nxe_ssize_t bytes_received=gnutls_record_recv(ss->session, ptr, size);
    if (bytes_received<0) {
      nxe_istream_unset_ready(is);
      if (bytes_received!=GNUTLS_E_AGAIN) nxe_publish(&fs->data_error, (nxe_data)NXE_ERROR);
      return 0;
    }
    if (bytes_received==0) {
      nxe_istream_unset_ready(is);
      nxe_publish(&fs->data_error, (nxe_data)NXE_RDCLOSED);
      return 0;
    }
/*
    if (bytes_received<size) {
      nxe_istream_unset_ready(is);
      if (bytes_received==0) {
        nxe_publish(&fs->data_error, (nxe_data)NXE_RDCLOSED);
        return 0;
      }
    }
*/
    return bytes_received;
  }
  return 0;
}

static nxe_ssize_t sock_data_send_write(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* _flags) {
  nxe_fd_source* fs=(nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os));
  nxd_ssl_socket* ss=(nxd_ssl_socket*)((char*)os-offsetof(nxe_fd_source, data_os)-offsetof(nxd_ssl_socket, fs));

  nxweb_log_debug("ssl sock_data_send_write");

  if (!ss->handshake_complete) {
    if (do_handshake(ss)) {
      nxe_ostream_unset_ready(os);
      return 0;
    }
  }

  nxe_flags_t flags=*_flags;
  nx_file_reader_to_mem_ptr(fd, fr, &ptr, &size, &flags);
  if (size) {
    nxe_loop* loop=os->super.loop;
    if (!loop->batch_write_fd) {
      int fd=fs->fd;
      _nxweb_batch_write_begin(fd);
      loop->batch_write_fd=fd;
    }
    /*
    nxe_ssize_t buffered_bytes_sent=0;
    if (ss->buffered_size) {
      buffered_bytes_sent=gnutls_record_send(ss->session, 0, 0); // flush buffered data
      if (buffered_bytes_sent != ss->buffered_size) {
        if (buffered_bytes_sent<0) {
          nxe_ostream_unset_ready(os);
          if (buffered_bytes_sent==GNUTLS_E_AGAIN) {
            nxweb_log_warning("gnutls_record_send() returned GNUTLS_E_AGAIN again");
            return 0;
          }
          nxweb_log_warning("gnutls_record_send() can't flush buffered data err=%d", (int)buffered_bytes_sent);
          nxe_publish(&fs->data_error, (nxe_data)NXE_ERROR);
          return 0;
        }
        ss->buffered_size-=buffered_bytes_sent;
        nxweb_log_info("gnutls_record_send() flushed %d bytes, %d bytes left in buffer", (int)buffered_bytes_sent, (int)ss->buffered_size);
        assert(ss->buffered_size>0);
        return 0;
      }
      ss->buffered_size=0;
      buffered_bytes_sent=1; // one byte that has left (see below)
      size--;
      if (!size) return buffered_bytes_sent;
    }
    */
    nxe_ssize_t bytes_sent=gnutls_record_send(ss->session, ptr.cptr, size);
    if (bytes_sent<0) {
      nxe_ostream_unset_ready(os);
      if (bytes_sent==GNUTLS_E_AGAIN) {
        nxweb_log_info("gnutls_record_send() returned GNUTLS_E_AGAIN; %ld bytes offered, some buffered", size);
        // GNUTLS buffers data provided to gnutls_record_send() so we can't change it anymore.
        // Effectively is could be considered as "sent" but we still need to call
        // gnutls_record_send() again to flush it out.
        //ss->buffered_size=size;
        //return buffered_bytes_sent+ss->buffered_size-1; // pretend all sent except last byte
        return 0;
      }
      nxe_publish(&fs->data_error, (nxe_data)NXE_ERROR);
      nxweb_log_warning("gnutls_record_send() returned error %d", (int)bytes_sent);
      //return buffered_bytes_sent; // +0
      return 0;
    }
    //return buffered_bytes_sent+bytes_sent;
    return bytes_sent;
  }
  return 0;
}

static void sock_data_send_shutdown(nxe_ostream* os) {
  //nxe_fd_source* fs=(nxe_fd_source*)((char*)os-offsetof(nxe_fd_source, data_os));
  nxd_ssl_socket* ss=(nxd_ssl_socket*)((char*)os-offsetof(nxe_fd_source, data_os)-offsetof(nxd_ssl_socket, fs));
  gnutls_bye(ss->session, GNUTLS_SHUT_WR);
}

static const nxe_istream_class sock_data_recv_class={.read=sock_data_recv_read};
static const nxe_ostream_class sock_data_send_class={.write=sock_data_send_write,
        .shutdown=sock_data_send_shutdown};

static const nxe_istream_class handshake_stub_is_class={.do_write=handshake_stub_is_do_write};
static const nxe_ostream_class handshake_stub_os_class={.do_read=handshake_stub_os_do_read};

/*
static int handshake_post_hello(gnutls_session_t session) {
  char buf[256];
  size_t buf_len=sizeof(buf);
  unsigned type;
  int ret=gnutls_server_name_get(session, buf, &buf_len, &type, 0);
  if (ret<0 || type!=GNUTLS_NAME_DNS) buf[0]='\0';
  nxweb_log_error("handshaking with [%s]", buf);
  return 0;
}
*/

/*
static int db_store_func(void* p, gnutls_datum_t key, gnutls_datum_t data) {
  nxweb_log_error("db_store_func(%p, %d, %d)", p, key.size, data.size);
  return 0;
}

static gnutls_datum_t db_retr_func(void* p, gnutls_datum_t key) {
  nxweb_log_error("db_retr_func(%p, %d)", p, key.size);
  gnutls_datum_t d={0, 0};
  return d;
}

static int db_remove_func(void* p, gnutls_datum_t key) {
  //nxweb_log_error("db_remove_func(%p, %d)", p, key.size);
  return 0;
}
*/

static void socket_shutdown(nxd_socket* sock) {
  nxd_ssl_socket* ss=(nxd_ssl_socket*)sock;
  gnutls_bye(ss->session, GNUTLS_SHUT_WR);
}

static void socket_finalize(nxd_socket* sock, int good) {
  nxd_ssl_socket* ss=(nxd_ssl_socket*)sock;
  nxd_ssl_server_socket_finalize(ss, good);
}

static const nxd_socket_class ssl_server_socket_class={.shutdown=socket_shutdown, .finalize=socket_finalize};

void nxd_ssl_server_socket_init(nxd_ssl_socket* ss, gnutls_certificate_credentials_t x509_cred, gnutls_priority_t priority_cache, gnutls_datum_t* session_ticket_key) {
  memset(ss, 0, sizeof(nxd_socket));
  ss->cls=&ssl_server_socket_class;
  nxe_init_fd_source(&ss->fs, 0, &sock_data_recv_class, &sock_data_send_class, NXE_PUB_DEFAULT);
  nxe_init_istream(&ss->handshake_stub_is, &handshake_stub_is_class);
  nxe_init_ostream(&ss->handshake_stub_os, &handshake_stub_os_class);

  gnutls_init(&ss->session, GNUTLS_SERVER);
  gnutls_priority_set(ss->session, priority_cache);

  // We don't request any certificate from the client.
  // If we did we would need to verify it.
  gnutls_certificate_server_set_request(ss->session, GNUTLS_CERT_IGNORE);

  gnutls_session_ticket_enable_server(ss->session, session_ticket_key);

/*
  gnutls_db_set_retrieve_function(ss->session, db_retr_func);
  gnutls_db_set_store_function(ss->session, db_store_func);
  gnutls_db_set_remove_function(ss->session, db_remove_func);
  gnutls_db_set_ptr(ss->session, ss);
*/

  gnutls_credentials_set(ss->session, GNUTLS_CRD_CERTIFICATE, x509_cred);

  //gnutls_handshake_set_post_client_hello_function(ss->session, handshake_post_hello);
}

void nxd_ssl_server_socket_finalize(nxd_ssl_socket* ss, int good) {
  if (ss->fs.data_is.super.loop) nxe_unregister_fd_source(&ss->fs); // this also disconnects streams and unsubscribes subscribers
  gnutls_deinit(ss->session);
  if (good) _nxweb_close_good_socket(ss->fs.fd);
  else _nxweb_close_bad_socket(ss->fs.fd);
}

#endif // WITH_SSL
