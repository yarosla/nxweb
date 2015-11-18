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
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <utime.h>

#include <zlib.h>

typedef struct nxweb_filter_gzip {
  nxweb_filter base;
  // compression level is between 0 and 9: 1 gives best speed, 9 gives best compression, 0 gives no compression at all
  int compression_level;
  _Bool dont_cache_queries:1;
  const char* cache_dir;
} nxweb_filter_gzip;

typedef struct gzip_filter_data {
  nxweb_filter_data fdata;
  nxd_rbuffer rb;
  z_stream zs;
  int input_fd;
} gzip_filter_data;

static nxweb_result gzip_translate_cache_key(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key) {
  /* if filter's behavior (and therefore response content) depends
   * on some request parameters other than handler took care of (host, uri, etc.)
   * then we should add corresponding variations to cache key here.
   */

  // NOTE we have already checked req->accept_gzip_encoding in init() method

  // looking at request we can say that content is gzippable,
  // so let's add $gzip suffix
  int key_len=strlen(key);
  char* gzip_key=nxb_alloc_obj(req->nxb, key_len+5+1);
  memcpy(gzip_key, key, key_len);
  strcpy(gzip_key+key_len, "$gzip");
  fdata->cache_key=gzip_key;
  return NXWEB_OK;
}

/*
static int copy_file(const char* src, const char* dst) {
  struct stat finfo;
  if (stat(src, &finfo)==-1) {
    return -1;
  }
  int sfd=open(src, O_RDONLY);
  if (sfd==-1) return -1;
  int dfd=open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (dfd==-1) {
    close(sfd);
    return -1;
  }
  char buf[4096];
  ssize_t cnt;
  while ((cnt=read(sfd, buf, sizeof(buf)))>0) {
    if (write(dfd, buf, cnt)!=cnt) {
      close(sfd);
      close(dfd);
      unlink(dst);
      return -1;
    }
  }
  close(sfd);
  close(dfd);
  struct utimbuf ut={.actime=finfo.st_atime, .modtime=finfo.st_mtime};
  utime(dst, &ut);
  return 0;
}
*/

void* nxweb_gzip_filter_alloc(void* q, unsigned n, unsigned m) {
  return nx_alloc(n*m);
}

void nxweb_gzip_filter_free(void* q, void* p) {
  nx_free(p);
}

#define BUF_SIZE 8192

static int gzip_file_fd(const char* src, int sfd, const char* dst, struct stat* src_finfo);

static int gzip_file(const char* src, const char* dst, struct stat* src_finfo) {
  int sfd=open(src, O_RDONLY);
  if (sfd==-1) return -1;
  int result=gzip_file_fd(src, sfd, dst, src_finfo);
  close(sfd);
  return result;
}

static int gzip_file_fd(const char* src, int sfd, const char* dst, struct stat* src_finfo) {
  struct stat _finfo;
  if (!src_finfo) {
    if (fstat(sfd, &_finfo)==-1) {
      return -1;
    }
    src_finfo=&_finfo;
  }
  int dfd=open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (dfd==-1) {
    return -1;
  }
  z_stream zs;
  zs.zalloc=nxweb_gzip_filter_alloc;
  zs.zfree=nxweb_gzip_filter_free;
  zs.opaque=Z_NULL;
  zs.next_in=Z_NULL;
  if (deflateInit2(&zs, 8, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)!=Z_OK) { // level 0-9
    nxweb_log_error("deflateInit2() failed for file %s", src);
    return -1;
  }
  void* ibuf=nx_alloc(BUF_SIZE);
  void* obuf=nx_alloc(BUF_SIZE);
  ssize_t cnt;
  lseek(sfd, 0, SEEK_SET);
  do {
    cnt=read(sfd, ibuf, BUF_SIZE);
    if (cnt<0) {
      nx_free(ibuf);
      nx_free(obuf);
      close(dfd);
      unlink(dst);
      return -1;
    }
    zs.next_in=ibuf;
    zs.avail_in=cnt;
    do {
      zs.next_out=obuf;
      zs.avail_out=BUF_SIZE;
      (void)deflate(&zs, cnt<BUF_SIZE? Z_FINISH : Z_NO_FLUSH);
      if (write(dfd, obuf, BUF_SIZE-zs.avail_out)!=BUF_SIZE-zs.avail_out) {
        nx_free(ibuf);
        nx_free(obuf);
        close(dfd);
        unlink(dst);
        return -1;
      }
    } while (zs.avail_out==0);
  } while (cnt==BUF_SIZE);
  deflateEnd(&zs);

  nx_free(ibuf);
  nx_free(obuf);
  close(dfd);
  struct utimbuf ut={.actime=src_finfo->st_atime, .modtime=src_finfo->st_mtime};
  utime(dst, &ut);
  return 0;
}

