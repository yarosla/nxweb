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

#include <malloc.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <utime.h>

#ifdef WITH_NETTLE
#include <nettle/sha.h>
#else
#include "deps/sha1-c/sha1.h"
#endif

#define NXFC_SIGNATURE (0x6366786e)

typedef union nxf_data {
  int i;
  long l;
  int32_t i32;
  int64_t i64;
  uint32_t u32;
  uint64_t u64;
  nxe_size_t sz;
  nxe_ssize_t ssz;
  off_t offs;
  time_t tim;
  void* ptr;
  const void* cptr;
  char* ptrc;
  const char* cptrc;
} nxf_data;

typedef struct fc_file_header {
  uint32_t signature;
  uint32_t header_size;

  uint32_t gzip_encoded:1;
  uint32_t ssi_on:1;
  int32_t status_code;

  nxf_data last_modified; // time_t
  nxf_data expires; // time_t
  nxf_data max_age; // time_t, delta seconds
  nxf_data content_offset; // off_t
  nxf_data content_length; // off_t
  nxf_data content_type; // const char*
  nxf_data content_charset; // const char*
  nxf_data cache_control; // const char*
  nxf_data extra_raw_headers; // const char*

  int64_t data_size;
  char data[]; // last field!
} fc_file_header;

#define FC_HEADER_SIZE (offsetof(fc_file_header, data))

typedef struct fc_filter_data {
  nxweb_filter_data fdata;
  _Bool added_ims:1; // If-Modified-Since header has been added by this filter
  nxe_ostream data_in;
  nxe_istream data_out;
  time_t expires_time;
  int fd; // cache file
  char* tmp_key;
  nxweb_http_response* resp;
} fc_filter_data;


#ifdef WITH_NETTLE

static void sha1path(const char* str, unsigned str_len, char* result) { // result size = 43 bytes (including zero-terminator)
  struct sha1_ctx sha;
  uint8_t res[SHA1_DIGEST_SIZE];
  sha1_init(&sha);
  sha1_update(&sha, str_len, (uint8_t*)str);
  sha1_digest(&sha, SHA1_DIGEST_SIZE, res);
  int i;
  char* p=result;
  *p++='/';
  for (i=0; i<SHA1_DIGEST_SIZE; i++) {
    uint32_t n=res[i];
    *p++=HEX_DIGIT(n>>4);
    if (i==1) *p++='/'; // split into directories
    *p++=HEX_DIGIT(n);
  }
  *p='\0';
}

#else

static void sha1path(const char* str, unsigned str_len, char* result) { // result size = 43 bytes (including zero-terminator)
  SHA1Context sha;
  SHA1Reset(&sha);
  SHA1Input(&sha, (const unsigned char*)str, str_len);
  SHA1Result(&sha);
  int i;
  char* p=result;
  *p++='/';
  for (i=0; i<5; i++) {
    uint32_t n=sha.Message_Digest[i];
    *p++=HEX_DIGIT(n>>28);
    *p++=HEX_DIGIT(n>>24);
    *p++=HEX_DIGIT(n>>20);
    if (i==0) *p++='/'; // split into directories
    *p++=HEX_DIGIT(n>>16);
    *p++=HEX_DIGIT(n>>12);
    *p++=HEX_DIGIT(n>>8);
    *p++=HEX_DIGIT(n>>4);
    *p++=HEX_DIGIT(n);
  }
  *p='\0';
}

#endif

static void fc_store_abort(fc_filter_data* fcdata);

