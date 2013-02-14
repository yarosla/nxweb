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

#include "nxweb/nxweb.h"

#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>

static nxweb_result subreq_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  nxweb_composite_stream* cs=nxweb_composite_stream_init(conn, req);
  nxweb_composite_stream_append_bytes(cs, "[test1]", sizeof("[test1]")-1);
  int fd=open("www/root/index.htm", O_RDONLY|O_NONBLOCK);
  nxweb_composite_stream_append_fd(cs, fd, 0, 15); // fd will be auto-closed
  nxweb_composite_stream_append_subrequest(cs, 0, "/8777/");
  nxweb_composite_stream_append_bytes(cs, "[test2]", sizeof("[test2]")-1);
  nxweb_composite_stream_close(cs);

  nxweb_composite_stream_start(cs, resp);

  return NXWEB_OK;
}

NXWEB_HANDLER(subreq, "/subreq", .on_request=subreq_on_request,
        .flags=NXWEB_HANDLE_ANY, .priority=1000);

int nxt_parse(nxt_context* ctx, const char* uri, char* buf, int buf_len);

static int tmpl_load(nxt_context* ctx, const char* uri, nxt_file* dst_file, nxt_block* dst_block) { // function to make subrequests
  if (dst_block) {
    nxweb_log_error("including file %s", uri);
    nxt_block_append_value(ctx, dst_block, "{% This is included file %}", sizeof("{% This is included file %}")-1, 0);
  }
  else {
    nxweb_log_error("loading template from %s", uri);
    if (!strcmp(uri, "base")) {
      const char* tmpl_src=" {%block _top_%}{%raw%}{{{{%%%%}}}}{%endraw%}{% include aaa %}AAA-YYY{%block header%}Header{%endblock%} bye...{% endblock %}{% block title %}New Title{% endblock %}";
      char* tmpl=nxb_copy_obj(ctx->nxb, tmpl_src, strlen(tmpl_src)+1);
      nxt_parse_file(dst_file, (char*)tmpl, strlen(tmpl));
    }
    else if (!strcmp(uri, "ttt")) {
      const char* tmpl_src=" {% extends 'base'%} {%block header%}Bbbb {%block title%}{%endblock%} {% parent %}{%endblock%}";
      char* tmpl=nxb_copy_obj(ctx->nxb, tmpl_src, strlen(tmpl_src)+1);
      nxt_parse_file(dst_file, (char*)tmpl, strlen(tmpl));
    }
  }
}

static nxweb_result tmpl_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  const char* tmpl="{% extends \"ttt\"%} {% block title %}The Very {% parent %}{% endblock %}";
  tmpl=nxb_copy_obj(req->nxb, tmpl, strlen(tmpl)+1);
  nxt_context ctx;
  nxt_init(&ctx, req->nxb, tmpl_load);
  nxt_parse(&ctx, req->uri, (char*)tmpl, strlen(tmpl));
  nxt_merge(&ctx);
  resp->content=nxt_serialize(&ctx);
  resp->content_length=strlen(resp->content);

  return NXWEB_OK;
}

NXWEB_HANDLER(tmpl, "/tmpl", .on_request=tmpl_on_request,
        .flags=NXWEB_HANDLE_ANY, .priority=1000);


static nxweb_result curtime_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");

  resp->last_modified=nxe_get_current_http_time(conn->tdata->loop);
  const char* max_age_str=nxweb_get_request_parameter(req, "max_age");
  int max_age=0;
  if (max_age_str) {
    max_age=atoi(max_age_str);
    resp->max_age=max_age;
  }

  nxweb_response_append_str(resp, nxe_get_current_http_time_str(conn->tdata->loop));
  nxweb_response_append_str(resp, " [max-age=");
  nxweb_response_append_uint(resp, max_age);
  nxweb_response_append_str(resp, "]");

  return NXWEB_OK;
}

static nxweb_result curtime_generate_cache_key(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_OK; // do not cache POST requests, etc.
  char* key=nxb_alloc_obj(req->nxb, strlen(req->uri)+2);
  *key=' ';
  strcpy(key+1, req->uri);
  resp->cache_key=key;
  resp->cache_key_root_len=1;
  return NXWEB_OK;
}

NXWEB_HANDLER(curtime, "/curtime", .on_request=curtime_on_request,
        .on_generate_cache_key=curtime_generate_cache_key,
        .flags=NXWEB_HANDLE_GET|NXWEB_PARSE_PARAMETERS, .priority=1000,
        .filters={&file_cache_filter}, .file_cache_dir="www/cache/curtime");

#ifdef WITH_IMAGEMAGICK

static nxweb_result captcha_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_set_response_content_type(resp, "text/html");
  nxweb_add_response_header(resp, "X-NXWEB-Draw", "7425");
  return NXWEB_OK;
}

NXWEB_HANDLER(captcha, "/captcha", .on_request=captcha_on_request,
        .flags=NXWEB_HANDLE_GET|NXWEB_PARSE_PARAMETERS, .priority=1000,
        .filters={&draw_filter}, .font="www/fonts/Sansation/Sansation_Bold.ttf");

#endif
