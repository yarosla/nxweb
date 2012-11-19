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

typedef struct ssi_buffer {
  nxb_buffer* nxb;
  nxe_ostream data_in;
  _Bool overflow:1;
  char* data_ptr;
  int data_size;
  nxweb_composite_stream* cs;
  int parse_start_idx;
} ssi_buffer;

typedef struct ssi_filter_data {
  nxweb_filter_data fdata;
  ssi_buffer ssib;
  int input_fd;
} ssi_filter_data;

#define MAX_SSI_SIZE (1000000)

#ifndef max
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#endif

#ifndef min
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
#endif

#define SPACE 32U


static int parse_directive(ssi_buffer* ssib, char* str, int len) {
  assert(!str[len]); // zero-terminated
  char* p=str;
  char* pe=str+len;
  while ((unsigned char)*p<=SPACE && p<pe) p++;
  if (p>=pe) return -1;
  char* cmd=p;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';
  if (!strncmp(cmd, "include", 7)) {
    for (;;) {
      while ((unsigned char)*p<=SPACE && p<pe) p++;
      if (p>=pe) return -1;
      char* attr=p;
      p=strchr(p, '=');
      if (!p) return -1;
      *p++='\0';
      while ((unsigned char)*p<=SPACE && p<pe) p++;
      if (p>=pe) return -1;
      char q=*p++;
      if (q!='\"' && q!='\'') return -1;
      char* value=p;
      p=strchr(p, q);
      if (!p) return -1;
      *p++='\0';
      if (!strncmp(attr, "virtual", 7)) {
        nxweb_composite_stream_append_subrequest(ssib->cs, ssib->cs->req->host, value);
        return 0;
      }
    }
  }
  return -1;
}

static int parse_text(ssi_buffer* ssib) {
  // find all includes & start subrequests
  int size;
  char* t=nxb_get_unfinished(ssib->nxb, &size);
  char* p;
  char* p1=t;
  char* p2;
  char* p3;
  int size1=size;
  for (;size1>0;) {
    p=memchr(p1, '<', size1);
    if (!p || (p-p1)+5>size1) break;
    if (p[1]=='!' && p[2]=='-' && p[3]=='-' && p[4]=='#') {
      p3=p+5;
      R: p2=memchr(p3, '>', size-(p3-t));
      if (!p2) break;
      if (*(p2-1)!='-' || *(p2-2)!='-') {
        p3=p2+1;
        if ((p3-t)>size) break;
        goto R;
      }
      // ssi directive found!
      nxb_finish_partial(ssib->nxb, (p2-t)+1); // text + directive
      if (p-t) {
        nxweb_composite_stream_append_bytes(ssib->cs, t, p-t);
      }
      *(p2-2)='\0';
      if (parse_directive(ssib, p+5, (p2-p)-5-2)==-1) {
        // ssi syntax error
        nxweb_composite_stream_append_bytes(ssib->cs, "<!--[ssi syntax error]-->", sizeof("<!--[ssi syntax error]-->")-1);
      }
      return 1;
    }
    else {
      p1=p+1;
      size1=size-(p1-t);
    }
  }
  return 0;
}

static nxe_ssize_t ssi_buffer_data_in_write(nxe_ostream* os, nxe_istream* is, int fd, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  ssi_buffer* ssib=OBJ_PTR_FROM_FLD_PTR(ssi_buffer, data_in, os);
  //nxe_loop* loop=os->super.loop;
  if (ssib->overflow) { // reached max_ssi_size
    // swallow input data
  }
  else {
    int wsize=size;
    if (ssib->data_size+wsize > MAX_SSI_SIZE) wsize=MAX_SSI_SIZE-ssib->data_size;
    assert(wsize>=0);
    if (wsize>0) {
      nxb_make_room(ssib->nxb, wsize);
      char* dptr=nxb_get_room(ssib->nxb, 0);
      memcpy(dptr, ptr.cptr, wsize);
      nxb_blank_fast(ssib->nxb, wsize);
      ssib->data_size+=wsize;
      while (parse_text(ssib));
      if (ssib->data_size >= MAX_SSI_SIZE) {
        ssib->overflow=1;
      }
    }
  }
  if (*flags&NXEF_EOF) {
    nxe_ostream_unset_ready(os);

    int size;
    char* ptr=nxb_finish_stream(ssib->nxb, &size);
    if (size) {
      nxweb_composite_stream_append_bytes(ssib->cs, ptr, size);
    }
    nxweb_composite_stream_close(ssib->cs);
  }
  return size;
}