static int fc_store_begin(fc_filter_data* fcdata) {
  fcdata->fd=open(fcdata->tmp_key, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (fcdata->fd==-1) {
    nxweb_log_error("fc_store_begin(): can't create cache file %s; could be open by another request", fcdata->tmp_key);
    return NXWEB_OK;
  }
  nxweb_http_response* resp=fcdata->resp;
  assert(resp);
  nxb_buffer* nxb=resp->nxb;
  assert(nxb);
  fc_file_header hdr;

  memset(&hdr, 0, sizeof(hdr));
  hdr.signature=NXFC_SIGNATURE;
  hdr.header_size=FC_HEADER_SIZE;
  hdr.status_code=resp->status_code;
  hdr.content_length.ssz=resp->content_length;
  hdr.last_modified.tim=resp->last_modified;
  hdr.expires.tim=resp->expires;
  hdr.max_age.tim=resp->max_age;
  hdr.gzip_encoded=resp->gzip_encoded;
  hdr.ssi_on=resp->ssi_on;

  int len1=0, len2=0, len3=0;
  if (resp->content_type) len1=strlen(resp->content_type)+1;
  if (resp->content_charset) len2=strlen(resp->content_charset)+1;
  if (resp->cache_control) len3=strlen(resp->cache_control)+1;

  hdr.data_size=1+len1+len2+len3; // 1st byte for "null-pointer"
  char* data=nxb_alloc_obj(nxb, hdr.data_size);
  char* p=data;
  *p++='\0'; // 1st byte for "null-pointer"
  if (len1) { memcpy(p, resp->content_type, len1); p+=len1; }
  if (len2) { memcpy(p, resp->content_charset, len2); p+=len2; }
  if (len3) { memcpy(p, resp->cache_control, len3); p+=len3; }

  hdr.content_type.u64=len1? 1+0 : 0; // 1st byte for "null-pointer"
  hdr.content_charset.u64=len2? 1+len1 : 0;
  hdr.cache_control.u64=len3? 1+len1+len2 : 0;
  // hdr.extra_raw_headers.u64=0; // TODO

  hdr.content_offset.offs=FC_HEADER_SIZE+hdr.data_size;
  if (write(fcdata->fd, &hdr, FC_HEADER_SIZE)!=FC_HEADER_SIZE) {
    nxweb_log_error("fc_store_begin(): can't write header into cache file %s", fcdata->tmp_key);
    fc_store_abort(fcdata);
    return -1;
  }
  if (write(fcdata->fd, data, hdr.data_size)!=hdr.data_size) {
    nxweb_log_error("fc_store_begin(): can't write data into cache file %s", fcdata->tmp_key);
    fc_store_abort(fcdata);
    return -1;
  }
  return 0;
}

static int fc_read(fc_filter_data* fcdata, nxweb_http_response* resp) {
  int fd;

  fd=open(fcdata->fdata.cache_key, O_RDONLY);
  if (fd==-1) {
    nxweb_log_error("fc_read(): can't open cache file %s", fcdata->fdata.cache_key);
    goto E1;
  }
  fc_file_header hdr;
  if (read(fd, &hdr, FC_HEADER_SIZE)!=FC_HEADER_SIZE) {
    nxweb_log_error("fc_read(): can't read header from cache file %s", fcdata->fdata.cache_key);
    goto E2;
  }
  if (hdr.signature!=NXFC_SIGNATURE || hdr.header_size!=FC_HEADER_SIZE) {
    nxweb_log_error("fc_read(): wrong header in cache file %s; trying to delete it", fcdata->fdata.cache_key);
    unlink(fcdata->fdata.cache_key); // try to clean up corrupt file
    goto E2;
  }
  char* data=nxb_alloc_obj(resp->nxb, hdr.data_size);
  if (read(fd, data, hdr.data_size)!=hdr.data_size) {
    nxweb_log_error("fc_read(): can't read data from cache file %s", fcdata->fdata.cache_key);
    goto E2;
  }
  hdr.content_type.cptrc=hdr.content_type.u64? data+hdr.content_type.u64 : 0;
  hdr.content_charset.cptrc=hdr.content_charset.u64? data+hdr.content_charset.u64 : 0;
  hdr.cache_control.cptrc=hdr.cache_control.u64? data+hdr.cache_control.u64 : 0;

  struct stat finfo;
  if (fstat(fd, &finfo)==-1) {
    nxweb_log_error("fc_read(): can't get content length from cache file %s", fcdata->fdata.cache_key);
    goto E2;
  }
  if (hdr.content_length.ssz<0) {
    hdr.content_length.ssz=finfo.st_size - hdr.content_offset.offs;
  }

  resp->status_code=hdr.status_code;
  resp->content_length=hdr.content_length.ssz;
  resp->last_modified=hdr.last_modified.tim;
  resp->expires=hdr.expires.tim;
  resp->max_age=hdr.max_age.tim;
  resp->gzip_encoded=hdr.gzip_encoded;
  resp->ssi_on=hdr.ssi_on;

  resp->content_type=hdr.content_type.cptrc;
  resp->content_charset=hdr.content_charset.cptrc;
  resp->cache_control=hdr.cache_control.cptrc;

  resp->sendfile_info=finfo;
  resp->sendfile_path=fcdata->fdata.cache_key;
  resp->sendfile_path_root_len=fcdata->fdata.cache_key_root_len;
  resp->sendfile_fd=fd;
  resp->sendfile_offset=hdr.content_offset.offs;
  resp->sendfile_end=resp->sendfile_offset+resp->content_length;
  resp->mtype=0;

  return 0;

  E2:
  close(fd);
  E1:
  return -1;
}

static int fc_store_append(fc_filter_data* fcdata, const void* ptr, nxe_size_t size) {
  if (write(fcdata->fd, ptr, size)!=size) {
    nxweb_log_error("fc_store_append(): can't write %ld bytes into cache file %s", size, fcdata->tmp_key);
    fc_store_abort(fcdata);
    return -1;
  }
  return 0;
}

static int fc_store_close(fc_filter_data* fcdata) {
  close(fcdata->fd);
  fcdata->fd=0;
  if (rename(fcdata->tmp_key, fcdata->fdata.cache_key)==-1) {
    nxweb_log_error("fc_store_close(): can't rename %s cache file into %s", fcdata->tmp_key, fcdata->fdata.cache_key);
    unlink(fcdata->tmp_key);
    return -1;
  }
  struct utimbuf ut={.actime=fcdata->expires_time, .modtime=fcdata->expires_time};
  utime(fcdata->fdata.cache_key, &ut);
  return 0;
}

static void fc_store_abort(fc_filter_data* fcdata) {
  close(fcdata->fd);
  unlink(fcdata->tmp_key);
  fcdata->fd=-1;
}

static void fc_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  fc_filter_data* fcdata=OBJ_PTR_FROM_FLD_PTR(fc_filter_data, data_out, is);
  nxe_loop* loop=is->super.loop;

  nxe_istream* prev_is=fcdata->data_in.pair;
  if (prev_is) {
    if (prev_is->ready) {
      fcdata->data_in.ready=1;
      ISTREAM_CLASS(prev_is)->do_write(prev_is, &fcdata->data_in);
    }
    if (!prev_is->ready) {
      nxe_istream_unset_ready(is);
      nxe_ostream_set_ready(loop, &fcdata->data_in); // get notified when prev_is becomes ready again
    }
  }
  else {
    // nxweb_log_error("no connected device for snode->data_in"); -- this is OK
    nxe_istream_unset_ready(is);
  }
}

