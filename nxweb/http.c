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

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <printf.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "nxweb_internal.h"

const char* nx_simple_map_get(nx_simple_map_entry map[], const char* name) {
  int i;
  for (i=0; map[i].name; i++) {
    if (strcmp(map[i].name, name)==0) return map[i].value;
  }
  return 0;
}

const char* nx_simple_map_get_nocase(nx_simple_map_entry map[], const char* name) {
  int i;
  for (i=0; map[i].name; i++) {
    if (strcasecmp(map[i].name, name)==0) return map[i].value;
  }
  return 0;
}

void nx_simple_map_add(nx_simple_map_entry map[], const char* name, const char* value, int max_entries) {
  int i;
  for (i=0; i<max_entries-1; i++) {
    if (!map[i].name) {
      map[i].name=name;
      map[i].value=value;
      return;
    }
  }
}

// Modifies req->uri and req->request_body content (does url_decode inplace)
// uri could be preserved if requested by preserve_uri
void nxweb_parse_request_parameters(nxweb_request *req, int preserve_uri) {

  if (req->parameters) return; // already parsed
  if (req->rstate!=NXWEB_RS_INITIAL) return; // illegal state

  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  char *name, *value, *next;
  char* query_string=strchr(req->uri, '?');
  if (query_string) {
    if (preserve_uri) {
      query_string++;
      query_string=nxb_copy_obj(nxb, query_string, strlen(query_string)+1);
    }
    else {
      *query_string++='\0';
    }
  }
  // last param must be nulled (nx_buffer allocates objects in reverse direction)
  nxweb_http_parameter* param=nxb_calloc_obj(nxb, sizeof(nxweb_http_parameter));
  if (query_string) {
    for (name=query_string; name; name=next) {
      next=strchr(name, '&');
      if (next) *next++='\0';
      value=strchr(name, '=');
      if (value) *value++='\0';
      else value=name+strlen(name); // ""
      if (*name) {
        nxweb_url_decode(name, 0);
        nxweb_trunc_space(name);
        nxweb_url_decode(value, 0);
        param=nxb_alloc_obj(nxb, sizeof(nxweb_http_parameter));
        param->name=name;
        param->value=value;
      }
    }
  }
  if (req->request_body && strcasecmp(req->content_type, "application/x-www-form-urlencoded")==0) {
    for (name=req->request_body; name; name=next) {
      next=strchr(name, '&');
      if (next) *next++='\0';
      value=strchr(name, '=');
      if (value) *value++='\0';
      else value=name+strlen(name); // ""
      if (*name) {
        nxweb_url_decode(name, 0);
        nxweb_trunc_space(name);
        nxweb_url_decode(value, 0);
        param=nxb_alloc_obj(nxb, sizeof(nxweb_http_parameter));
        param->name=name;
        param->value=value;
      }
    }
    req->request_body=0;
  }
  req->parameters=param;
}

// Modifies conn->cookie content (does url_decode inplace)
void nxweb_parse_request_cookies(nxweb_request *req) {

  if (req->cookies) return; // already parsed
  if (req->rstate!=NXWEB_RS_INITIAL) return; // illegal state

  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  char *name, *value, *next;
  // last cookie must be nulled (nx_buffer allocates objects in reverse direction)
  nxweb_http_cookie* cookie=nxb_calloc_obj(nxb, sizeof(nxweb_http_cookie));
  if (req->cookie) {
    for (name=req->cookie; name; name=next) {
      next=strchr(name, ';');
      if (next) *next++='\0';
      value=strchr(name, '=');
      if (value) *value++='\0';
      if (*name) {
        nxweb_url_decode(name, 0);
        nxweb_trunc_space(name);
        if (value) nxweb_url_decode(value, 0);
        cookie=nxb_alloc_obj(nxb, sizeof(nxweb_http_cookie));
        cookie->name=name;
        cookie->value=value;
      }
    }
    req->cookie=0;
  }
  req->cookies=cookie;
}

const char* nxweb_get_request_header(nxweb_request *req, const char* name) {
  return req->headers? nx_simple_map_get_nocase(req->headers, name) : 0;
}

