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

typedef struct tf_buffer {
  nxb_buffer* nxb;
  nxe_ostream data_in;
  _Bool overflow:1;
  char* data_ptr;
  int data_size;

  struct tf_filter_data* tfdata;
  nxt_file* file;
  nxt_block* blk;
} tf_buffer;

typedef struct tf_filter_data {
  nxweb_filter_data fdata;
  tf_buffer tfb;
  nxweb_composite_stream* cs;
  int input_fd;
  nxt_context ctx;
  nxweb_http_server_connection* conn;
} tf_filter_data;

#define MAX_TEMPLATE_SIZE (1000000)

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

static const char tfb_key; // variable's address only matters
#define TFB_KEY ((nxe_data)&tfb_key)

static void tf_check_complete(tf_filter_data* tfdata) {
  nxt_context* ctx=&tfdata->ctx;
  if (nxt_is_complete(ctx)) {
    // merge
    nxt_merge(ctx);
    // serialize
    nxt_serialize_to_cs(ctx, tfdata->cs);
    nxweb_composite_stream_close(tfdata->cs);
  }
}

static nxe_ssize_t tf_buffer_data_in_write(nxe_ostream* os, nxe_istream* is, int fd, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  tf_buffer* tfb=OBJ_PTR_FROM_FLD_PTR(tf_buffer, data_in, os);
  //nxe_loop* loop=os->super.loop;
  if (tfb->overflow) { // reached max_tf_size
    // swallow input data
  }
  else {
    int wsize=size;
    if (tfb->data_size+wsize > MAX_TEMPLATE_SIZE) wsize=MAX_TEMPLATE_SIZE-tfb->data_size;
    assert(wsize>=0);
    if (wsize>0) {
      nxb_make_room(tfb->nxb, wsize);
      char* dptr=nxb_get_room(tfb->nxb, 0);
      memcpy(dptr, ptr.cptr, wsize);
      nxb_blank_fast(tfb->nxb, wsize);
      tfb->data_size+=wsize;
      if (tfb->data_size >= MAX_TEMPLATE_SIZE) {
        tfb->overflow=1;
      }
    }
  }
  if (*flags&NXEF_EOF) {
    nxe_ostream_unset_ready(os);

    tf_filter_data* tfdata=tfb->tfdata;
    nxt_context* ctx=&tfdata->ctx;
    if (tfb->overflow) {
      nxweb_log_error("MAX_TEMPLATE_SIZE exceeded");
      nxb_unfinish_stream(tfb->nxb);
      ctx->error=1;
      ctx->files_pending--;
    }
    else {
      int size;
      char* ptr=nxb_finish_stream(tfb->nxb, &size);
      if (tfb->file) {
        if (nxt_parse_file(tfb->file, ptr, size)==-1) {
          // handle error
          ctx->files_pending--;
          // it's been logged; ignore it
        }
      }
      else if (tfb->blk) {
        nxt_block_append_value(ctx, tfb->blk, ptr, size, NXT_NONE);
        ctx->files_pending--;
      }
    }
    tf_check_complete(tfdata);
  }
  return size;
}

static const nxe_ostream_class tf_buffer_data_in_class={.write=tf_buffer_data_in_write};

static void tf_buffer_init(tf_buffer* tfb, nxb_buffer* nxb, tf_filter_data* tfdata, nxt_file* file, nxt_block* blk) {
  memset(tfb, 0, sizeof(tf_buffer));
  tfb->nxb=nxb;
  tfb->data_in.super.cls.os_cls=&tf_buffer_data_in_class;
  tfb->data_in.ready=1;
  tfb->tfdata=tfdata;
  tfb->file=file;
  tfb->blk=blk;
}

static void tf_buffer_make_room(tf_buffer* tfb, int data_size) {
  assert(tfb->data_size+data_size <= MAX_TEMPLATE_SIZE);
  nxb_make_room(tfb->nxb, data_size);
}

static void tf_on_subrequest_ready(nxe_data data) {
  nxweb_http_server_connection* subconn=(nxweb_http_server_connection*)data.ptr;
  nxweb_http_request* subreq=&subconn->hsp.req;
  nxweb_http_server_connection* conn=subconn->parent;
  //nxweb_http_request* req=&conn->hsp.req;
  nxweb_http_response* resp=subconn->hsp.resp;

  tf_buffer* tfb=nxweb_get_request_data(subreq, TFB_KEY).ptr;
  assert(tfb);
  tf_filter_data* tfdata=tfb->tfdata;

  int status=resp->status_code;
  if (!subconn->subrequest_failed && (!status || status==200)) {
    if (resp->content_length>0) tf_buffer_make_room(tfb, min(MAX_TEMPLATE_SIZE, resp->content_length));
    nxe_connect_streams(subconn->tdata->loop, subconn->hsp.resp->content_out, &tfb->data_in);
  }
  else {
    // subrequest error
    nxweb_log_error("subrequest failed: %s%s", subconn->hsp.req.host, subconn->hsp.req.uri);
    tfdata->ctx.files_pending--;
    tf_check_complete(tfdata);
  }
}

