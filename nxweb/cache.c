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

#include "nxweb.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/fcntl.h>

#include "../deps/ulib/alignhash_tpl.h"
#include "../deps/ulib/hash.h"

typedef struct nxweb_cache_rec {
  nxe_ssize_t content_length;
  const char* content_type;
  const char* content_charset;
  nxe_time_t expires_time;
  time_t last_modified;
  uint32_t ref_count;
  struct nxweb_cache_rec* prev;
  struct nxweb_cache_rec* next;
  _Bool gzip_encoded:1;
  char content[];
} nxweb_cache_rec;

#define nxweb_cache_hash_fn(key) hash_sdbm((const unsigned char*)(key))
#define nxweb_cache_eq_fn(a, b) (!strcmp((a), (b)))

DECLARE_ALIGNHASH(nxweb_cache, const char*, nxweb_cache_rec*, 1, nxweb_cache_hash_fn, nxweb_cache_eq_fn)

static alignhash_t(nxweb_cache) *_nxweb_cache;
static nxweb_cache_rec* _nxweb_cache_head;
static nxweb_cache_rec* _nxweb_cache_tail;
static pthread_mutex_t _nxweb_cache_mutex;

#define IS_LINKED(rec) ((rec)->prev || _nxweb_cache_head==(rec))

static inline void cache_rec_link(nxweb_cache_rec* rec) {
  // add to head
  rec->prev=0;
  rec->next=_nxweb_cache_head;
  if (_nxweb_cache_head) _nxweb_cache_head->prev=rec;
  else _nxweb_cache_tail=rec;
  _nxweb_cache_head=rec;
}

static inline void cache_rec_unlink(nxweb_cache_rec* rec) {
  if (rec->prev) rec->prev->next=rec->next;
  else _nxweb_cache_head=rec->next;
  if (rec->next) rec->next->prev=rec->prev;
  else _nxweb_cache_tail=rec->prev;
  rec->next=0;
  rec->prev=0;
}

static int cache_init() {
  pthread_mutex_init(&_nxweb_cache_mutex, 0);
  _nxweb_cache=alignhash_init(nxweb_cache);
  return 0;
}

static void cache_finalize() {
  ah_iter_t ci;
  for (ci=alignhash_begin(_nxweb_cache); ci!=alignhash_end(_nxweb_cache); ci++) {
    if (alignhash_exist(_nxweb_cache, ci)) {
      nxweb_cache_rec* rec=alignhash_value(_nxweb_cache, ci);
      if (rec->ref_count) nxweb_log_error("file %s still in cache with ref_count=%d", alignhash_key(_nxweb_cache, ci), rec->ref_count);
      cache_rec_unlink(rec);
      nx_free(rec);
    }
  }
  alignhash_destroy(nxweb_cache, _nxweb_cache);
  pthread_mutex_destroy(&_nxweb_cache_mutex);
}

NXWEB_MODULE(cache, .on_server_startup=cache_init, .on_server_shutdown=cache_finalize);

static inline void cache_check_size() {
  while (alignhash_size(_nxweb_cache)>NXWEB_MAX_CACHED_ITEMS) {
    nxweb_cache_rec* rec=_nxweb_cache_tail;
    while (rec && rec->ref_count) rec=rec->prev;
    if (rec) {
      const char* fpath=rec->content+rec->content_length+1;
      ah_iter_t ci=alignhash_get(nxweb_cache, _nxweb_cache, fpath);
      if (ci!=alignhash_end(_nxweb_cache)) {
        assert(rec==alignhash_value(_nxweb_cache, ci));
        cache_rec_unlink(rec);
        alignhash_del(nxweb_cache, _nxweb_cache, ci);
        nx_free(rec);
      }
    }
    else {
      break;
    }
  }
}

static void cache_rec_unref(nxd_http_server_proto* hsp, void* req_data) {
  pthread_mutex_lock(&_nxweb_cache_mutex);
  nxweb_cache_rec* rec=req_data;
  assert(rec->ref_count>0);
  if (!--rec->ref_count) {
    nxe_time_t loop_time=_nxweb_net_thread_data->loop->current_time;
    if (loop_time > rec->expires_time || !IS_LINKED(rec)) {
      const char* fpath=rec->content+rec->content_length+1;
      ah_iter_t ci=alignhash_get(nxweb_cache, _nxweb_cache, fpath);
      if (ci!=alignhash_end(_nxweb_cache) && rec==alignhash_value(_nxweb_cache, ci)) {
        alignhash_del(nxweb_cache, _nxweb_cache, ci);
        cache_rec_unlink(rec);
      }
      else {
        nxweb_log_error("freed previously removed cache entry %s [%p]", fpath, rec);
      }
      nx_free(rec);
    }
  }
  cache_check_size();
  pthread_mutex_unlock(&_nxweb_cache_mutex);
}

