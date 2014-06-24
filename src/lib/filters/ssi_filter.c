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
  nxweb_http_request* req;
  nx_simple_map_entry* var_map;
  int parse_start_idx;
} ssi_buffer;

typedef struct ssi_filter_data {
  nxweb_filter_data fdata;
  ssi_buffer ssib;
  int input_fd;
} ssi_filter_data;

#define MAX_SSI_SIZE (20000000)

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

static const char ssib_req_key; // variable's address only matters
#define SSIB_REQ_KEY ((nxe_data)&ssib_req_key)

#define SPACE 32U

#define SSI_MAX_ATTRS 16
#define SSI_MAX_EXPR_LEN 4096

static const char* lookup_var(const ssi_buffer* ssib, const char* var_name, int var_name_len) {
  nx_simple_map_entry* e;
  for (e=nx_simple_map_itr_begin(ssib->var_map); e; e=nx_simple_map_itr_next(e)) {
    if (strlen(e->name)==var_name_len && !strncmp(var_name, e->name, var_name_len)) {
      return e->value;
    }
  }
  // lookup through all parent requests' vars
  nxweb_http_request* req=ssib->req->parent_req;
  while (req) {
    // find first req with SSIB
    const ssi_buffer* parent_ssib=nxweb_get_request_data(req, SSIB_REQ_KEY).cptr;
    if (parent_ssib) return lookup_var(parent_ssib, var_name, var_name_len);
    req=req->parent_req;
  }
  return 0;
}

static char* expand_var(ssi_buffer* ssib, const char* var_name, int var_name_len, char* dst, int max_dst_len) {
  if (var_name_len==12 && !strncmp(var_name, "QUERY_STRING", var_name_len)) {
    char* query_string=strchr(ssib->req->uri, '?');
    if (query_string) {
      query_string++;
      int len=strlen(query_string);
      if (len>max_dst_len) return 0;
      memcpy(dst, query_string, len);
      return dst+len;
    }
    else {
      return dst;
    }
  }
  else if ((var_name_len==12 && !strncmp(var_name, "DOCUMENT_URI", var_name_len))
        || (var_name_len==11 && !strncmp(var_name, "REQUEST_URI", var_name_len))) {
    const char* request_uri=ssib->req->uri;
    char* query_string=strchr(request_uri, '?');
    int len=query_string? (query_string-request_uri) : strlen(request_uri);
    if (len>max_dst_len) return 0;
    memcpy(dst, request_uri, len);
    return dst+len;
  }
  else {
    const char* value=lookup_var(ssib, var_name, var_name_len);
    if (value) {
      int len=strlen(value);
      if (len>max_dst_len) return 0;
      memcpy(dst, value, len);
      return dst+len;
    }
    nxweb_log_warning("unknown ssi variable: %.*s ref: %s", var_name_len, var_name, ssib->req->uri);
    return dst;
  }
}

#define exp_append(ptr, len) { if (d+(len)>de) return 0; if (len) { memcpy(d, ptr, len); d+=len; } }
#define exp_append_char(c) { if (d+1>de) return 0; *d++=c; }
#define exp_append_var(var_name, var_name_len) { char* r=expand_var(ssib, var_name, var_name_len, d, de-d); if (!r) return 0; d=r; }

static const char* expand_vars(ssi_buffer* ssib, const char* expr) {
  char expanded[SSI_MAX_EXPR_LEN];
  char *d=expanded, *de=d+SSI_MAX_EXPR_LEN-1;

  const char* start=expr;
  const char *p, *pp;
  while ((p=strchr(start, '$'))) {
    if (p>start && *(p-1)=='\\') {
      // escaped \$
      exp_append(start, p-start-1);
      exp_append_char('$');
      start=p+1;
      continue;
    }
    exp_append(start, p-start);
    p++;
    if (*p=='{') {
      pp=strchr(p+1, '}');
      if (!pp) return 0;
      exp_append_var(p+1, pp-(p+1));
      start=pp+1;
    }
    else {
      for (pp=p; (*pp>='a' && *pp<='z')||(*pp>='A' && *pp<='Z')||(*pp>='0' && *pp<='9')||*pp=='_'; pp++) ;
      exp_append_var(p, pp-p);
      start=pp;
    }
  }

  if (d==expanded) return expr; // unchanged
  else {
    exp_append(start, strlen(start));
    *d++='\0';
    return nxb_copy_obj(ssib->nxb, expanded, d-expanded);
  }
}

