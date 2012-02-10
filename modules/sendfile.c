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

#include "sendfile.h"

#include "../deps/ulib/alignhash_tpl.h"
#include "../deps/ulib/hash.h"

enum {FC_ERR_NO_ERROR=0, FC_ERR_NOT_FOUND, FC_ERR_IS_DIR, FC_ERR_TYPE};

typedef struct file_cache_rec {
  nxe_ssize_t content_length;
  const char* content_type;
  const char* content_charset;
  nxe_time_t expires_time;
  time_t last_modified;
  uint32_t ref_count;
  struct file_cache_rec* prev;
  struct file_cache_rec* next;
  int error_code;
  _Bool gzip_encoded:1;
  char content[];
} file_cache_rec;

#define file_cache_hash_fn(key) hash_sdbm((const unsigned char*)(key))
#define file_cache_eq_fn(a, b) (!strcmp((a), (b)))

DECLARE_ALIGNHASH(file_cache, const char*, file_cache_rec*, 1, file_cache_hash_fn, file_cache_eq_fn)

static alignhash_t(file_cache) *_file_cache;
static file_cache_rec* _file_cache_head;
static file_cache_rec* _file_cache_tail;
static pthread_mutex_t _file_cache_mutex;

#define IS_LINKED(rec) ((rec)->prev || _file_cache_head==(rec))

static inline void cache_rec_link(file_cache_rec* rec) {
  // add to head
  rec->prev=0;
  rec->next=_file_cache_head;
  if (_file_cache_head) _file_cache_head->prev=rec;
  else _file_cache_tail=rec;
  _file_cache_head=rec;
}

static inline void cache_rec_unlink(file_cache_rec* rec) {
  if (rec->prev) rec->prev->next=rec->next;
  else _file_cache_head=rec->next;
  if (rec->next) rec->next->prev=rec->prev;
  else _file_cache_tail=rec->prev;
  rec->next=0;
  rec->prev=0;
}

static int cache_init() {
  pthread_mutex_init(&_file_cache_mutex, 0);
  _file_cache=alignhash_init(file_cache);
  return 0;
}

static void cache_finalize() {
  ah_iter_t ci;
  for (ci=alignhash_begin(_file_cache); ci!=alignhash_end(_file_cache); ci++) {
    if (alignhash_exist(_file_cache, ci)) {
      file_cache_rec* rec=alignhash_value(_file_cache, ci);
      if (rec->ref_count) nxweb_log_error("file %s still in cache with ref_count=%d", alignhash_key(_file_cache, ci), rec->ref_count);
      cache_rec_unlink(rec);
      nx_free(rec);
    }
  }
  alignhash_destroy(file_cache, _file_cache);
  pthread_mutex_destroy(&_file_cache_mutex);
}

NXWEB_MODULE(sendfile, .on_server_startup=cache_init, .on_server_shutdown=cache_finalize);

static inline void cache_check_size() {
  while (alignhash_size(_file_cache)>MAX_CACHED_ITEMS) {
    file_cache_rec* rec=_file_cache_tail;
    while (rec && rec->ref_count) rec=rec->prev;
    if (rec) {
      const char* fpath=rec->content+rec->content_length+1;
      ah_iter_t ci=alignhash_get(file_cache, _file_cache, fpath);
      if (ci!=alignhash_end(_file_cache)) {
        assert(rec==alignhash_value(_file_cache, ci));
        cache_rec_unlink(rec);
        alignhash_del(file_cache, _file_cache, ci);
        nx_free(rec);
      }
    }
    else {
      break;
    }
  }
}

static void cache_rec_unref(nxd_http_server_proto* hsp, void* req_data) {
  pthread_mutex_lock(&_file_cache_mutex);
  file_cache_rec* rec=req_data;
  assert(rec->ref_count>0);
  if (!--rec->ref_count) {
    nxe_time_t loop_time=_nxweb_net_thread_data->loop->current_time;
    if (loop_time > rec->expires_time || !IS_LINKED(rec)) {
      const char* fpath=rec->content+rec->content_length+1;
      ah_iter_t ci=alignhash_get(file_cache, _file_cache, fpath);
      if (ci!=alignhash_end(_file_cache) && rec==alignhash_value(_file_cache, ci)) {
        alignhash_del(file_cache, _file_cache, ci);
        cache_rec_unlink(rec);
      }
      else {
        nxweb_log_error("freed previously removed cache entry %s [%p]", fpath, rec);
      }
      nx_free(rec);
    }
  }
  cache_check_size();
  pthread_mutex_unlock(&_file_cache_mutex);
}