static int gzip_mem_buf(const void* src, unsigned src_size, nxb_buffer* nxb, const void** dst, unsigned* dst_size) {
  z_stream zs;
  zs.zalloc=nxweb_gzip_filter_alloc;
  zs.zfree=nxweb_gzip_filter_free;
  zs.opaque=Z_NULL;
  zs.next_in=Z_NULL;
  if (deflateInit2(&zs, 5, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)!=Z_OK) { // level 0-9
    nxweb_log_error("deflateInit2() failed for memory buffer");
    return -1;
  }
  nxb_make_room(nxb, src_size>4096? src_size/4 : 1024);
  int room_avail;
  zs.next_in=(void*)src;
  zs.avail_in=src_size;
  do {
    nxb_make_room(nxb, 1024);
    zs.next_out=nxb_get_room(nxb, &room_avail);
    zs.avail_out=room_avail;
    (void)deflate(&zs, Z_FINISH);
    nxb_blank_fast(nxb, room_avail-zs.avail_out);
  } while (zs.avail_out==0);
  deflateEnd(&zs);
  return 0;
}


static void gzip_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_rbuffer* rb=OBJ_PTR_FROM_FLD_PTR(nxd_rbuffer, data_out, is);
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("gzip_data_out_do_write");

  nxe_size_t size;
  const void* ptr;
  nxe_flags_t flags=0;
  ptr=nxd_rbuffer_get_read_ptr(rb, &size, &flags);
  if (size>0 || flags&NXEF_EOF) {
    nxe_ssize_t bytes_sent=OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)ptr, size, &flags);
    if (bytes_sent>0) {
      nxd_rbuffer_read(rb, bytes_sent);
    }
  }
  else {
    nxe_istream_unset_ready(is);
  }
}

static nxe_ssize_t gzip_data_in_write(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* _flags) {
  nxd_rbuffer* rb=OBJ_PTR_FROM_FLD_PTR(nxd_rbuffer, data_in, os);
  gzip_filter_data* gdata=OBJ_PTR_FROM_FLD_PTR(gzip_filter_data, rb, rb);

  nxweb_log_debug("gzip_data_in_write");

  z_stream* zs=&gdata->zs;
  nxe_loop* loop=os->super.loop;
  nxe_ssize_t bytes_sent=0;
  int deflate_result=0;
  nxe_flags_t flags=*_flags;
  nx_file_reader_to_mem_ptr(fd, fr, &ptr, &size, &flags);
  if (size>0 || flags&NXEF_EOF) {
    nxe_size_t size_avail;
    zs->next_out=nxd_rbuffer_get_write_ptr(rb, &size_avail);
    if (size_avail) {
      zs->avail_out=size_avail;
      zs->next_in=ptr.ptr? ptr.ptr : ""; // deflate does not like nulls even when size is zero
      zs->avail_in=size;
      int flush=flags&NXEF_EOF? Z_FINISH : Z_SYNC_FLUSH;
      deflate_result=deflate(zs, flush);
      if (!((flush==Z_FINISH && (deflate_result==Z_STREAM_END || deflate_result==Z_OK)) || (flush==Z_SYNC_FLUSH && deflate_result==Z_OK))) {
        nxweb_log_warning("gzip-deflate unexpected: flush=%d, deflate_result=%d, avail_in=%d/%d, avail_out=%d/%d, ptr=%p, size=%d, rb.eof=%d",
                          flush, deflate_result, (int)zs->avail_in, (int)size, (int)zs->avail_out, (int)size_avail, ptr.ptr, (int)size, (int)gdata->rb.eof);
        deflate_result=Z_STREAM_END; // force out of loop
      }
      bytes_sent=size - zs->avail_in;
      nxd_rbuffer_write(rb, size_avail - zs->avail_out);
    }
    else {
      nxe_ostream_unset_ready(os);
      nxe_istream_set_ready(loop, &gdata->rb.data_out); // please read out compressed data
    }
  }
  if (flags&NXEF_EOF && bytes_sent==size && deflate_result==Z_STREAM_END) {
    nxe_ostream_unset_ready(os);
    gdata->rb.eof=1;
    nxe_istream_set_ready(os->super.loop, &rb->data_out); // even when no bytes received make sure we signal readiness on EOF
    deflateEnd(zs);
  }
  return bytes_sent;
}

static const nxe_istream_class gzip_data_out_class={.do_write=gzip_data_out_do_write};
static const nxe_ostream_class gzip_data_in_class={.write=gzip_data_in_write};


static nxweb_filter_data* gzip_init(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->accept_gzip_encoding) return 0; // bypass
  gzip_filter_data* gdata=nxb_calloc_obj(req->nxb, sizeof(gzip_filter_data));
  gdata->rb.data_out.super.cls.is_cls=&gzip_data_out_class;
  gdata->rb.data_out.evt.cls=NXE_EV_STREAM;
  gdata->rb.data_in.super.cls.os_cls=&gzip_data_in_class;
  gdata->rb.data_in.ready=1;
  // gdata->rb.data_out.ready=1;
  if (((nxweb_filter_gzip*)filter)->cache_dir) {
    if (!(((nxweb_filter_gzip*)filter)->dont_cache_queries && strchr(req->uri, '?'))) // do not cache requests with query string
      gdata->fdata.fcache=_nxweb_fc_create(req->nxb, ((nxweb_filter_gzip*)filter)->cache_dir);
  }
  return &gdata->fdata;
}