static const nxe_ostream_class ssi_buffer_data_in_class={.write=ssi_buffer_data_in_write};

void ssi_buffer_init(ssi_buffer* ssib, nxb_buffer* nxb) {
  memset(ssib, 0, sizeof(ssi_buffer));
  ssib->nxb=nxb;
  ssib->data_in.super.cls.os_cls=&ssi_buffer_data_in_class;
  ssib->data_in.ready=1;
}

void ssi_buffer_make_room(ssi_buffer* ssib, int data_size) {
  assert(ssib->data_size+data_size <= MAX_SSI_SIZE);
  nxb_make_room(ssib->nxb, data_size);
}

char* ssi_buffer_get_result(ssi_buffer* ssib, int* size) {
  if (!ssib->data_ptr) {
    nxb_append_char(ssib->nxb, '\0');
    ssib->data_ptr=nxb_finish_stream(ssib->nxb, 0);
  }
  if (size) *size=ssib->data_size;
  return ssib->data_ptr;
}


static nxweb_filter_data* ssi_init(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(ssi_filter_data));
  return fdata;
}

static void ssi_finalize(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  ssi_filter_data* sfdata=(ssi_filter_data*)fdata;
  if (sfdata->ssib.data_in.pair) nxe_disconnect_streams(sfdata->ssib.data_in.pair, &sfdata->ssib.data_in);
  if (sfdata->input_fd) {
    close(sfdata->input_fd);
    sfdata->input_fd=0;
  }
}

static nxweb_result ssi_translate_cache_key(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  if (!resp->ssi_on) {
    if (*resp->cache_key!=' ') { // response originally comes from file
      if (!resp->mtype && *key!=' ') {
        resp->mtype=nxweb_get_mime_type_by_ext(key); // always returns not null
      }
      if (!resp->mtype && resp->content_type) {
        resp->mtype=nxweb_get_mime_type(resp->content_type);
      }
    }
    if (resp->mtype && !resp->mtype->ssi_on) {
      fdata->bypass=1;
      return NXWEB_NEXT;
    }
  }

  // transform to virtual key ( )
  int plen=strlen(key)-root_len;
  assert(plen>=0);
  int rlen=sizeof(" /_ssi_")-1;
  char* fc_key=nxb_alloc_obj(req->nxb, rlen+plen+1);
  memcpy(fc_key, " /_ssi_", rlen);
  strcpy(fc_key+rlen, key+root_len);
  fdata->cache_key=fc_key;
  fdata->cache_key_root_len=1;
  return NXWEB_OK;
}

static nxweb_result ssi_do_filter(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  ssi_filter_data* sfdata=(ssi_filter_data*)fdata;
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;

  if (resp->gzip_encoded) {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }

  if (!resp->ssi_on) {
    if (!resp->mtype && resp->content_type) {
      resp->mtype=nxweb_get_mime_type(resp->content_type);
    }
    if (!resp->mtype || !resp->mtype->ssi_on) {
      fdata->bypass=1;
      return NXWEB_NEXT;
    }
  }

  nxd_http_server_proto_setup_content_out(&conn->hsp, resp);

  // attach content_out to ssi_buffer
  ssi_buffer_init(&sfdata->ssib, req->nxb);
  if (resp->content_length>0) ssi_buffer_make_room(&sfdata->ssib, min(MAX_SSI_SIZE, resp->content_length));
  nxe_connect_streams(conn->tdata->loop, resp->content_out, &sfdata->ssib.data_in);

  // replace content_out with composite stream
  nxweb_composite_stream* cs=nxweb_composite_stream_init(conn, req);

  nxweb_composite_stream_start(cs, resp);

  // reset previous response content
  resp->content=0;
  resp->sendfile_path=0;
  if (resp->sendfile_fd) {
    // save it to close on finalize
    sfdata->input_fd=resp->sendfile_fd;
    resp->sendfile_fd=0;
  }
  resp->last_modified=0;

  sfdata->ssib.cs=cs;

  return NXWEB_OK;
}

nxweb_filter ssi_filter={.name="ssi", .init=ssi_init, .finalize=ssi_finalize,
        .translate_cache_key=ssi_translate_cache_key, .do_filter=ssi_do_filter};