static void tf_subreq_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data) {
  tf_buffer* tfb=data.ptr;
  if (tfb->data_in.pair) nxe_disconnect_streams(tfb->data_in.pair, &tfb->data_in);
}

static int tf_load(nxt_context* ctx, const char* uri, nxt_file* dst_file, nxt_block* dst_block) { // function to make subrequests
  tf_filter_data* tfdata=OBJ_PTR_FROM_FLD_PTR(tf_filter_data, ctx, ctx);
  tf_buffer* tfb=nxb_calloc_obj(ctx->nxb, sizeof(tf_buffer));
  if (dst_block) {
    // nxweb_log_error("including file %s", uri);
    tf_buffer_init(tfb, ctx->nxb, tfdata, 0, dst_block);
  }
  else {
    // nxweb_log_error("loading template from %s", uri);
    assert(dst_file);
    tf_buffer_init(tfb, ctx->nxb, tfdata, dst_file, 0);
  }
  nxweb_http_server_connection* subconn=nxweb_http_server_subrequest_start(tfdata->conn, tf_on_subrequest_ready, 0, uri);
  nxweb_http_request* subreq=&subconn->hsp.req;
  nxweb_set_request_data(subreq, TFB_KEY, (nxe_data)(void*)tfb, tf_subreq_finalize);
  if (dst_file) subreq->templates_no_parse=1;
}

static nxweb_filter_data* tf_init(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(tf_filter_data));
  return fdata;
}

static void tf_finalize(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  tf_filter_data* tfdata=(tf_filter_data*)fdata;
  if (tfdata->tfb.data_in.pair) nxe_disconnect_streams(tfdata->tfb.data_in.pair, &tfdata->tfb.data_in);
  if (tfdata->input_fd) {
    close(tfdata->input_fd);
    tfdata->input_fd=0;
  }
}

static nxweb_result tf_translate_cache_key(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  if (req->templates_no_parse) {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }
  if (!resp->templates_on) {
    if (*resp->cache_key!=' ') { // response originally comes from file
      if (!resp->mtype && *key!=' ') {
        resp->mtype=nxweb_get_mime_type_by_ext(key); // always returns not null
      }
      if (!resp->mtype && resp->content_type) {
        resp->mtype=nxweb_get_mime_type(resp->content_type);
      }
    }
    if (resp->mtype && !resp->mtype->templates_on) {
      fdata->bypass=1;
      return NXWEB_NEXT;
    }
  }

  // transform to virtual key ( )
  int plen=strlen(key)-root_len;
  assert(plen>=0);
  int rlen=sizeof(" /_tf_")-1;
  char* fc_key=nxb_alloc_obj(req->nxb, rlen+plen+1);
  memcpy(fc_key, " /_tf_", rlen);
  strcpy(fc_key+rlen, key+root_len);
  fdata->cache_key=fc_key;
  fdata->cache_key_root_len=1;
  return NXWEB_OK;
}

static nxweb_result tf_do_filter(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  tf_filter_data* tfdata=(tf_filter_data*)fdata;
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;

  if (resp->gzip_encoded) {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }

  if (!resp->templates_on) {
    if (!resp->mtype && resp->content_type) {
      resp->mtype=nxweb_get_mime_type(resp->content_type);
    }
    if (!resp->mtype || !resp->mtype->templates_on) {
      fdata->bypass=1;
      return NXWEB_NEXT;
    }
  }

  nxd_http_server_proto_setup_content_out(&conn->hsp, resp);

  tfdata->conn=conn;
  nxt_init(&tfdata->ctx, req->nxb, tf_load);
  tf_buffer_init(&tfdata->tfb, req->nxb, tfdata, nxt_file_create(&tfdata->ctx, req->uri), 0);

  // attach content_out to tf_buffer
  if (resp->content_length>0) tf_buffer_make_room(&tfdata->tfb, min(MAX_TEMPLATE_SIZE, resp->content_length));
  nxe_connect_streams(conn->tdata->loop, resp->content_out, &tfdata->tfb.data_in);

  // replace content_out with composite stream
  nxweb_composite_stream* cs=nxweb_composite_stream_init(conn, req);

  nxweb_composite_stream_start(cs, resp);

  // reset previous response content
  resp->content=0;
  resp->sendfile_path=0;
  if (resp->sendfile_fd) {
    // save it to close on finalize
    tfdata->input_fd=resp->sendfile_fd;
    resp->sendfile_fd=0;
  }
  resp->last_modified=0;

  tfdata->cs=cs;

  return NXWEB_OK;
}

nxweb_filter templates_filter={.name="templates", .init=tf_init, .finalize=tf_finalize,
        .translate_cache_key=tf_translate_cache_key, .do_filter=tf_do_filter};