nxweb_result nxweb_cache_try(nxweb_http_server_connection* conn, nxweb_http_response* resp, const char* key, time_t if_modified_since, time_t revalidated_mtime) {
  nxe_time_t loop_time=nxweb_get_loop_time(conn);
  ah_iter_t ci;
  //nxweb_log_error("trying cache for %s", fpath);
  pthread_mutex_lock(&_nxweb_cache_mutex);
  if ((ci=alignhash_get(nxweb_cache, _nxweb_cache, key))!=alignhash_end(_nxweb_cache)) {
    nxweb_cache_rec* rec=alignhash_value(_nxweb_cache, ci);
    if (rec->last_modified==revalidated_mtime) {
      rec->expires_time=loop_time+NXWEB_DEFAULT_CACHED_TIME;
      nxweb_log_error("revalidated %s in cache", key);
    }
    if (loop_time <= rec->expires_time) {
      if (rec!=_nxweb_cache_head) {
        cache_rec_unlink(rec);
        cache_rec_link(rec); // relink to head
      }
      if (if_modified_since && rec->last_modified<=if_modified_since) {
        pthread_mutex_unlock(&_nxweb_cache_mutex);
        resp->status_code=304;
        resp->status="Not Modified";
        return NXWEB_OK;
      }
      rec->ref_count++; // this must be within mutex-protected section
      pthread_mutex_unlock(&_nxweb_cache_mutex);
      resp->content_length=rec->content_length;
      resp->content=rec->content;
      resp->content_type=rec->content_type;
      resp->content_charset=rec->content_charset;
      resp->last_modified=rec->last_modified;
      resp->gzip_encoded=rec->gzip_encoded;
      conn->hsp.req_data=rec;
      conn->hsp.req_finalize=cache_rec_unref;
      return NXWEB_OK;
    }
    else if (!revalidated_mtime) {
      pthread_mutex_unlock(&_nxweb_cache_mutex);
      return NXWEB_REVALIDATE;
    }
    alignhash_del(nxweb_cache, _nxweb_cache, ci);
    cache_rec_unlink(rec);
    if (!rec->ref_count) {
      nx_free(rec);
    }
    else {
      // this is normal; just notification
      nxweb_log_error("removed %s [%p] from cache while its ref_count=%d", key, rec, rec->ref_count);
    }
  }
  pthread_mutex_unlock(&_nxweb_cache_mutex);
  return NXWEB_MISS;
}

nxweb_result nxweb_cache_store_response(nxweb_http_server_connection* conn, nxweb_http_response* resp) {
  nxe_time_t loop_time=nxweb_get_loop_time(conn);
  if (!resp->status_code) resp->status_code=200;
  const char* fpath=resp->sendfile_path;

  if (resp->status_code==200 && fpath && resp->content_length<=NXWEB_MAX_CACHED_ITEM_SIZE && alignhash_size(_nxweb_cache)<NXWEB_MAX_CACHED_ITEMS+16) {

    if (nxweb_cache_try(conn, resp, fpath, 0, resp->last_modified)!=NXWEB_MISS) return NXWEB_OK;

    nxweb_cache_rec* rec=nx_calloc(sizeof(nxweb_cache_rec)+resp->content_length+1+strlen(fpath)+1);

    rec->expires_time=loop_time+NXWEB_DEFAULT_CACHED_TIME;
    rec->last_modified=resp->last_modified;
    rec->content_type=resp->content_type;
    rec->content_charset=resp->content_charset;
    rec->content_length=resp->content_length;
    rec->gzip_encoded=resp->gzip_encoded;
    char* ptr=((char*)rec)+offsetof(nxweb_cache_rec, content);
    int fd=open(fpath, O_RDONLY);
    if ((fd=open(fpath, O_RDONLY))<0 || read(fd, ptr, resp->content_length)!=resp->content_length) {
      if (fd>0) close(fd);
      nx_free(rec);
      nxweb_log_error("nxweb_cache_file_store_and_send(): [%s] stat() was OK, but open/read() failed", fpath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
    close(fd);
    resp->content=ptr;
    ptr+=resp->content_length;
    *ptr++='\0';
    strcpy(ptr, fpath);

    int ret=0;
    ah_iter_t ci;
    pthread_mutex_lock(&_nxweb_cache_mutex);
    ci=alignhash_set(nxweb_cache, _nxweb_cache, ptr, &ret);
    if (ci!=alignhash_end(_nxweb_cache)) {
      if (ret!=AH_INS_ERR) {
        alignhash_value(_nxweb_cache, ci)=rec;
        cache_rec_link(rec);
        rec->ref_count++;
        cache_check_size();
        pthread_mutex_unlock(&_nxweb_cache_mutex);
        nxweb_log_error("cached %s", ptr);
        conn->hsp.req_data=rec;
        assert(!conn->hsp.req_finalize);
        conn->hsp.req_finalize=cache_rec_unref;
        //nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
      }
      else { // AH_INS_ERR => key already exists (added by other thread)
        nx_free(rec);
        rec=alignhash_value(_nxweb_cache, ci);
        resp->content_length=rec->content_length;
        resp->content=rec->content;
        resp->content_type=rec->content_type;
        resp->content_charset=rec->content_charset;
        resp->last_modified=rec->last_modified;
        resp->gzip_encoded=rec->gzip_encoded;
        rec->ref_count++;
        pthread_mutex_unlock(&_nxweb_cache_mutex);
        conn->hsp.req_data=rec;
        assert(!conn->hsp.req_finalize);
        conn->hsp.req_finalize=cache_rec_unref;
        //nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
      }
    }
    pthread_mutex_unlock(&_nxweb_cache_mutex);
    nx_free(rec);
  }
  return NXWEB_OK;
}