int nxweb_remove_dots_from_uri_path(char* path) {
  if (!*path) return 0; // end of path
  if (*path!='/') return -1; // invalid path
  while (1) {
    if (path[1]=='.' && path[2]=='.' && (path[3]=='/' || path[3]=='\0')) { // /..(/.*)?$
      memmove(path, path+3, strlen(path+3)+1);
      return 1;
    }
    char* p1=strchr(path+1, '/');
    if (!p1) return 0;
    if (!nxweb_remove_dots_from_uri_path(p1)) return 0;
    memmove(path, p1, strlen(p1)+1);
  }
}

static nxweb_result try_cache(nxweb_http_server_connection* conn, nxweb_http_response* resp, const char* fpath, char* path_info, time_t if_modified_since, _Bool expect_gzip, nxe_time_t loop_time) {
  ah_iter_t ci;
  //nxweb_log_error("trying cache for %s", fpath);
  pthread_mutex_lock(&_file_cache_mutex);
  if ((ci=alignhash_get(file_cache, _file_cache, fpath))!=alignhash_end(_file_cache)) {
    file_cache_rec* rec=alignhash_value(_file_cache, ci);
    if (loop_time <= rec->expires_time) {
      if (rec!=_file_cache_head) {
        cache_rec_unlink(rec);
        cache_rec_link(rec); // relink to head
      }
      switch (rec->error_code) {
        case FC_ERR_NOT_FOUND:
          pthread_mutex_unlock(&_file_cache_mutex);
          return NXWEB_NEXT;
        case FC_ERR_IS_DIR:
          pthread_mutex_unlock(&_file_cache_mutex);
          assert(!expect_gzip);
          strcat(path_info, "/");
          nxweb_send_redirect(resp, 302, path_info);
          nxweb_start_sending_response(conn, resp);
          return NXWEB_OK;
        case FC_ERR_TYPE:
          pthread_mutex_unlock(&_file_cache_mutex);
          nxweb_send_http_error(resp, 403, "Forbidden");
          nxweb_start_sending_response(conn, resp);
          return NXWEB_ERROR;
        case FC_ERR_NO_ERROR:
          if (rec->gzip_encoded == expect_gzip) {
            if (if_modified_since && rec->last_modified<=if_modified_since) {
              pthread_mutex_unlock(&_file_cache_mutex);
              resp->status_code=304;
              resp->status="Not Modified";
              nxweb_start_sending_response(conn, resp);
              return NXWEB_OK;
            }
            rec->ref_count++;
            pthread_mutex_unlock(&_file_cache_mutex);
            resp->content_length=rec->content_length;
            resp->content=rec->content;
            resp->content_type=rec->content_type;
            resp->content_charset=rec->content_charset;
            resp->last_modified=rec->last_modified;
            resp->gzip_encoded=rec->gzip_encoded;
            conn->hsp.req_data=rec;
            conn->hsp.req_finalize=cache_rec_unref;
            nxweb_start_sending_response(conn, resp);
            return NXWEB_OK;
          }
          break;
        default:
          nxweb_log_error("unexpected file cache error_code %d", rec->error_code);
          break;
      }
    }
    alignhash_del(file_cache, _file_cache, ci);
    cache_rec_unlink(rec);
    if (!rec->ref_count) {
      nx_free(rec);
    }
    else {
      nxweb_log_error("removed %s [%p] from cache while its ref_count=%d", fpath, rec, rec->ref_count);
    }
  }
  pthread_mutex_unlock(&_file_cache_mutex);
  return -1;
}