static void gzip_finalize(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  gzip_filter_data* gdata=(gzip_filter_data*)fdata;
  if (fdata->fcache) _nxweb_fc_finalize(fdata->fcache);
  if (gdata->rb.data_out.pair)
    nxe_disconnect_streams(&gdata->rb.data_out, gdata->rb.data_out.pair);
  if (gdata->rb.data_in.pair)
    nxe_disconnect_streams(gdata->rb.data_in.pair, &gdata->rb.data_in);
  if (gdata->input_fd) close(gdata->input_fd);
  deflateEnd(&gdata->zs); // this is safe to call twice
}

static nxweb_result gzip_serve_from_cache(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, time_t check_time) {
  return fdata->fcache? _nxweb_fc_serve_from_cache(conn, req, resp, fdata->cache_key, fdata->fcache, check_time) : NXWEB_NEXT;
}

static nxweb_result gzip_do_filter(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {

  nxweb_log_debug("gzip_do_filter");

  gzip_filter_data* gdata=(gzip_filter_data*)fdata;
  if (fdata->fcache) {
    nxweb_result r=_nxweb_fc_revalidate(conn, req, resp, fdata->fcache);
    if (r!=NXWEB_NEXT) return r;
  }

  if (resp->gzip_encoded) return NXWEB_NEXT;
  if (resp->status_code && resp->status_code!=200 && resp->status_code!=404) return NXWEB_OK;

  if (!resp->mtype && resp->content_type) {
    resp->mtype=nxweb_get_mime_type(resp->content_type);
  }
  if (!resp->mtype || !resp->mtype->gzippable) {
    return NXWEB_NEXT;
  }

  nxd_http_server_proto_setup_content_out(&conn->hsp, resp);

  if (resp->content_length>=0 && resp->content_length<100) {
    // too small to gzip
    nxweb_log_info("not gzipping %s as it is too small (%d bytes)", fdata->cache_key, (int)resp->content_length);
    return NXWEB_NEXT;
  }

  if (!resp->content_out) {
    // must be empty file
    nxweb_log_error("not gzipping %s as it has no content_out", fdata->cache_key);
    return NXWEB_NEXT;
  }

  // do gzip
  nxweb_log_info("gzipping %s", fdata->cache_key);
  nxd_rbuffer_init_ptr(&gdata->rb, nxb_alloc_obj(req->nxb, 16384), 16384);
  gdata->zs.zalloc=nxweb_gzip_filter_alloc;
  gdata->zs.zfree=nxweb_gzip_filter_free;
  gdata->zs.opaque=Z_NULL;
  gdata->zs.next_in=Z_NULL;
  if (deflateInit2(&gdata->zs, ((nxweb_filter_gzip*)filter)->compression_level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)!=Z_OK) { // level 0-9
    nxweb_log_error("deflateInit2() failed in gzip_do_filter()");
    return NXWEB_ERROR;
  }

  nxe_connect_streams(conn->tdata->loop, resp->content_out, &gdata->rb.data_in);
  resp->content_out=&gdata->rb.data_out;
  resp->gzip_encoded=1;
  // reset previous response content
  resp->content=0;
  resp->sendfile_path=0;
  if (resp->sendfile_fd) {
    // save it to close on finalize
    gdata->input_fd=resp->sendfile_fd;
    resp->sendfile_fd=0;
  }
  resp->content_length=-1; // chunked encoding
  resp->chunked_autoencode=1;

  return fdata->fcache? _nxweb_fc_store(conn, req, resp, fdata->fcache) : NXWEB_OK;
}

static nxweb_filter* gzip_config(nxweb_filter* base, const nx_json* json) {
  nxweb_filter_gzip* f=calloc(1, sizeof(nxweb_filter_gzip)); // NOTE this will never be freed
  *f=*(nxweb_filter_gzip*)base;
  f->cache_dir=nx_json_get(json, "cache_dir")->text_value;
  f->compression_level=(int)nx_json_get(json, "compression")->int_value;
  f->dont_cache_queries=nx_json_get(json, "dont_cache_queries")->int_value!=0;
  return (nxweb_filter*)f;
}

static nxweb_filter_gzip gzip_filter={.base={
        .config=gzip_config,
        .init=gzip_init, .finalize=gzip_finalize,
        .translate_cache_key=gzip_translate_cache_key,
        .serve_from_cache=gzip_serve_from_cache, .do_filter=gzip_do_filter},
        .compression_level=4, .cache_dir=0};

NXWEB_DEFINE_FILTER(gzip, gzip_filter.base);

// compression level is between 0 and 9: 1 gives best speed, 9 gives best compression, 0 gives no compression at all
nxweb_filter* nxweb_gzip_filter_setup(int compression_level, const char* cache_dir) {
  nxweb_filter_gzip* f=nx_alloc(sizeof(nxweb_filter_gzip)); // NOTE this will never be freed
  *f=gzip_filter;
  f->compression_level=compression_level;
  f->cache_dir=cache_dir;
  return (nxweb_filter*)f;
}
