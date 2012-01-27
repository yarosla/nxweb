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

#include "../deps/ulib/alignhash_tpl.h"
#include "../deps/ulib/hash.h"

#define INDEX_FILE "index.htm"
#define DEFAULT_CHARSET "utf-8"
#define MAX_CACHED_TIME 30000000
#define MAX_CACHED_ITEMS 500
#define MAX_CACHE_ITEM_SIZE 32768

typedef struct file_cache_rec {
  nxe_ssize_t content_length;
  const char* content_type;
  const char* content_charset;
  nxe_time_t cached_time;
  time_t last_modified;
  uint32_t ref_count;
  struct file_cache_rec* prev;
  struct file_cache_rec* next;
  char content[];
} file_cache_rec;

#define file_cache_hash_fn(key) hash_sdbm((const unsigned char*)(key))
#define file_cache_eq_fn(a, b) (!strcmp((a), (b)))

DECLARE_ALIGNHASH(file_cache, const char*, file_cache_rec*, 1, file_cache_hash_fn, file_cache_eq_fn)

static alignhash_t(file_cache) *_file_cache;
static file_cache_rec* _file_cache_head;
static file_cache_rec* _file_cache_tail;
static pthread_mutex_t _file_cache_mutex;

static void cache_rec_link(file_cache_rec* rec) {
  // add to head
  rec->prev=0;
  rec->next=_file_cache_head;
  if (_file_cache_head) _file_cache_head->prev=rec;
  else _file_cache_tail=rec;
  _file_cache_head=rec;
}

static void cache_rec_unlink(file_cache_rec* rec) {
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
  if (!--rec->ref_count) {
    nxe_time_t loop_time=_nxweb_net_thread_data->loop->current_time;
    if (loop_time - rec->cached_time > MAX_CACHED_TIME) {
      const char* fpath=rec->content+rec->content_length+1;
      ah_iter_t ci=alignhash_get(file_cache, _file_cache, fpath);
      if (ci!=alignhash_end(_file_cache)) {
        assert(rec==alignhash_value(_file_cache, ci));
        cache_rec_unlink(rec);
        alignhash_del(file_cache, _file_cache, ci);
        nx_free(rec);
      }
    }
  }
  cache_check_size();
  pthread_mutex_unlock(&_file_cache_mutex);
}

