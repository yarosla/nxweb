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

#define SPACE 32U

// Modifies headers content
int _nxweb_parse_http_request(nxweb_request* req, char* headers, char* end_of_headers, int bytes_received) {
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  char* body=end_of_headers;

  if (!body) return -1; // no body
  while (*body=='\r' || *body=='\n') *body++='\0';

  req->content_length=0;

  // first line
  char* pl=strchr(headers, '\n');
  if (pl) *pl++='\0';
  else pl=body;
  req->method=headers;
  char* p=headers;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';
  while ((unsigned char)*p<=SPACE && p<pl) p++;
  if (p>=pl) return -1;
  req->uri=p;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';
  while ((unsigned char)*p<=SPACE && p<pl) p++;
  if (p>=pl) return -1;
  req->http_version=p;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';

  req->keep_alive=
  req->http11=strcasecmp(req->http_version, "HTTP/1.0")==0? 0 : 1;

  if (strncmp(req->uri, "http://", 7)==0) {
    char* host=req->uri;
    char* uri=strchr(req->uri+7, '/');
    if (!uri) return -1;
    int host_len=(uri-host)-7;
    memmove(host, host+7, host_len);
    host[host_len]='\0';
    req->host=host;
    req->uri=uri;
  }

  if (*req->uri!='/') return -1;

  // Read headers
  char* name;
  char* value=0;
  char* expect=0;
  // last header must be nulled
  nxweb_http_header* header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
  while (pl<body) {
    name=pl;
    pl=strchr(pl, '\n');
    if (pl) *pl++='\0';
    else pl=body;

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

    if (!strcasecmp(name, "Host")) req->host=value;
    else if (!strcasecmp(name, "Range")) req->range=value; // not implemented
    else if (!strcasecmp(name, "Trailer")) return -2; // not implemented
    else if (!strcasecmp(name, "Expect")) expect=value;
    else if (!strcasecmp(name, "Cookie")) req->cookie=value;
    else if (!strcasecmp(name, "User-Agent")) req->user_agent=value;
    else if (!strcasecmp(name, "Content-Type")) req->content_type=value;
    else if (!strcasecmp(name, "Content-Length")) req->content_length=atoi(value);
    else if (!strcasecmp(name, "Transfer-Encoding")) req->transfer_encoding=value;
    else if (!strcasecmp(name, "Connection")) req->keep_alive=!strcasecmp(value, "keep-alive");
    else {
      header=nxb_alloc_obj(nxb, sizeof(nxweb_http_header));
      header->name=name;
      header->value=value;
    }
  }
  req->headers=header;

  req->request_body=body;
  req->content_received=bytes_received-(body-headers);
  req->path_info=0;
  req->chunked_request=req->transfer_encoding && !strcasecmp(req->transfer_encoding, "chunked");
  if (req->chunked_request) req->content_length=-1;
  req->expect_100_continue=req->content_length && expect && !strcasecmp(expect, "100-continue");
  req->head_method=!strcasecmp(req->method, "HEAD");
  if (req->head_method) req->method="GET";
  req->get_method=req->head_method || !strcasecmp(req->method, "GET");
  req->post_method=!req->head_method && !strcasecmp(req->method, "POST");

  return 0;
}