const char* nxweb_get_request_parameter(nxweb_request *req, const char* name) {
  return req->parameters? nx_simple_map_get(req->parameters, name) : 0;
}

const char* nxweb_get_request_cookie(nxweb_request *req, const char* name) {
  return req->cookies? nx_simple_map_get(req->cookies, name) : 0;
}

char* _nxweb_find_end_of_http_headers(char* buf, int len) {
  if (len<4) return 0;
  char* p;
  for (p=memchr(buf+3, '\n', len-3); p; p=memchr(p+1, '\n', len-(p-buf)-1)) {
    if (*(p-1)=='\n') return p-1;
    if (*(p-3)=='\r' && *(p-2)=='\n' && *(p-1)=='\r') return p-3;
  }
  return 0;
}

// Modifies conn->in_buffer content
int _nxweb_parse_http_request(nxweb_request* req, char* headers, char* end_of_headers, int bytes_received) {
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  char* body=end_of_headers;

  if (!body) return -1; // no body
  while (*body=='\r' || *body=='\n') *body++='\0';

  char* pl;
  char* first_line=strtok_r(headers, "\r\n", &pl);

  char* pc;
  char* method=strtok_r(first_line, " \t", &pc);
  char* uri=strtok_r(0, " \t", &pc);
  char* http_version=strtok_r(0, " \t", &pc);

  int http11=strcasecmp(http_version, "HTTP/1.0")==0? 0 : 1;

  char* host=0;
  // Check for HTTP/1.1 absolute URL
  if (strncasecmp(uri, "http://", 7)==0) {
    host=uri;
    uri=strchr(uri+7, '/');
    if (!uri) return -1;
    int host_len=(uri-host)-7;
    memmove(host, host+7, host_len);
    host[host_len]='\0';
  }

  if (*uri!='/') return -1;

  // Read headers
  char* cookie=0;
  char* user_agent=0;
  char* content_type=0;
  char* expect=0;
  size_t content_length=0;
  char* transfer_encoding=0;
  int keep_alive=http11;
  char* name;
  char* value=0;
  // last header must be nulled
  nxweb_http_header* header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
  while ((name=strtok_r(0, "\r\n", &pl))) {
    if (*name>0 && *name<=' ') {
      // starts with whitespace => header continuation
      if (value) {
        // concatenate with previous value
        memmove(value+strlen(value), name, strlen(name)+1);
      }
      continue;
    }
    value=strchr(name, ':');
    if (!value) continue;
    *value++='\0';
    //value+=strspn(value, " \t");
    value=nxweb_trunc_space(value);

    if (!strcasecmp(name, "Host")) host=value;
    else if (!strcasecmp(name, "Range")) return -2; // not implemented
    else if (!strcasecmp(name, "Trailer")) return -2; // not implemented
    else if (!strcasecmp(name, "Expect")) expect=value;
    else if (!strcasecmp(name, "Cookie")) cookie=value;
    else if (!strcasecmp(name, "User-Agent")) user_agent=value;
    else if (!strcasecmp(name, "Content-Type")) content_type=value;
    else if (!strcasecmp(name, "Content-Length")) content_length=atoi(value);
    else if (!strcasecmp(name, "Transfer-Encoding")) transfer_encoding=value;
    else if (!strcasecmp(name, "Connection")) keep_alive=!strcasecmp(value, "keep-alive");
    else {
      header=nxb_alloc_obj(nxb, sizeof(nxweb_http_header));
      header->name=name;
      header->value=value;
    }
  }
  req->headers=header;

  req->content_type=content_type;
  req->content_length=content_length;
  req->cookie=cookie;
  req->transfer_encoding=transfer_encoding;
  req->keep_alive=keep_alive;
  req->request_body=body;
  req->content_received=bytes_received-(body-headers);
  req->host=host;
  req->method=method;
  req->uri=uri;
  req->http_version=http_version;
  req->http11=http11;
  req->user_agent=user_agent;
  req->path_info=0;
  req->chunked_request=transfer_encoding && !strcasecmp(transfer_encoding, "chunked");
  if (req->chunked_request) req->content_length=-1;
  req->expect_100_continue=req->content_length && expect && !strcasecmp(expect, "100-continue");
  req->head_method=!strcasecmp(req->method, "HEAD");
  if (req->head_method) req->method="GET";

  return 0;
}