static int parse_directive(ssi_buffer* ssib, char* str, int len) {
  assert(!str[len]); // zero-terminated
  char* p=str;
  char* pe=str+len;
  char* attr_name[SSI_MAX_ATTRS];
  char* attr_value[SSI_MAX_ATTRS];
  int num_attr=0;
  while ((unsigned char)*p<=SPACE && p<pe) p++;
  if (p>=pe) return -1;
  char* cmd=p;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';
  while (num_attr<SSI_MAX_ATTRS) {
    attr_name[num_attr]=p;
    p=strchr(p, '=');
    if (!p) break;
    *p++='\0';
    attr_name[num_attr]=nxweb_trunc_space(attr_name[num_attr]);
    while ((unsigned char)*p<=SPACE && p<pe) p++;
    if (p>=pe) return -1;
    char q=*p++;
    if (q!='\"' && q!='\'') return -1;
    attr_value[num_attr]=p;
    p=strchr(p, q);
    if (!p) return -1;
    *p++='\0';
    num_attr++;
  }

  if (!strcmp(cmd, "include")) {
    int i;
    for (i=0; i<num_attr; i++) {
      if (!strcmp(attr_name[i], "virtual")) {
        const char* expanded=expand_vars(ssib, attr_value[i]);
        if (!expanded) {
          nxweb_log_warning("ssi variables expansion failure: %s @ %s", attr_value[i], ssib->req->uri);
          return -1;
        }
        // nxweb_log_error("ssi variables expanded: %s -> %s", attr_value[i], expanded);
        nxweb_composite_stream_append_subrequest(ssib->cs, ssib->cs->req->host, expanded);
        return 0;
      }
    }
  }
  else if (!strcmp(cmd, "set")) {
    int i;
    const char* var_name=0;
    const char* var_value=0;
    nx_simple_map_entry* param;
    for (i=0; i<num_attr; i++) {
      if (!strcmp(attr_name[i], "var")) {
        var_name=attr_value[i];
      }
      else if (!strcmp(attr_name[i], "value")) {
        var_value=expand_vars(ssib, attr_value[i]);
        if (!var_value) {
          nxweb_log_warning("ssi set variable expansion failure: %s @ %s", attr_value[i], ssib->req->uri);
          return -1;
        }
        // nxweb_log_error("ssi variables expanded: %s -> %s", attr_value[i], var_value);
      }
    }
    if (var_name && var_value) {
      param=nx_simple_map_find(ssib->var_map, var_name);
      if (!param) {
        param=nxb_calloc_obj(ssib->nxb, sizeof(nx_simple_map_entry));
        ssib->var_map=nx_simple_map_add(ssib->var_map, param);
        param->name=var_name;
      }
      param->value=var_value;
      return 0;
    }
  }
  else if (!strcmp(cmd, "echo")) {
    int i;
    for (i=0; i<num_attr; i++) {
      if (!strcmp(attr_name[i], "var")) {
        const char* var_name=attr_value[i];
        char expanded[SSI_MAX_EXPR_LEN];
        char* end=expand_var(ssib, var_name, strlen(var_name), expanded, SSI_MAX_EXPR_LEN);
        if (!end) {
          // nxweb_log_warning("ssi echo variable expansion failure: %s @ %s", var_name, ssib->req->uri);
          return -1;
        }
        int len=end-expanded;
        char* buf=nxb_copy_obj(ssib->nxb, expanded, len);
        nxweb_composite_stream_append_bytes(ssib->cs, buf, len);
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

static nxe_ssize_t ssi_buffer_data_in_write(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* _flags) {
  ssi_buffer* ssib=OBJ_PTR_FROM_FLD_PTR(ssi_buffer, data_in, os);
  //nxe_loop* loop=os->super.loop;

  nxweb_log_debug("ssi_buffer_data_in_write");

  nxe_flags_t flags=*_flags;
  if (ssib->overflow) { // reached max_ssi_size
    // swallow input data
  }
  else {
    int wsize=size;
    if (ssib->data_size+wsize > MAX_SSI_SIZE) wsize=MAX_SSI_SIZE-ssib->data_size;
    assert(wsize>=0);
    if (wsize>0) {
      nx_file_reader_to_mem_ptr(fd, fr, &ptr, &size, &flags);
      if (((int)size)<wsize) wsize=size;
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
  if (flags&NXEF_EOF) {
    nxe_ostream_unset_ready(os);

    int nbytes;
    char* ptr=nxb_finish_stream(ssib->nxb, &nbytes);
    if (nbytes) {
      nxweb_composite_stream_append_bytes(ssib->cs, ptr, nbytes);
    }
    nxweb_composite_stream_close(ssib->cs);
  }
  return size;
}

static const nxe_ostream_class ssi_buffer_data_in_class={.write=ssi_buffer_data_in_write};

void ssi_buffer_init(ssi_buffer* ssib, nxweb_http_server_connection* conn, nxweb_http_request* req) {
  memset(ssib, 0, sizeof(ssi_buffer));
  ssib->req=req;
  ssib->nxb=nxp_alloc(conn->hsp.nxb_pool); // allocate separate nxb to not interfere with other filters
  nxb_init(ssib->nxb, NXWEB_CONN_NXB_SIZE);
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


static nxweb_filter_data* ssi_init(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(ssi_filter_data));
  return fdata;
}

static void ssi_finalize(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  ssi_filter_data* sfdata=(ssi_filter_data*)fdata;
  if (sfdata->ssib.data_in.pair) nxe_disconnect_streams(sfdata->ssib.data_in.pair, &sfdata->ssib.data_in);
  if (sfdata->input_fd) {
    close(sfdata->input_fd);
    sfdata->input_fd=0;
  }
  if (sfdata->ssib.nxb) {
    nxb_empty(sfdata->ssib.nxb);
    nxp_free(conn->hsp.nxb_pool, sfdata->ssib.nxb);
  }
}

static nxweb_result ssi_translate_cache_key(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  // ssi result does not depend on request options other than basic ones
  // fdata->cache_key=key; // just store it
  return NXWEB_OK;
}

static nxweb_result ssi_do_filter(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  ssi_filter_data* sfdata=(ssi_filter_data*)fdata;
  if (resp->status_code && resp->status_code!=200 && resp->status_code!=404) return NXWEB_OK;
  if (!resp->content_length) return NXWEB_OK;

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
  ssi_buffer_init(&sfdata->ssib, conn, req);
  if (resp->content_length>0) ssi_buffer_make_room(&sfdata->ssib, min(MAX_SSI_SIZE, resp->content_length));
  nxe_connect_streams(conn->tdata->loop, resp->content_out, &sfdata->ssib.data_in);

  nxweb_set_request_data(req, SSIB_REQ_KEY, (nxe_data)(void*)&sfdata->ssib, 0); // will be used for variable lookups in parent requests

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

nxweb_filter ssi_filter={.init=ssi_init, .finalize=ssi_finalize,
        .do_filter=ssi_do_filter};

NXWEB_DEFINE_FILTER(ssi, ssi_filter);
