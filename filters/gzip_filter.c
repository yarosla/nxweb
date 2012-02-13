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

#include "../nxweb/nxweb.h"
#include <malloc.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <utime.h>

#include <zlib.h>

static nxweb_filter_data* gzip_init(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(nxweb_filter_data));
  fdata->bypass=!req->accept_gzip_encoding;
  return fdata;
}

/*
static char* gzip_decode_uri(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, char* uri) {
  return uri; // unchanged
}
*/

static nxweb_result gzip_translate_cache_key(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  // if filter does not store its own cache file
  // it must not store cache_key and just return NXWEB_NEXT
  // it must not implement serve_from_cache either
  if (!*key) return NXWEB_OK;
  if (*key==' ') { // virtual key
    return NXWEB_NEXT;
  }
  assert(conn->handler->gzip_dir);
  if (!resp->mtype) {
    resp->mtype=nxweb_get_mime_type_by_ext(key);
  }
  if (!resp->mtype->gzippable) {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }
  int plen=strlen(key)-root_len;
  int rlen=strlen(conn->handler->gzip_dir);
  char* gzip_key=nxb_calloc_obj(req->nxb, rlen+plen+3+1);
  strcpy(gzip_key, conn->handler->gzip_dir);
  strcpy(gzip_key+rlen, key+root_len);
  strcpy(gzip_key+rlen+plen, ".gz");
  fdata->cache_key=gzip_key;
  fdata->cache_key_root_len=rlen;
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

void* myalloc(void* q, unsigned n, unsigned m) {
  q=Z_NULL;
  return nx_calloc(n*m);
}

void myfree(void* q, void* p) {
  q=Z_NULL;
  nx_free(p);
}

#define BUF_SIZE 8192

static int gzip_file(const char* src, const char* dst) {
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
  z_stream zs;
  zs.zalloc=myalloc;
  zs.zfree=myfree;
  zs.opaque=Z_NULL;
  zs.next_in=Z_NULL;
  if (deflateInit2(&zs, 8, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)!=Z_OK) { // level 0-9
    nxweb_log_error("deflateInit2() failed for file %s", src);
    return -1;
  }
  void* ibuf=nx_alloc(BUF_SIZE);
  void* obuf=nx_alloc(BUF_SIZE);
  ssize_t cnt;
  do {
    cnt=read(sfd, ibuf, BUF_SIZE);
    if (cnt<0) {
      nx_free(ibuf);
      nx_free(obuf);
      close(sfd);
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
        close(sfd);
        close(dfd);
        unlink(dst);
        return -1;
      }
    } while (zs.avail_out==0);
  } while (cnt==BUF_SIZE);
  deflateEnd(&zs);

  nx_free(ibuf);
  nx_free(obuf);
  close(sfd);
  close(dfd);
  struct utimbuf ut={.actime=finfo.st_atime, .modtime=finfo.st_mtime};
  utime(dst, &ut);
  return 0;
}

static int gzip_mem_buf(const void* src, unsigned src_size, nxb_buffer* nxb, const void** dst, unsigned* dst_size) {
  z_stream zs;
  zs.zalloc=myalloc;
  zs.zfree=myfree;
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

static nxweb_result gzip_serve_from_cache(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  nxweb_send_file(resp, (char*)fdata->cache_key, fdata->cache_key_root_len, &fdata->cache_key_finfo, 1, 0, 0, resp->mtype, NXWEB_DEFAULT_CHARSET);
  return NXWEB_OK;
}

static nxweb_result gzip_do_filter(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  assert(!resp->gzip_encoded);
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;
  if (resp->sendfile_path) { // gzip on disk
    assert(fdata->cache_key);
    int rlen=fdata->cache_key_root_len;
    const char* fpath=fdata->cache_key;

    if (nxweb_mkpath((char*)fpath, 0755)<0
     || gzip_file(resp->sendfile_path, fpath)<0) {
      nxweb_log_error("nxweb_mkpath() or gzip_file() failed for %s", resp->sendfile_path);
      return NXWEB_ERROR;
    }
    nxweb_log_error("gzipped %s", resp->sendfile_path);

    resp->sendfile_path=fpath;
    resp->sendfile_path_root_len=rlen;
    if (stat(fpath, &resp->sendfile_info)==-1) return NXWEB_ERROR;
    resp->content_length=resp->sendfile_info.st_size;
    resp->last_modified=resp->sendfile_info.st_mtime;
    resp->gzip_encoded=1;
    return NXWEB_OK;
  }
  else if (resp->content && resp->content_length>0) { // gzip in memory
    if (!resp->mtype) {
      resp->mtype=nxweb_get_mime_type(resp->content_type);
    }
    if (!resp->mtype->gzippable) {
      fdata->bypass=1;
      return NXWEB_NEXT;
    }
    unsigned zsize=0;
    const void* zbuf=0;
    gzip_mem_buf(resp->content, resp->content_length, resp->nxb, &zbuf, &zsize);
    resp->content=zbuf;
    resp->content_length=zsize;
    resp->gzip_encoded=1;
    return NXWEB_OK;
  }
  // TODO gzip istream
  assert(0); // not implemented
  return NXWEB_OK;
}

nxweb_filter gzip_filter={.name="gzip", .init=gzip_init, .translate_cache_key=gzip_translate_cache_key,
        .serve_from_cache=gzip_serve_from_cache, .do_filter=gzip_do_filter};