void _nxweb_decode_chunked_request(nxweb_request* req) {
  req->content_length=req->content_received=_nxweb_decode_chunked(req->request_body, req->content_received);
  assert(req->content_length>=0);
}

int _nxweb_decode_chunked(char* buf, int buf_len) {
  char* p=buf;
  char* endp;
  char* buf_end=buf+buf_len;
  char* d=buf;
  int size=0;

  while (1) {
    int chunk_size=strtol(p, &endp, 16);
    char* chunk_data=strchr(endp, '\n');
    if (!chunk_data) return -1;
    chunk_data++;
    if (chunk_size==0) {
      *d='\0';
      return size;
    }
    p=chunk_data+chunk_size+2; // plus CRLF
    if (p>=buf_end) return -1;
    if (*(p-2)!='\r' || *(p-1)!='\n') return -1;
    memmove(d, chunk_data, chunk_size);
    d+=chunk_size;
    size+=chunk_size;
  }
  return -1;
}

int _nxweb_verify_chunked(const char* buf, int buf_len) {
  const char* p=buf;
  char* endp;
  const char* buf_end=buf+buf_len;
  int size=0;

  while (1) {
    int chunk_size=strtol(p, &endp, 16);
    char* chunk_data=strchr(endp, '\n');
    if (!chunk_data) return -1;
    chunk_data++;
    if (chunk_size==0) {
      return size;
    }
    p=chunk_data+chunk_size+2; // plus CRLF
    if (p>=buf_end) return -1;
    if (*(p-2)!='\r' || *(p-1)!='\n') return -1;
    size+=chunk_size;
  }
  return -1;
}

int nxweb_send_file(nxweb_request *req, const char* fpath, struct stat* finfo) {
/*
  if (fpath==0) { // cancel sendfile
    if (req->sendfile_fd) close(req->sendfile_fd);
    req->sendfile_fd=0;
    req->sendfile_length=0;
    req->response_content_type=0;
    return 0;
  }

  // if no finfo provided by the caller, get it here
  struct stat _finfo;
  if (!finfo) {
    if (stat(fpath, &_finfo)==-1) return -1;
    finfo=&_finfo;
  }
  if (S_ISDIR(finfo->st_mode)) {
    return -2;
  }
  if (!S_ISREG(finfo->st_mode)) {
    return -3;
  }

  int fd=open(fpath, O_RDONLY|O_NONBLOCK);
  if (fd==-1) return -1;
  req->sendfile_fd=fd;
  req->sendfile_length=finfo->st_size;
  req->sendfile_last_modified=finfo->st_mtime;
  req->response_content_type=nxweb_get_mime_type_by_ext(fpath)->mime;
*/
  return 0;
}

void nxweb_set_response_status(nxweb_request *req, int code, const char* message) {
  req->response_code=code;
  req->response_status=message;
}

void nxweb_set_response_content_type(nxweb_request *req, const char* content_type) {
  req->response_content_type=content_type;
}

void nxweb_set_response_charset(nxweb_request *req, const char* charset) {
  req->response_content_charset=charset;
}

void nxweb_add_response_header(nxweb_request *req, const char* name, const char* value) {
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  if (!req->response_headers) req->response_headers=nxb_calloc_obj(nxb, NXWEB_MAX_RESPONSE_HEADERS*sizeof(nxweb_http_header));
  nx_simple_map_add(req->response_headers, name, value, NXWEB_MAX_RESPONSE_HEADERS);
}

void _nxweb_write_response_headers_raw(nxweb_request *req, const char* fmt, ...) {
  if (req->rstate!=NXWEB_RS_WRITING_RESPONSE_HEADERS) {
    _nxweb_finalize_response_writing_state(req);
    if (req->out_headers) return; // illegal state; already have headers
    req->rstate=NXWEB_RS_WRITING_RESPONSE_HEADERS;
  }
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  va_list ap;
  va_start(ap, fmt);
  nxb_printf_va(nxb, fmt, ap);
  va_end(ap);
}