static nxe_ssize_t fc_data_in_write_or_sendfile(nxe_ostream* os, nxe_istream* is, int fd, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  fc_filter_data* fcdata=OBJ_PTR_FROM_FLD_PTR(fc_filter_data, data_in, os);
  nxe_loop* loop=os->super.loop;
  nxe_ssize_t bytes_sent=0;
  if (size>0 || *flags&NXEF_EOF) {
    nxe_ostream* next_os=fcdata->data_out.pair;
    if (next_os) {
      nxe_flags_t wflags=*flags;
      if (next_os->ready) {
        if (fd) { // invoked as sendfile
          assert(OSTREAM_CLASS(next_os)->sendfile);
          bytes_sent=OSTREAM_CLASS(next_os)->sendfile(next_os, &fcdata->data_out, fd, ptr, size, &wflags);
          if (bytes_sent>0 && fcdata->fd && fcdata->fd!=-1) {
            char* buf=malloc(bytes_sent);
            if (lseek(fd, ptr.offs, SEEK_SET)!=-1 && read(fd, buf, bytes_sent)==bytes_sent) {
              fc_store_append(fcdata, buf, bytes_sent);
            }
            else {
              nxweb_log_error("fc_data_in_write_or_sendfile(): can't read %ld bytes from fd", bytes_sent);
              fc_store_abort(fcdata);
            }
            free(buf);
          }
        }
        else {
          bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &fcdata->data_out, 0, ptr, size, &wflags);
          if (bytes_sent>0 && fcdata->fd && fcdata->fd!=-1) {
            fc_store_append(fcdata, ptr.cptr, bytes_sent);
          }
        }
      }
      if (!next_os->ready) {
        nxe_ostream_unset_ready(os);
        nxe_istream_set_ready(loop, &fcdata->data_out); // get notified when next_os becomes ready again
      }
    }
    else {
      // nxweb_log_error("no connected device for strm->data_out"); -- this is OK
      nxe_ostream_unset_ready(os);
    }
  }
  //nxweb_log_error("fc_write %d bytes: %.*s", (int)bytes_sent, (int)bytes_sent, ptr.cptrc);
  if (*flags&NXEF_EOF && bytes_sent==size) {
    // end of stream => close cache file
    if (fcdata->fd && fcdata->fd!=-1) {
      fc_store_close(fcdata);
    }
  }
  return bytes_sent;
}