int _nxweb_decode_chunked_stream(nxweb_chunked_decoder_state* decoder_state, char* buf, long* buf_len) {
  char* p=buf;
  char* d=buf;
  char* end=buf+*buf_len;
  char c;
  while (p<end) {
    c=*p;
    switch (decoder_state->state) {
      case CDS_DATA:
        if (end-p>=decoder_state->chunk_bytes_left) {
          p+=decoder_state->chunk_bytes_left;
          decoder_state->chunk_bytes_left=0;
          decoder_state->state=CDS_CR1;
          d=p;
          break;
        }
        else {
          decoder_state->chunk_bytes_left-=(end-p);
          *buf_len=(end-buf);
          return 0;
        }
      case CDS_CR1:
        if (c!='\r') return -1;
        p++;
        decoder_state->state=CDS_LF1;
        break;
      case CDS_LF1:
        if (c!='\n') return -1;
        p++;
        decoder_state->state=CDS_SIZE;
        break;
      case CDS_SIZE: // read digits until CR2
        if (c=='\r') {
          if (!decoder_state->chunk_bytes_left) {
            // terminator found
            *buf_len=(d-buf);
            return 1;
          }
          p++;
          decoder_state->state=CDS_LF2;
        }
        else {
          if (c>='0' && c<='9') c-='0';
          else if (c>='A' && c<='F') c=c-'A'+10;
          else if (c>='a' && c<='f') c=c-'a'+10;
          else return -1;
          decoder_state->chunk_bytes_left=(decoder_state->chunk_bytes_left<<4)+c;
          p++;
        }
        break;
      case CDS_LF2:
        if (c!='\n') return -1;
        p++;
        memmove(d, p, end-p);
        end-=(p-d);
        p=d;
        decoder_state->state=CDS_DATA;
        break;
    }
  }
  *buf_len=(d-buf);
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

int nxweb_send_file(nxweb_request *req, const char* fpath, struct stat* finfo, off_t offset, size_t size, const char* charset) {
  if (fpath==0) { // cancel sendfile
    if (req->sendfile_fd) close(req->sendfile_fd);
    req->sendfile_fd=0;
    req->sendfile_offset=0;
    req->response_content_type=0;
    req->out_body_length=0;
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
  req->sendfile_offset=offset;
  req->out_body_length=size? size : finfo->st_size-offset;
  req->response_last_modified=finfo->st_mtime;
  const nxweb_mime_type* mtype=nxweb_get_mime_type_by_ext(fpath);
  req->response_content_type=mtype->mime;
  if (mtype->charset_required) req->response_content_charset=charset;
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

static inline int writing_response_body_begin(nxweb_request *req) {
  if (req->rstate==NXWEB_RS_WRITING_RESPONSE_BODY) return 0; // already started = OK
  if (req->out_body) return -1; // already have body
  req->rstate=NXWEB_RS_WRITING_RESPONSE_BODY;
  return 0;
}

static inline void writing_response_body_complete(nxweb_request *req) {
  if (req->rstate!=NXWEB_RS_WRITING_RESPONSE_BODY) return;
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  req->out_body=nxb_get_unfinished(nxb, &req->out_body_length);
  nxb_finish_stream(nxb);
  req->rstate=NXWEB_RS_INITIAL;
}

void nxweb_response_make_room(nxweb_request *req, int size) {
  if (writing_response_body_begin(req)) return; // illegal state
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  nxb_make_room(nxb, size);
}

void nxweb_response_append(nxweb_request *req, const char* text) {
  if (writing_response_body_begin(req)) return; // illegal state
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  nxb_append(nxb, text, strlen(text));
}

void nxweb_response_printf(nxweb_request *req, const char* fmt, ...) {
  if (writing_response_body_begin(req)) return; // illegal state
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  va_list ap;
  va_start(ap, fmt);
  nxb_printf_va(nxb, fmt, ap);
  va_end(ap);
}

void _nxweb_finalize_response(nxweb_request *req) {
  writing_response_body_complete(req);
}

static char* uint_to_decimal_string(unsigned n, char* buf, int buf_size) {
  char* p=buf+buf_size;
  *--p='\0';
  if (!n) {
    *--p='0';
    return p;
  }
  while (n) {
    *--p=n%10+'0';
    n=n/10;
  }
  return p;
}

void _nxweb_prepare_response_headers(nxweb_request *req) {
  char date_buf[64], buf[32];
  time_t t;
  struct tm tm;
  time(&t);
  gmtime_r(&t, &tm);
  strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);

  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;

  writing_response_body_complete(req);

  nxb_make_room(nxb, 200);
  nxb_append_fast(nxb, "HTTP/1.", 7);
  nxb_append_char_fast(nxb, req->http11? '1':'0');
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str_fast(nxb, uint_to_decimal_string(req->response_code? req->response_code : 200, buf, sizeof(buf)));
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str(nxb, req->response_status? req->response_status : "OK");
  nxb_make_room(nxb, 200);
  nxb_append_str_fast(nxb, "\r\n"
                      "Server: nxweb/" REVISION "\r\n"
                      "Date: ");
  nxb_append_str_fast(nxb, date_buf);
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, req->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\n");

  if (req->response_headers) {
    // write added headers
    int i;
    for (i=0; req->response_headers[i].name; i++) {
      nxb_append_str(nxb, req->response_headers[i].name);
      nxb_append_char(nxb, ':');
      nxb_append_char(nxb, ' ');
      nxb_append_str(nxb, req->response_headers[i].value);
      nxb_append_str(nxb, "\r\n");
    }
  }
  nxb_append_str(nxb, "Content-Type: ");
  nxb_append_str(nxb, req->response_content_type? req->response_content_type : "text/html");
  if (req->response_content_charset) {
    nxb_append_str(nxb, "; charset=");
    nxb_append_str(nxb, req->response_content_charset);
  }
  nxb_append_str(nxb, "\r\n");
  if (req->response_last_modified) {
    gmtime_r(&req->response_last_modified, &tm);
    strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);
    nxb_append_str(nxb, "Last-Modified: ");
    nxb_append_str(nxb, date_buf);
    nxb_append_str(nxb, "\r\n");
  }
  nxb_make_room(nxb, 48);
  if (req->out_body_length>=0) {
    nxb_append_str_fast(nxb, "Content-Length: ");
    nxb_append_str_fast(nxb, uint_to_decimal_string(req->out_body_length, buf, sizeof(buf)));
  }
  else {
    nxb_append_str_fast(nxb, "Transfer-Encoding: chunked");
  }
  nxb_append_fast(nxb, "\r\n\r\n", 5);

  req->out_headers=nxb_finish_stream(nxb);
}

void nxweb_send_redirect(nxweb_request *req, int code, const char* location) {
  char date_buf[64], buf[32];
  time_t t;
  struct tm tm;
  time(&t);
  gmtime_r(&t, &tm);
  strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);

  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;

  writing_response_body_complete(req);

  nxb_make_room(nxb, 250);
  nxb_append_fast(nxb, "HTTP/1.", 7);
  nxb_append_char_fast(nxb, req->http11? '1':'0');
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str_fast(nxb, uint_to_decimal_string(code, buf, sizeof(buf)));
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str(nxb, code==302? "Found":(code==301? "Moved Permanently":"Redirect"));
  nxb_append_str_fast(nxb, "\r\n"
                      "Server: nxweb/" REVISION "\r\n"
                      "Date: ");
  nxb_append_str_fast(nxb, date_buf);
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, req->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\nContent-Length: 0\r\nLocation: ");
  if (!strncmp(location, "http", 4)) { // absolute uri
    nxb_append_str(nxb, location);
  }
  else {
    nxb_append_str(nxb, "http://");
    nxb_append_str(nxb, req->host);
    nxb_append_str(nxb, location);
  }
  nxb_append_str(nxb, "\r\n\r\n");

  req->out_headers=nxb_finish_stream(nxb);
}

void nxweb_send_http_error(nxweb_request *req, int code, const char* message) {
  static const char* response_body1="<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" \"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\">"
    "<head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>"
    "<title>";
  static const char* response_body2="</title>"
    "</head>\n"
    "<body><p>";
  static const char* response_body3="</p></body>\n"
    "</html>";
  if (writing_response_body_begin(req)) return; // illegal state
  nxb_buffer* nxb=&NXWEB_REQUEST_CONNECTION(req)->iobuf;
  nxb_append_str(nxb, response_body1);
  nxb_append_str(nxb, message);
  nxb_append_str(nxb, response_body2);
  nxb_append_str(nxb, message);
  nxb_append_str(nxb, response_body3);
  nxweb_set_response_status(req, code, message);
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