static int remove_dots_from_uri_path(char* path) {
  if (!*path) return 0; // end of path
  if (*path!='/') return -1; // invalid path
  while (1) {
    if (path[1]=='.' && path[2]=='.' && (path[3]=='/' || path[3]=='\0')) { // /..(/.*)?$
      memmove(path, path+3, strlen(path+3)+1);
      return 1;
    }
    char* p1=strchr(path+1, '/');
    if (!p1) return 0;
    if (!remove_dots_from_uri_path(p1)) return 0;
    memmove(path, p1, strlen(p1)+1);
  }
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
  if (rlen+plen>sizeof(fpath)-21) { // leave room for index file name
    nxweb_send_http_error(resp, 414, "Request-URI Too Long");
    return NXWEB_ERROR;
  }
  strncat(path_info, req->path_info, plen);
  nxweb_url_decode(path_info, 0);
  plen=strlen(path_info);
  if (plen>0 && path_info[plen-1]=='/') { // directory index
    strcat(path_info+plen, INDEX_FILE);
  }

  if (remove_dots_from_uri_path(path_info)) {
    nxweb_send_http_error(resp, 404, "Not Found");
    return NXWEB_ERROR;
  }

  nxe_time_t loop_time=nxweb_get_loop_time(conn);
  ah_iter_t ci;
  if (handler->cache) {
    // check cache
    pthread_mutex_lock(&_file_cache_mutex);
    if ((ci=alignhash_get(file_cache, _file_cache, fpath))!=alignhash_end(_file_cache)) {
      file_cache_rec* rec=alignhash_value(_file_cache, ci);
      if (loop_time - rec->cached_time <= MAX_CACHED_TIME || rec->ref_count) {
        if (rec!=_file_cache_head) {
          cache_rec_unlink(rec);
          cache_rec_link(rec); // relink to head
        }
        resp->content_length=rec->content_length;
        resp->content=rec->content;
        resp->content_type=rec->content_type;
        resp->content_charset=rec->content_charset;
        resp->last_modified=rec->last_modified;
        rec->ref_count++;
        pthread_mutex_unlock(&_file_cache_mutex);
        conn->hsp.req_data=rec;
        conn->hsp.req_finalize=cache_rec_unref;
        nxweb_start_sending_response(conn, resp);
        return NXWEB_OK;
      }
      else {
        cache_rec_unlink(rec);
        alignhash_del(file_cache, _file_cache, ci);
        nx_free(rec);
      }
    }
    pthread_mutex_unlock(&_file_cache_mutex);
  }

  struct stat finfo;
  if (stat(fpath, &finfo)==-1) {
    // no such file
    //nxweb_send_http_error(resp, 404, "Not Found");
    return NXWEB_NEXT;
  }
  if (S_ISDIR(finfo.st_mode)) {
    strcat(path_info, "/");
    nxweb_send_redirect(resp, 302, path_info);
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
  if (!S_ISREG(finfo.st_mode)) { // symlink or ...?
    nxweb_send_http_error(resp, 403, "Forbidden");
    return NXWEB_ERROR;
  }

  if (handler->cache && finfo.st_size<=MAX_CACHE_ITEM_SIZE && alignhash_size(_file_cache)<MAX_CACHED_ITEMS+16) {
    // cache the file
    file_cache_rec* rec=nx_calloc(sizeof(file_cache_rec)+finfo.st_size+1+strlen(fpath)+1);
    rec->content_length=
    resp->content_length=finfo.st_size;
    char* path_info=((char*)rec)+offsetof(file_cache_rec, content);
    int fd=open(fpath, O_RDONLY);
    if ((fd=open(fpath, O_RDONLY))<0 || read(fd, path_info, finfo.st_size)<finfo.st_size) {
      if (fd>0) close(fd);
      nx_free(rec);
      nxweb_log_error("sendfile: [%s] stat() was OK, but open/read() failed", fpath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
    close(fd);
    resp->content=path_info;
    path_info+=finfo.st_size;
    *path_info++='\0';
    strcpy(path_info, fpath);
    rec->cached_time=loop_time;
    rec->last_modified=
    resp->last_modified=finfo.st_mtime;
    const nxweb_mime_type* mtype=nxweb_get_mime_type_by_ext(fpath);
    rec->content_type=
    resp->content_type=mtype->mime;
    if (mtype->charset_required) rec->content_charset=resp->content_charset=DEFAULT_CHARSET;
    else rec->content_charset=resp->content_charset=0;

    int ret=0;
    pthread_mutex_lock(&_file_cache_mutex);
    ci=alignhash_set(file_cache, _file_cache, path_info, &ret);
    if (ci!=alignhash_end(_file_cache)) {
      if (ret!=AH_INS_ERR) {
        alignhash_value(_file_cache, ci)=rec;
        cache_rec_link(rec);
        rec->ref_count++;
        cache_check_size();
        pthread_mutex_unlock(&_file_cache_mutex);
        nxweb_log_error("cached %s", path_info);
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

  int result=nxweb_send_file(resp, fpath, &finfo, 0, 0, DEFAULT_CHARSET);
  if (result!=0) { // should not happen
    nxweb_log_error("sendfile: [%s] stat() was OK, but open() failed", fpath);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    return NXWEB_ERROR;
  }

  nxweb_start_sending_response(conn, resp);
  return NXWEB_OK;
}

nxweb_handler sendfile_handler={.on_select=sendfile_on_select, .flags=NXWEB_HANDLE_GET};
