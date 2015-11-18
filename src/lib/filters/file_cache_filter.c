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

#define NXFC_SIGNATURE (0x6366786e)

typedef struct nxweb_filter_file_cache {
  nxweb_filter base;
  const char* cache_dir;
  _Bool dont_cache_queries:1;
} nxweb_filter_file_cache;

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
  uint32_t templates_on:1;
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
} fc_file_header;

#define FC_HEADER_SIZE (sizeof(fc_file_header))

typedef struct fc_filter_data {
  _Bool revalidation_mode:1; // If-Modified-Since header has been added by this filter
  nxe_ostream data_in;
  nxe_istream data_out;
  time_t expires_time;
  time_t if_modified_since_original;
  int fd; // cache file
  const char* cache_dir;
  char* cache_fpath;
  char* tmp_fpath;
  nxweb_http_response* resp;
  struct stat cache_finfo;
  int input_fd;
  nxd_fbuffer fb;
  fc_file_header hdr;
} fc_filter_data;


static void fc_store_abort(fc_filter_data* fcdata);

static int fc_store_begin(fc_filter_data* fcdata) {
  assert(!fcdata->fd || fcdata->fd==-1);
  fcdata->fd=open(fcdata->tmp_fpath, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (fcdata->fd==-1) {
    nxweb_log_error("fc_store_begin(): can't create cache file %s; could be open by another request; errno=%d", fcdata->tmp_fpath, errno);
    return NXWEB_OK;
  }
  nxweb_http_response* resp=fcdata->resp;
  assert(resp);
  nxb_buffer* nxb=resp->nxb;
  assert(nxb);
  nxb_start_stream(nxb);
  fc_file_header* hdr=&fcdata->hdr;

  memset(hdr, 0, sizeof(*hdr));
  hdr->signature=NXFC_SIGNATURE;
  hdr->header_size=FC_HEADER_SIZE;
  hdr->status_code=resp->status_code;
  hdr->content_length.ssz=resp->content_length;
  hdr->last_modified.tim=resp->last_modified;
  hdr->expires.tim=resp->expires;
  hdr->max_age.tim=resp->max_age;
  hdr->gzip_encoded=resp->gzip_encoded;
  hdr->ssi_on=resp->ssi_on;
  hdr->templates_on=resp->templates_on;

  uint64_t idx=0;
  int len;
  nxb_append_char(nxb, '\0'); idx++;
  if (resp->content_type) {
    hdr->content_type.u64=idx;
    len=strlen(resp->content_type)+1;
    nxb_append(nxb, resp->content_type, len);
    idx+=len;
  }
  if (resp->content_charset) {
    hdr->content_charset.u64=idx;
    len=strlen(resp->content_charset)+1;
    nxb_append(nxb, resp->content_charset, len);
    idx+=len;
  }
  if (resp->cache_control) {
    hdr->cache_control.u64=idx;
    len=strlen(resp->cache_control)+1;
    nxb_append(nxb, resp->cache_control, len);
    idx+=len;
  }
  if (resp->headers) {
    hdr->extra_raw_headers.u64=idx;
    _nxweb_add_extra_response_headers(nxb, resp->headers);
    nxb_append_char(nxb, '\0');
  }

  char* data=nxb_finish_stream(nxb, &len);
  hdr->data_size=len;

  hdr->content_offset.offs=FC_HEADER_SIZE+hdr->data_size;
  if (write(fcdata->fd, hdr, FC_HEADER_SIZE)!=FC_HEADER_SIZE) {
    nxweb_log_error("fc_store_begin(): can't write header into cache file %s", fcdata->tmp_fpath);
    fc_store_abort(fcdata);
    return -1;
  }
  if (write(fcdata->fd, data, hdr->data_size)!=hdr->data_size) {
    nxweb_log_error("fc_store_begin(): can't write data into cache file %s", fcdata->tmp_fpath);
    fc_store_abort(fcdata);
    return -1;
  }
  return 0;
}

static int fc_read_header(fc_filter_data* fcdata) {
  if (fcdata->fd && fcdata->fd!=-1) {
    assert(fcdata->hdr.header_size==FC_HEADER_SIZE);
    if (lseek(fcdata->fd, FC_HEADER_SIZE, SEEK_SET)==-1) {
      nxweb_log_error("fc_read_header(): can't lseek cache file %s", fcdata->cache_fpath);
      return -1;
    }
    return 0;
  }
  fcdata->fd=open(fcdata->cache_fpath, O_RDONLY);
  if (fcdata->fd==-1) {
    nxweb_log_debug("fc_read_header(): can't open cache file %s", fcdata->cache_fpath);
    return -1;
  }
  if (fstat(fcdata->fd, &fcdata->cache_finfo)==-1) {
    nxweb_log_error("fc_read_header(): can't fstat cache file %s", fcdata->cache_fpath);
    close(fcdata->fd);
    fcdata->fd=0;
    return -1;
  }
  fc_file_header* hdr=&fcdata->hdr;
  if (read(fcdata->fd, hdr, FC_HEADER_SIZE)!=FC_HEADER_SIZE) {
    nxweb_log_error("fc_read_header(): can't read header from cache file %s", fcdata->cache_fpath);
    close(fcdata->fd);
    fcdata->fd=0;
    return -1;
  }
  if (hdr->signature!=NXFC_SIGNATURE || hdr->header_size!=FC_HEADER_SIZE) {
    nxweb_log_error("fc_read_header(): wrong header in cache file %s; trying to delete it", fcdata->cache_fpath);
    unlink(fcdata->cache_fpath); // try to clean up corrupt file
    close(fcdata->fd);
    fcdata->fd=0;
    return -1;
  }
  return 0;
}

static int fc_read_data(fc_filter_data* fcdata, nxweb_http_response* resp) {
  fc_file_header* hdr=&fcdata->hdr;
  int fd=fcdata->fd;
  assert(fd && fd!=-1);
  char* data=nxb_alloc_obj(resp->nxb, hdr->data_size);
  if (read(fd, data, hdr->data_size)!=hdr->data_size) {
    nxweb_log_error("fc_read(): can't read data from cache file %s", fcdata->cache_fpath);
    goto E2;
  }
  hdr->content_type.cptrc=hdr->content_type.u64? data+hdr->content_type.u64 : 0;
  hdr->content_charset.cptrc=hdr->content_charset.u64? data+hdr->content_charset.u64 : 0;
  hdr->cache_control.cptrc=hdr->cache_control.u64? data+hdr->cache_control.u64 : 0;
  hdr->extra_raw_headers.cptrc=hdr->extra_raw_headers.u64? data+hdr->extra_raw_headers.u64 : 0;

  if (hdr->content_length.ssz<0) {
    hdr->content_length.ssz=fcdata->cache_finfo.st_size - hdr->content_offset.offs;
  }

  resp->status_code=hdr->status_code;
  assert(resp->status_code==200 || !resp->status_code);
  resp->status="OK";
  resp->content_length=hdr->content_length.ssz;
  resp->last_modified=hdr->last_modified.tim;
  // resp->expires=hdr->expires.tim;
  resp->max_age=hdr->max_age.tim;
  resp->gzip_encoded=hdr->gzip_encoded;
  resp->ssi_on=hdr->ssi_on;
  resp->templates_on=hdr->templates_on;

  resp->content_type=hdr->content_type.cptrc;
  resp->content_charset=hdr->content_charset.cptrc;
  resp->cache_control=hdr->cache_control.cptrc;
  resp->extra_raw_headers=hdr->extra_raw_headers.cptrc;

  resp->sendfile_info=fcdata->cache_finfo;
  resp->sendfile_path=fcdata->cache_fpath;
  fcdata->input_fd=resp->sendfile_fd; // save to close on finalize
  resp->sendfile_fd=fd; // shall auto-close
  resp->sendfile_offset=hdr->content_offset.offs;
  resp->sendfile_end=resp->sendfile_offset+resp->content_length;
  resp->mtype=0;
  resp->chunked_encoding=0;
  resp->chunked_autoencode=0;

  resp->content=0;
  // override cache control
  resp->etag=0;
  resp->max_age=0;
  resp->cache_control="must-revalidate";
  resp->expires=fcdata->cache_finfo.st_mtime;

  fcdata->fd=0; // don't auto-close twice

  nxd_fbuffer_init(&fcdata->fb, resp->sendfile_fd, resp->sendfile_offset, resp->sendfile_end);
  resp->content_out=&fcdata->fb.data_out;

  return 0;

  E2:
  if (fd && fd!=-1) close(fd);
  fcdata->fd=0;
  E1:
  return -1;
}

static int fc_store_append(fc_filter_data* fcdata, const void* ptr, nxe_size_t size) {
  if (write(fcdata->fd, ptr, size)!=size) {
    nxweb_log_error("fc_store_append(): can't write %ld bytes into cache file %s", size, fcdata->tmp_fpath);
    fc_store_abort(fcdata);
    return -1;
  }
  return 0;
}

static int fc_store_close(fc_filter_data* fcdata) {
  struct timeval mtimes[2]={
    {.tv_sec=fcdata->expires_time},
    {.tv_sec=fcdata->expires_time}
  };
  futimes(fcdata->fd, mtimes);
  close(fcdata->fd);
  fcdata->fd=0;
  // use link/unlink instead of rename to preserve timestamp
  unlink(fcdata->cache_fpath);
  if (link(fcdata->tmp_fpath, fcdata->cache_fpath)==-1) {
    nxweb_log_error("fc_store_close(): can't rename %s cache file into %s", fcdata->tmp_fpath, fcdata->cache_fpath);
    unlink(fcdata->tmp_fpath);
    fcdata->tmp_fpath=0;
    return -1;
  }
  unlink(fcdata->tmp_fpath);
  return 0;
}

static void fc_store_abort(fc_filter_data* fcdata) {
  close(fcdata->fd);
  unlink(fcdata->tmp_fpath);
  fcdata->tmp_fpath=0;
  fcdata->fd=-1;
}

static void fc_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  fc_filter_data* fcdata=OBJ_PTR_FROM_FLD_PTR(fc_filter_data, data_out, is);
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("fc_data_out_do_write");

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

static nxe_ssize_t fc_data_in_write_or_sendfile(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  fc_filter_data* fcdata=OBJ_PTR_FROM_FLD_PTR(fc_filter_data, data_in, os);
  nxe_loop* loop=os->super.loop;

  nxweb_log_debug("fc_data_in_write_or_sendfile");

  nxe_ssize_t bytes_sent=0;
  if (size>0 || *flags&NXEF_EOF) {
    nxe_ostream* next_os=fcdata->data_out.pair;
    if (next_os) {
      nxe_flags_t wflags=*flags;
      if (next_os->ready) {
        if (fd) { // invoked as sendfile
          bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &fcdata->data_out, fd, fr, ptr, size, &wflags);
          if (bytes_sent>0 && fcdata->fd && fcdata->fd!=-1) {
            char* buf=malloc(bytes_sent);
            if (buf && lseek(fd, ptr.offs, SEEK_SET)!=-1 && read(fd, buf, bytes_sent)==bytes_sent) {
              fc_store_append(fcdata, buf, bytes_sent);
            }
            else {
              nxweb_log_error("fc_data_in_write_or_sendfile(): can't read %ld bytes from fd", bytes_sent);
              fc_store_abort(fcdata);
            }
            if (buf) free(buf);
          }
        }
        else {
          bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &fcdata->data_out, 0, 0, ptr, size, &wflags);
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
static const nxe_ostream_class fc_data_in_class={.write=fc_data_in_write_or_sendfile /*, .sendfile=fc_data_in_write_or_sendfile*/ };

void _nxweb_fc_init(fc_filter_data* fcdata, const char* cache_dir) {
  assert(cache_dir && *cache_dir);
  fcdata->cache_dir=cache_dir;
  fcdata->data_out.super.cls.is_cls=&fc_data_out_class;
  fcdata->data_out.evt.cls=NXE_EV_STREAM;
  fcdata->data_in.super.cls.os_cls=&fc_data_in_class;
  fcdata->data_in.ready=1;
  fcdata->data_out.ready=1;
}

fc_filter_data* _nxweb_fc_create(nxb_buffer* nxb, const char* cache_dir) {
  assert(cache_dir);
  fc_filter_data* fcdata=nxb_calloc_obj(nxb, sizeof(fc_filter_data));
  _nxweb_fc_init(fcdata, cache_dir);
  return fcdata;
}

void _nxweb_fc_finalize(fc_filter_data* fcdata) {
  if (fcdata->data_out.pair)
    nxe_disconnect_streams(&fcdata->data_out, fcdata->data_out.pair);
  if (fcdata->data_in.pair)
    nxe_disconnect_streams(fcdata->data_in.pair, &fcdata->data_in);
  if (fcdata->fd && fcdata->fd!=-1) {
    close(fcdata->fd);
    if (fcdata->tmp_fpath) unlink(fcdata->tmp_fpath);
  }
  nxd_fbuffer_finalize(&fcdata->fb);
  if (fcdata->input_fd && fcdata->input_fd!=-1) {
    close(fcdata->input_fd);
  }
}

static nxweb_filter_data* fc_init(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (((nxweb_filter_file_cache*)filter)->dont_cache_queries && strchr(req->uri, '?')) return 0; // bypass requests with query string
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(nxweb_filter_data));
  fdata->fcache=_nxweb_fc_create(req->nxb, ((nxweb_filter_file_cache*)filter)->cache_dir);
  return fdata;
}

static void fc_finalize(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  _nxweb_fc_finalize(fdata->fcache);
}

static nxweb_result fc_translate_cache_key(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key) {
  fdata->cache_key=key; // just store it
}

static void fc_build_cache_fpath(nxb_buffer* nxb, fc_filter_data* fcdata, const char* cache_key) {
  nxb_start_stream(nxb);
  nxb_append_str(nxb, fcdata->cache_dir);
  if (cache_key[0]=='.' && cache_key[1]=='.' && cache_key[2]=='/') cache_key+=2; // avoid going up dir tree
  if (*cache_key!='/') nxb_append_char(nxb, '/');
  nxb_append_str(nxb, cache_key);
  nxb_append(nxb, ".nxc", sizeof(".nxc")); // including null-terminator
  fcdata->cache_fpath=nxb_finish_stream(nxb, 0);
}

static nxweb_result fc_serve(fc_filter_data* fcdata, nxweb_http_request* req, nxweb_http_response* resp, time_t cur_time) {
  if (fc_read_header(fcdata)==-1) return NXWEB_NEXT; // no valid cached content
  /*
  if (req->if_modified_since && fcdata->hdr.last_modified.tim<=req->if_modified_since) {
    close(fcdata->fd);
    fcdata->fd=0;
    resp->status_code=304;
    resp->status="Not Modified";
    resp->content=0;
    resp->content_out=0; // reset content_out
    resp->last_modified=fcdata->hdr.last_modified.tim;
    resp->etag=0;
    resp->max_age=0;
    resp->cache_control="must-revalidate";
    resp->expires=fcdata->cache_finfo.st_mtime>cur_time? fcdata->cache_finfo.st_mtime : 0;
    return NXWEB_OK;
  }
  */
  if (fc_read_data(fcdata, resp)==-1) return NXWEB_NEXT; // no valid cached content
  if (resp->expires<=cur_time) resp->expires=0;
  return NXWEB_OK;
}

static nxweb_result fc_initiate_revalidation(fc_filter_data* fcdata, nxweb_http_request* req) {
  nxweb_log_info("revalidating cache file %s for uri %s", fcdata->cache_fpath, req->uri);
  assert(fcdata->fd && fcdata->fd!=-1);
  fc_file_header* hdr=&fcdata->hdr;
  fcdata->if_modified_since_original=req->if_modified_since;
  req->if_modified_since=hdr->last_modified.tim;
  fcdata->revalidation_mode=1;
  return NXWEB_REVALIDATE;
}

nxweb_result _nxweb_fc_serve_from_cache(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, const char* cache_key, fc_filter_data* fcdata, time_t check_time) {
  if (!cache_key) return NXWEB_NEXT;

  nxweb_log_debug("_nxweb_fc_serve_from_cache");

  fc_build_cache_fpath(req->nxb, fcdata, cache_key);
  if (fc_read_header(fcdata)==-1) {
    if (req->if_modified_since) {
      // content not cached although it must be
      // remove if_modified_since
      fcdata->if_modified_since_original=req->if_modified_since;
      req->if_modified_since=0;
      return NXWEB_NEXT;
    }
    else {
      return NXWEB_NEXT; // no cached content
    }
  }
  if (fcdata->cache_finfo.st_mtime<check_time) { // cache expired
    return fc_initiate_revalidation(fcdata, req);
  }
  else { // cache valid & not expired
    // raise(SIGABRT);
    return fc_serve(fcdata, req, resp, check_time);
  }
}

static nxweb_result fc_serve_from_cache(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, time_t check_time) {
  return _nxweb_fc_serve_from_cache(conn, req, resp, fdata->cache_key, fdata->fcache, check_time);
}

nxweb_result _nxweb_fc_revalidate(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, fc_filter_data* fcdata) {

  if (fcdata->if_modified_since_original) req->if_modified_since=fcdata->if_modified_since_original; // restore if_modified_since

  if (!fcdata->revalidation_mode) return NXWEB_NEXT; // go ahead with do_filter()

  // when in revalidation_mode and 'not modified' flag set OR gateway timeout => use cached version

  req->if_modified_since=fcdata->if_modified_since_original; // restore if_modified_since even if it was zero

  time_t cur_time=nxe_get_current_http_time(conn->tdata->loop);

  if (resp->status_code==304) {
    time_t expires_time=0;
    // check new max_age & expires headers if present
    if (resp->max_age>0) {
      expires_time=cur_time+resp->max_age;
    }
    else if (resp->expires && resp->expires > cur_time) {
      expires_time=resp->expires;
    }
    if (fc_serve(fcdata, req, resp, cur_time)!=NXWEB_OK) {
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
    if (!expires_time) {
      if (fcdata->hdr.max_age.tim>0) { // use previous max_age if was set
        expires_time=cur_time+fcdata->hdr.max_age.tim;
      }
    }
    if (expires_time) { // have new expires time
      struct utimbuf ut={.actime=expires_time, .modtime=expires_time};
      utime(fcdata->cache_fpath, &ut);
      resp->expires=expires_time;
    }
  }
  else if (resp->status_code/100==5) { // backend error => use cached data
    nxweb_log_error("backend returned %d; serving from cache req_id=%" PRIx64, resp->status_code, req->uid);
    if (fc_serve(fcdata, req, resp, cur_time)!=NXWEB_OK) {
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
  }
  else {
    if (fcdata->fd && fcdata->fd!=-1) {
      close(fcdata->fd);
      fcdata->fd=0;
    }
    return NXWEB_NEXT;
  }
  return NXWEB_OK;
}

nxweb_result _nxweb_fc_store(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, fc_filter_data* fcdata) {
  if (!fcdata->cache_fpath) return NXWEB_OK; // no cache key
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;
  if (resp->no_cache || resp->cache_private) return NXWEB_OK;
  nxd_http_server_proto_setup_content_out(&conn->hsp, resp);
  if (!resp->content_out) return NXWEB_OK;

  time_t cur_time=nxe_get_current_http_time(conn->tdata->loop);
  time_t expires_time=0;
  if (resp->max_age) {
    expires_time=cur_time+resp->max_age; // note: max_age could be -1, which is OK
  }
  else if (resp->expires) {
    expires_time=resp->expires;
  }
  else if (resp->last_modified) {
    expires_time=cur_time-5; // five seconds back in time => cache but require revalidation
  }
  else {
    return NXWEB_NEXT;
  }

  if (fcdata->hdr.header_size && fcdata->hdr.last_modified.tim==resp->last_modified) {
    nxweb_log_info("nothing new to store in %s", fcdata->cache_fpath);
    return NXWEB_OK;
  }

  fcdata->tmp_fpath=nxb_alloc_obj(req->nxb, strlen(fcdata->cache_fpath)+4+1);
  strcat(strcpy(fcdata->tmp_fpath, fcdata->cache_fpath), ".tmp");
  if (nxweb_mkpath(fcdata->tmp_fpath, 0755)==-1) {
    nxweb_log_error("can't create path to cache file %s; check permissions", fcdata->tmp_fpath);
    return NXWEB_OK;
  }
  fcdata->resp=resp;
  fcdata->expires_time=expires_time;
  if (!resp->last_modified) resp->last_modified=cur_time;
  resp->etag=0; // we do not support it
  if (fc_store_begin(fcdata)==-1) return NXWEB_OK;
  nxe_connect_streams(conn->tdata->loop, resp->content_out, &fcdata->data_in);
  resp->content_out=&fcdata->data_out;
  return NXWEB_OK;
}

static nxweb_result fc_do_filter(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {

  nxweb_log_debug("fc_do_filter");

  nxweb_result r=_nxweb_fc_revalidate(conn, req, resp, fdata->fcache);
  if (r!=NXWEB_NEXT) return r;
  return _nxweb_fc_store(conn, req, resp, fdata->fcache);
}

static nxweb_filter* fc_config(nxweb_filter* base, const nx_json* json) {
  nxweb_filter_file_cache* f=calloc(1, sizeof(nxweb_filter_file_cache)); // NOTE this will never be freed
  *f=*(nxweb_filter_file_cache*)base;
  f->cache_dir=nx_json_get(json, "cache_dir")->text_value;
  f->dont_cache_queries=nx_json_get(json, "dont_cache_queries")->int_value!=0;
  return (nxweb_filter*)f;
}

static nxweb_filter_file_cache file_cache_filter={.base={
        .config=fc_config,
        .init=fc_init, .finalize=fc_finalize,
        .translate_cache_key=fc_translate_cache_key,
        .serve_from_cache=fc_serve_from_cache, .do_filter=fc_do_filter}};

NXWEB_DEFINE_FILTER(file_cache, file_cache_filter.base);

nxweb_filter* nxweb_file_cache_filter_setup(const char* cache_dir) {
  nxweb_filter_file_cache* f=nx_alloc(sizeof(nxweb_filter_file_cache)); // NOTE this will never be freed
  *f=file_cache_filter;
  f->cache_dir=cache_dir;
  return (nxweb_filter*)f;
}