static void writing_response_headers_complete(nxweb_request *req) {
  assert(req->rstate==NXWEB_RS_WRITING_RESPONSE_HEADERS);
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  nxb_append_char(nxb, '\0');
  req->out_headers=nxb_finish_obj(nxb);
  req->rstate=NXWEB_RS_INITIAL;
}

static int writing_response_body_begin(nxweb_request *req) {
  if (req->rstate==NXWEB_RS_WRITING_RESPONSE_BODY) return 0; // already started = OK
  if (req->out_body) return -1; // already have body
  req->rstate=NXWEB_RS_WRITING_RESPONSE_BODY;
  return 0;
}

static void writing_response_body_complete(nxweb_request *req) {
  assert(req->rstate==NXWEB_RS_WRITING_RESPONSE_BODY);
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  //nxb_append_char(nxb, '\0');
  req->out_body=nxb_get_unfinished(nxb, &req->out_body_length);
  nxb_finish_obj(nxb);
  req->rstate=NXWEB_RS_INITIAL;
}

void _nxweb_finalize_response_writing_state(nxweb_request *req) {
  switch (req->rstate) {
    case NXWEB_RS_WRITING_RESPONSE_HEADERS:
      writing_response_headers_complete(req);
      break;
    case NXWEB_RS_WRITING_RESPONSE_BODY:
      writing_response_body_complete(req);
      break;
    default:
      break;
  }
}

void _nxweb_prepare_response_headers(nxweb_request *req) {
  char date_buf[64];
  time_t t;
  struct tm tm;
  time(&t);
  gmtime_r(&t, &tm);
  strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);

  _nxweb_write_response_headers_raw(req, "HTTP/1.%d %d %s\r\n"
           "Server: nxweb/" REVISION "\r\n"
           "Date: %s\r\n"
           "Connection: %s\r\n",
           req->http11, req->response_code? req->response_code : 200,
           req->response_status? req->response_status : "OK",
           date_buf,
           req->keep_alive?"keep-alive":"close");
  if (req->response_headers) {
    // write added headers
    int i;
    for (i=0; req->response_headers[i].name; i++) {
      _nxweb_write_response_headers_raw(req, "%s: %s\r\n", req->response_headers[i].name, req->response_headers[i].value);
    }
  }
  const nxweb_mime_type* mtype=nxweb_get_mime_type(req->response_content_type);
  if (req->response_content_charset && mtype && mtype->charset_required) {
    _nxweb_write_response_headers_raw(req,
             "Content-Type: %s; charset=%s\r\n",
             mtype->mime, req->response_content_charset);
  }
  else {
    _nxweb_write_response_headers_raw(req,
             "Content-Type: %s\r\n",
             mtype? mtype->mime : req->response_content_type);
  }
  if (req->response_last_modified) {
    gmtime_r(&req->response_last_modified, &tm);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);
    _nxweb_write_response_headers_raw(req, "Last-Modified: %s\r\n", date_buf);
  }
  _nxweb_write_response_headers_raw(req,
           "Content-Length: %u\r\n\r\n",
           (unsigned)req->out_body_length);
  writing_response_headers_complete(req);
}

void nxweb_response_make_room(nxweb_request *req, int size) {
  if (req->rstate!=NXWEB_RS_WRITING_RESPONSE_BODY) {
    _nxweb_finalize_response_writing_state(req);
    if (writing_response_body_begin(req)) return; // illegal state
  }
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  nxb_make_room(nxb, size);
}

void nxweb_response_append(nxweb_request *req, const char* text) {
  if (req->rstate!=NXWEB_RS_WRITING_RESPONSE_BODY) {
    _nxweb_finalize_response_writing_state(req);
    if (writing_response_body_begin(req)) return; // illegal state
  }
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  nxb_append(nxb, text, strlen(text));
}