static void cache_store_error(const char* fpath, nxe_time_t loop_time, int error_code) {
  ah_iter_t ci;
  file_cache_rec* rec=nx_calloc(sizeof(file_cache_rec)+strlen(fpath)+1);
  char* ptr=(char*)(rec+1);
  strcpy(ptr, fpath);
  rec->expires_time=loop_time+DEFAULT_CACHED_TIME;
  rec->error_code=error_code;
  int ret=0;
  pthread_mutex_lock(&_file_cache_mutex);
  ci=alignhash_set(file_cache, _file_cache, ptr, &ret);
  if (ci!=alignhash_end(_file_cache) && ret!=AH_INS_ERR) {
    alignhash_value(_file_cache, ci)=rec;
    cache_rec_link(rec);
    //rec->ref_count++;
    cache_check_size();
    pthread_mutex_unlock(&_file_cache_mutex);
    nxweb_log_error("cached %s error %d", ptr, error_code);
    return;
  }
  pthread_mutex_unlock(&_file_cache_mutex);
  nx_free(rec);
}

nxweb_result nxweb_sendfile_try(nxweb_http_server_connection* conn, nxweb_http_response* resp, char* fpath, char* path_info, time_t if_modified_since, nxe_time_t use_cache_time, _Bool cache_error, _Bool try_gzip_encoding, const struct stat* finfo, const nxweb_mime_type* mtype) {
  nxe_time_t loop_time=nxweb_get_loop_time(conn);
  _Bool use_cache=!!use_cache_time;
  int plen=strlen(path_info);
  if (try_gzip_encoding) strcpy(path_info+plen, ".gz");
  if (use_cache && (!finfo || finfo->st_size<=MAX_CACHE_ITEM_SIZE)) {
    // check cache
    int cres=try_cache(conn, resp, fpath, path_info, if_modified_since, try_gzip_encoding, loop_time);
    if (cres==NXWEB_NEXT && try_gzip_encoding) {
      path_info[plen]='\0'; // cut .gz
      try_gzip_encoding=0;
      cres=try_cache(conn, resp, fpath, path_info, if_modified_since, 0, loop_time);
    }
    if (cres!=-1) return cres;
  }

  struct stat _finfo;
  if (!finfo) {
RETRY:
    //nxweb_log_error("trying file %s", fpath);
    if (stat(fpath, &_finfo)==-1) {
      // no such file
      if (cache_error) cache_store_error(fpath, loop_time, FC_ERR_NOT_FOUND);
      if (try_gzip_encoding) {
        path_info[plen]='\0'; // cut .gz
        try_gzip_encoding=0;
        goto RETRY;
      }
      return NXWEB_NEXT;
    }
    finfo=&_finfo;
  }
  if (S_ISDIR(finfo->st_mode) && !try_gzip_encoding) {
    if (cache_error) cache_store_error(fpath, loop_time, FC_ERR_IS_DIR);
    strcat(path_info, "/");
    nxweb_send_redirect(resp, 302, path_info);
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
  if (!S_ISREG(finfo->st_mode)) { // symlink or ...?
    if (cache_error) cache_store_error(fpath, loop_time, FC_ERR_TYPE);
    nxweb_send_http_error(resp, 403, "Forbidden");
    nxweb_start_sending_response(conn, resp);
    return NXWEB_ERROR;
  }

  if (if_modified_since && finfo->st_mtime<=if_modified_since) {
    resp->status_code=304;
    resp->status="Not Modified";
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }

  if (use_cache && finfo->st_size<=MAX_CACHE_ITEM_SIZE && alignhash_size(_file_cache)<MAX_CACHED_ITEMS+16) {
    // cache the file
    file_cache_rec* rec=nx_calloc(sizeof(file_cache_rec)+finfo->st_size+1+strlen(fpath)+1);
    rec->content_length=
    resp->content_length=finfo->st_size;
    resp->gzip_encoded=
    rec->gzip_encoded=try_gzip_encoding;
    char* ptr=((char*)rec)+offsetof(file_cache_rec, content);
    int fd=open(fpath, O_RDONLY);
    if ((fd=open(fpath, O_RDONLY))<0 || read(fd, ptr, finfo->st_size)!=finfo->st_size) {
      if (fd>0) close(fd);
      nx_free(rec);
      nxweb_log_error("sendfile: [%s] stat() was OK, but open/read() failed", fpath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
    close(fd);
    resp->content=ptr;
    ptr+=finfo->st_size;
    *ptr++='\0';
    strcpy(ptr, fpath);
    rec->expires_time=loop_time+use_cache_time;
    rec->last_modified=
    resp->last_modified=finfo->st_mtime;
    if (!mtype) {
      if (try_gzip_encoding) path_info[plen]='\0'; // cut .gz
      mtype=nxweb_get_mime_type_by_ext(path_info);
      if (try_gzip_encoding) path_info[plen]='.'; // restore
    }
    rec->content_type=
    resp->content_type=mtype->mime;
    if (mtype->charset_required) rec->content_charset=resp->content_charset=DEFAULT_CHARSET;
    else rec->content_charset=resp->content_charset=0;

    int ret=0;
    ah_iter_t ci;
    pthread_mutex_lock(&_file_cache_mutex);
    ci=alignhash_set(file_cache, _file_cache, ptr, &ret);
    if (ci!=alignhash_end(_file_cache)) {
      if (ret!=AH_INS_ERR) {
        alignhash_value(_file_cache, ci)=rec;
        cache_rec_link(rec);
        rec->ref_count++;
        cache_check_size();
        pthread_mutex_unlock(&_file_cache_mutex);
        nxweb_log_error("cached %s", ptr);
        conn->hsp.req_data=rec;
        conn->hsp.req_finalize=cache_rec_unref;
        nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
      }
      else { // AH_INS_ERR => key already exists (added by other thread)
        nx_free(rec);
        rec=alignhash_value(_file_cache, ci);
        resp->content_length=rec->content_length;
        resp->content=rec->content;
        resp->content_type=rec->content_type;
        resp->content_charset=rec->content_charset;
        resp->last_modified=rec->last_modified;
        resp->gzip_encoded=rec->gzip_encoded;
        rec->ref_count++;
        pthread_mutex_unlock(&_file_cache_mutex);
        conn->hsp.req_data=rec;
        conn->hsp.req_finalize=cache_rec_unref;
        nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
      }
    }
    pthread_mutex_unlock(&_file_cache_mutex);
    nx_free(rec);
  }

  int result=nxweb_send_file(resp, fpath, finfo, try_gzip_encoding, 0, 0, mtype, DEFAULT_CHARSET);
  if (result!=0) { // should not happen
    nxweb_log_error("sendfile: [%s] stat() was OK, but open() failed", fpath);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    return NXWEB_ERROR;
  }

  nxweb_start_sending_response(conn, resp);
  return NXWEB_OK;
}

static nxweb_result sendfile_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_NEXT; // do not respond to POST requests, etc.

  nxweb_handler* handler=conn->handler;
  const char* document_root=handler->dir;
  assert(document_root);

  char fpath[2048];
  int rlen=strlen(document_root);
  assert(rlen<sizeof(fpath));
  strcpy(fpath, document_root);
  char* path_info=fpath+rlen;
  const char* q=strchr(req->path_info, '?');
  int plen=q? q-req->path_info : strlen(req->path_info);
  if (rlen+plen>sizeof(fpath)-64) { // leave room for index file name etc.
    nxweb_send_http_error(resp, 414, "Request-URI Too Long");
    return NXWEB_ERROR;
  }
  strncat(path_info, req->path_info, plen);
  nxweb_url_decode(path_info, 0);
  plen=strlen(path_info);
  if (plen>0 && path_info[plen-1]=='/') { // directory index
    strcat(path_info+plen, INDEX_FILE);
  }

  if (nxweb_remove_dots_from_uri_path(path_info)) {
    //nxweb_send_http_error(resp, 404, "Not Found");
    return NXWEB_NEXT;
  }

  return nxweb_sendfile_try(conn, resp, fpath, path_info, req->if_modified_since, handler->cache? DEFAULT_CACHED_TIME : 0, 1, req->accept_gzip_encoding, 0, 0);
}

nxweb_handler sendfile_handler={.on_select=sendfile_on_select, .flags=NXWEB_HANDLE_GET};