static const nxe_istream_class fc_data_out_class={.do_write=fc_data_out_do_write};
static const nxe_ostream_class fc_data_in_class={.write=fc_data_in_write_or_sendfile, .sendfile=fc_data_in_write_or_sendfile};

static nxweb_filter_data* fc_init(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  fc_filter_data* fcdata=nxb_calloc_obj(req->nxb, sizeof(fc_filter_data));
  fcdata->data_out.super.cls.is_cls=&fc_data_out_class;
  fcdata->data_out.evt.cls=NXE_EV_STREAM;
  fcdata->data_in.super.cls.os_cls=&fc_data_in_class;
  fcdata->data_in.ready=1;
  fcdata->data_out.ready=1;
  return &fcdata->fdata;
}

static void fc_finalize(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  fc_filter_data* fcdata=(fc_filter_data*)fdata;
  if (fcdata->data_out.pair)
    nxe_disconnect_streams(&fcdata->data_out, fcdata->data_out.pair);
  if (fcdata->data_in.pair)
    nxe_disconnect_streams(fcdata->data_in.pair, &fcdata->data_in);
  if (fcdata->fd && fcdata->fd!=-1) {
    close(fcdata->fd);
    unlink(fcdata->tmp_key);
  }
}

static nxweb_result fc_translate_cache_key(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  // only accept virtual key ( ) => make it file key
  if (*key!=' ') { // virtual key
    fdata->bypass=1;
    return NXWEB_NEXT;
  }
  assert(conn->handler->file_cache_dir);
  assert(root_len==1);
  int plen=strlen(key)-root_len;
  int rlen=strlen(conn->handler->file_cache_dir);
  char* fc_key=nxb_alloc_obj(req->nxb, rlen+41+1);
  strcpy(fc_key, conn->handler->file_cache_dir);
  sha1path(key+root_len, plen, fc_key+rlen);
  fdata->cache_key=fc_key;
  fdata->cache_key_root_len=rlen;
  return NXWEB_OK;
}

static nxweb_result fc_serve_from_cache(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  fc_filter_data* fcdata=(fc_filter_data*)fdata;
  if (fc_read(fcdata, resp)==-1) return NXWEB_NEXT;
  return NXWEB_OK;
}

static nxweb_result fc_revalidate_cache(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  // cache file exists but outdated
  // add if-modified-since header to request
  nxweb_log_error("revalidating cache file %s for uri %s", fdata->cache_key, req->uri);
  return NXWEB_OK;
}

static nxweb_result fc_do_filter(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  // if 'not modified' and added_ims flag set => use cached version
  if (!fdata->cache_key || !*fdata->cache_key) return NXWEB_OK;
  if (resp->status_code==304) {
    return NXWEB_OK;
  }
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;
  if (resp->no_cache) return NXWEB_OK;
  if (!resp->content_out) return NXWEB_OK;

  if (resp->gzip_encoded) {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }

  time_t cur_time=time(0);
  time_t expires_time=0;
  if (resp->max_age) {
    expires_time=cur_time+resp->max_age; // note: max_age could be -1, which is OK
  }
  else if (resp->expires) {
    expires_time=resp->expires;
  }
  else if (resp->last_modified) {
    expires_time=cur_time-1;
  }
  else {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }

  fc_filter_data* fcdata=(fc_filter_data*)fdata;
  fcdata->tmp_key=nxb_alloc_obj(req->nxb, strlen(fdata->cache_key)+4+1);
  strcat(strcpy(fcdata->tmp_key, fdata->cache_key), ".tmp");
  if (nxweb_mkpath(fcdata->tmp_key, 0755)==-1) {
    nxweb_log_error("can't create path to cache file %s; check permissions", fcdata->tmp_key);
    return NXWEB_OK;
  }
  fcdata->resp=resp;
  fcdata->expires_time=expires_time;
  if (!resp->last_modified) resp->last_modified=cur_time;
  if (fc_store_begin(fcdata)==-1) return NXWEB_OK;
  nxe_connect_streams(conn->tdata->loop, resp->content_out, &fcdata->data_in);
  resp->content_out=&fcdata->data_out;
  return NXWEB_OK;
}

nxweb_filter file_cache_filter={.name="file_cache", .init=fc_init, .finalize=fc_finalize,
        .translate_cache_key=fc_translate_cache_key,
        .serve_from_cache=fc_serve_from_cache, .revalidate_cache=fc_revalidate_cache, .do_filter=fc_do_filter};