void nxweb_response_printf(nxweb_request *req, const char* fmt, ...) {
  if (req->rstate!=NXWEB_RS_WRITING_RESPONSE_BODY) {
    _nxweb_finalize_response_writing_state(req);
    if (writing_response_body_begin(req)) return; // illegal state
  }
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  va_list ap;
  va_start(ap, fmt);
  nxb_printf_va(nxb, fmt, ap);
  va_end(ap);
}

void nxweb_send_http_error(nxweb_request *req, int code, const char* message) {
  const char* response_body="<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\"><html xmlns=\"http://www.w3.org/1999/xhtml\" version=\"-//W3C//DTD XHTML 1.1//EN\" xml:lang=\"en\"><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/><title>%s</title></head><body><p>%s</p></body></html>";
  nxweb_response_printf(req, response_body, message, message);
  nxweb_set_response_status(req, code, message);
}

void nxweb_send_redirect(nxweb_request *req, int code, const char* location) {
  if (!strncmp(location, "http", 4)) { // absolute uri
    _nxweb_write_response_headers_raw(req,
             "HTTP/1.%d %d %s\r\n"
             "Connection: %s\r\n"
             "Content-Length: 0\r\n"
             "Location: %s\r\n\r\n",
             req->http11, code, code==302? "Found":(code==301? "Moved Permanently":"Redirect"),
             req->keep_alive?"keep-alive":"close",
             location);
  }
  else { // uri without host
    _nxweb_write_response_headers_raw(req,
             "HTTP/1.%d %d %s\r\n"
             "Connection: %s\r\n"
             "Content-Length: 0\r\n"
             "Location: http://%s%s\r\n\r\n",
             req->http11, code, code==302? "Found":(code==301? "Moved Permanently":"Redirect"),
             req->keep_alive?"keep-alive":"close",
             req->host, location);
  }
  writing_response_headers_complete(req);
}

//int nxweb_response_append_escape_html(nxweb_request *req, const char* text) {
//  /* count chars to escape in text */
//  const char* pt=text;
//  int count=0;
//  while ((pt=strpbrk(pt, "<>\"'&"))) count++, pt++;
//
//  int size_avail;
//  char* pbuf=nx_buffer_space(conn->outb_buffer, strlen(text)+count*5, &size_avail);
//  char* pb=pbuf;
//  if (!pb) return -1;
//
//  char c;
//  pt=text;
//  while ((c=*pt++)) {
//    switch (c) {
//      case '<': *pb++='&'; *pb++='l'; *pb++='t'; *pb++=';'; break;
//      case '>': *pb++='&'; *pb++='g'; *pb++='t'; *pb++=';'; break;
//      case '"': *pb++='&'; *pb++='q'; *pb++='u'; *pb++='o'; *pb++='t'; *pb++=';'; break;
//      case '\'': *pb++='&'; *pb++='#'; *pb++='3'; *pb++='9'; *pb++=';'; break;
//      case '&': *pb++='&'; *pb++='a'; *pb++='m'; *pb++='p'; *pb++=';'; break;
//      default: *pb++=c; break;
//    }
//  }
//  nx_buffer_append_room(conn->outb_buffer, pb-pbuf);
//  return 0;
//}

static const char CHAR_MAP[256] = {
	/* 0 */
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
	100, 101, 102, 103, 104, 105, 106, 107,   108, 109, 0, 0, 0, 0, 0, 0,
	/* 64 */
	0, 110, 111, 112, 113, 114, 115, 1,   1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
	0, 110, 111, 112, 113, 114, 115, 1,   1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
	/* 128 */
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	/* 192 */
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

static inline char HEX_DIGIT(char n) { n&=0xf; return n<10? n+'0' : n-10+'A'; }

static inline char HEX_DIGIT_VALUE(char c) {
  char n=CHAR_MAP[(unsigned char)c]-100;
  return n<0? 0 : n;
}

static inline int IS_URI_CHAR(char c) {
  return CHAR_MAP[(unsigned char)c];
}

//int nxweb_response_append_escape_url(nxweb_request *req, const char* text) {
//  const char* pt=text;
//
//  int size_avail;
//  char* pbuf=nx_buffer_space(conn->outb_buffer, strlen(text)*3, &size_avail);
//  char* pb=pbuf;
//  if (!pb) return -1;
//
//  char c;
//  while ((c=*pt++)) {
//    if (IS_URI_CHAR(c)) *pb++=c;
//    else {
//      *pb++='%'; *pb++=HEX_DIGIT(c>>4); *pb++=HEX_DIGIT(c);
//    }
//  }
//  nx_buffer_append_room(conn->outb_buffer, pb-pbuf);
//  return 0;
//}

static int print_html_escaped(FILE *stream, const struct printf_info *info, const void *const *args) {
  const char *arg=*((const char **)(args[0]));
//  nxweb_log_error("print_html_escaped called with %s arg", arg);
  if (!arg) {
    fputs("(null)", stream);
    return 6;
  }
  int len=0;
  const char* pt=arg;
  char c;
  while ((c=*pt++)) {
    switch (c) {
      case '<': fputs("&lt;", stream); len+=4; break;
      case '>': fputs("&gt;", stream); len+=4; break;
      case '"': fputs("&quot;", stream); len+=6; break;
      case '\'': fputs("&#39;", stream); len+=5; break;
      case '&': fputs("&amp;", stream); len+=5; break;
      default: fputc(c, stream); len++; break;
    }
  }
  return len;
}

static int print_url_escaped(FILE *stream, const struct printf_info *info, const void *const *args) {
  const char *arg=*((const char **)(args[0]));
  if (!arg) {
    fputs("(null)", stream);
    return 6;
  }
  int len=0;
  const char* pt=arg;
  char c;
  while ((c=*pt++)) {
    if (IS_URI_CHAR(c)) {
      fputc(c, stream);
      len++;
    }
    else {
      fputc('%', stream);
      fputc(HEX_DIGIT(c>>4), stream);
      fputc(HEX_DIGIT(c), stream);
      len+=3;
    }
  }
  return len;
}

#ifdef USE_REGISTER_PRINTF_SPECIFIER

static int print_string_arginfo(const struct printf_info *info, size_t n, int *argtypes, int *size) {
  // We always take exactly one argument and this is a pointer to null-terminated string
  if (n>0) argtypes[0]=PA_STRING;
  return 1;
}

void _nxweb_register_printf_extensions() {
  register_printf_specifier('H', print_html_escaped, print_string_arginfo);
  register_printf_specifier('U', print_url_escaped, print_string_arginfo);
}

#else

static int print_string_arginfo(const struct printf_info *info, size_t n, int *argtypes) {
  // We always take exactly one argument and this is a pointer to null-terminated string
  if (n>0) argtypes[0]=PA_STRING;
  return 1;
}

void _nxweb_register_printf_extensions() {
  register_printf_function('H', print_html_escaped, print_string_arginfo);
  register_printf_function('U', print_url_escaped, print_string_arginfo);
}

#endif

const unsigned char PIXEL_GIF[43] = {
  0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x01, 0x00, 0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x4C, 0x01, 0x00, 0x3B
};

char* nxweb_url_decode(char* src, char* dst) { // can do it inplace
  register char *d=(dst?dst:src), *s=src;
  for (; *s; s++) {
    char c=*s;
    if (c=='+') *d++=' ';
    else if (c=='%' && s[1] && s[2]) {
      *d++=HEX_DIGIT_VALUE(s[1])<<4 | HEX_DIGIT_VALUE(s[2]);
      s+=2;
    }
    else *d++=c;
  }
  *d='\0';
  return dst;
}

char* nxweb_trunc_space(char* str) { // does it inplace
  char *p;

  if (!str || !*str) return str;

  // truncate beginning of string
  p=str;
  while (*p && ((unsigned char)*p)<=((unsigned char)' ')) p++;
  if (p!=str) memmove(str, p, strlen(p)+1);

  // truncate end of string
  p=str+strlen(str)-1;
  while (p>=str && ((unsigned char)*p)<=((unsigned char)' ')) *p--='\0';

  return str;
}
