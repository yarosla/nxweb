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
#include <sys/stat.h>


static const char* WEEK_DAY[]={"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* MONTH[]={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

int nxweb_format_http_time(char* buf, struct tm* tm) {
  // eg. Tue, 24 Jan 2012 13:05:54 GMT
  char* p=buf;
  char num[16];
  strcpy(p, WEEK_DAY[tm->tm_wday]);
  p+=3;
  *p++=',';
  *p++=' ';
  strcpy(p, uint_to_decimal_string(tm->tm_mday, num, sizeof(num)));
  p++;
  if (*p) p++;
  *p++=' ';
  strcpy(p, MONTH[tm->tm_mon]);
  p+=3;
  *p++=' ';
  strcpy(p, uint_to_decimal_string(tm->tm_year+1900, num, sizeof(num)));
  p+=4;
  *p++=' ';
  uint_to_decimal_string_zeropad(tm->tm_hour, p, 2);
  p+=2;
  *p++=':';
  uint_to_decimal_string_zeropad(tm->tm_min, p, 2);
  p+=2;
  *p++=':';
  uint_to_decimal_string_zeropad(tm->tm_sec, p, 2);
  p+=2;
  *p++=' ';
  *p++='G';
  *p++='M';
  *p++='T';
  *p='\0';
  return p-buf;
}


// Modifies req->uri and req->request_body content (does url_decode inplace)
// uri could be preserved if requested by preserve_uri
void nxweb_parse_request_parameters(nxweb_http_request *req, int preserve_uri) {

  if (req->parameters) return; // already parsed

  nxb_buffer* nxb=req->nxb;
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
        name=nxweb_trunc_space(name);
        nxweb_url_decode(value, 0);
        param=nxb_alloc_obj(nxb, sizeof(nxweb_http_parameter));
        param->name=name;
        param->value=value;
      }
    }
  }
  if (req->content && nx_strcasecmp(req->content_type, "application/x-www-form-urlencoded")==0) {
    for (name=(char*)req->content; name; name=next) {
      next=strchr(name, '&');
      if (next) *next++='\0';
      value=strchr(name, '=');
      if (value) *value++='\0';
      else value=name+strlen(name); // ""
      if (*name) {
        nxweb_url_decode(name, 0);
        name=nxweb_trunc_space(name);
        nxweb_url_decode(value, 0);
        param=nxb_alloc_obj(nxb, sizeof(nxweb_http_parameter));
        param->name=name;
        param->value=value;
      }
    }
    req->content=0;
  }
  req->parameters=param;
}

// Modifies conn->cookie content (does url_decode inplace)
void nxweb_parse_request_cookies(nxweb_http_request *req) {

  if (req->cookies) return; // already parsed

  nxb_buffer* nxb=req->nxb;
  char *name, *value, *next;
  // last cookie must be nulled (nx_buffer allocates objects in reverse direction)
  nxweb_http_cookie* cookie=nxb_calloc_obj(nxb, sizeof(nxweb_http_cookie));
  if (req->cookie) {
    for (name=(char*)req->cookie; name; name=next) {
      next=strchr(name, ';');
      if (next) *next++='\0';
      value=strchr(name, '=');
      if (value) *value++='\0';
      if (*name) {
        nxweb_url_decode(name, 0);
        name=nxweb_trunc_space(name);
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

char* _nxweb_find_end_of_http_headers(char* buf, int len, char** start_of_body) {
  if (len<4) return 0;
  char* p;
  for (p=memchr(buf+3, '\n', len-3); p; p=memchr(p+1, '\n', len-(p-buf)-1)) {
    if (*(p-1)=='\n') { *start_of_body=p+1; return p-1; }
    if (*(p-3)=='\r' && *(p-2)=='\n' && *(p-1)=='\r') { *start_of_body=p+1; return p-3; }
  }
  return 0;
}

#define SPACE 32U

enum nxweb_http_header_name {
  NXWEB_HTTP_UNKNOWN,
  NXWEB_HTTP_DATE,
  NXWEB_HTTP_HOST,
  NXWEB_HTTP_RANGE,
  NXWEB_HTTP_COOKIE,
  NXWEB_HTTP_EXPECT,
  NXWEB_HTTP_SERVER,
  NXWEB_HTTP_TRAILER,
  NXWEB_HTTP_CONNECTION,
  NXWEB_HTTP_KEEP_ALIVE,
  NXWEB_HTTP_USER_AGENT,
  NXWEB_HTTP_CONTENT_TYPE,
  NXWEB_HTTP_CONTENT_LENGTH,
  NXWEB_HTTP_ACCEPT_ENCODING,
  NXWEB_HTTP_TRANSFER_ENCODING
};

static int identify_http_header(const char* name, int name_len) {
  if (!name_len) name_len=strlen(name);
  char first_char=nx_tolower(*name);
  switch (name_len) {
    case 4:
      if (first_char=='h') return nx_strcasecmp(name, "Host")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_HOST;
      if (first_char=='d') return nx_strcasecmp(name, "Date")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_DATE;
      return NXWEB_HTTP_UNKNOWN;
    case 5:
      if (first_char=='r') return nx_strcasecmp(name, "Range")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_RANGE;
      return NXWEB_HTTP_UNKNOWN;
    case 6:
      if (first_char=='c') return nx_strcasecmp(name, "Cookie")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_COOKIE;
      if (first_char=='e') return nx_strcasecmp(name, "Expect")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_EXPECT;
      if (first_char=='s') return nx_strcasecmp(name, "Server")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_SERVER;
      return NXWEB_HTTP_UNKNOWN;
    case 7:
      if (first_char=='t') return nx_strcasecmp(name, "Trailer")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_TRAILER;
      return NXWEB_HTTP_UNKNOWN;
    case 10:
      if (first_char=='c') return nx_strcasecmp(name, "Connection")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CONNECTION;
      if (first_char=='k') return nx_strcasecmp(name, "Keep-Alive")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_KEEP_ALIVE;
      if (first_char=='u') return nx_strcasecmp(name, "User-Agent")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_USER_AGENT;
      return NXWEB_HTTP_UNKNOWN;
    case 12:
      if (first_char=='c') return nx_strcasecmp(name, "Content-Type")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CONTENT_TYPE;
      return NXWEB_HTTP_UNKNOWN;
    case 14:
      if (first_char=='c') return nx_strcasecmp(name, "Content-Length")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CONTENT_LENGTH;
      return NXWEB_HTTP_UNKNOWN;
    case 15:
      if (first_char=='a') return nx_strcasecmp(name, "Accept-Encoding")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_ACCEPT_ENCODING;
      return NXWEB_HTTP_UNKNOWN;
    case 17:
      if (first_char=='t') return nx_strcasecmp(name, "Transfer-Encoding")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_TRANSFER_ENCODING;
      return NXWEB_HTTP_UNKNOWN;
    default:
      return NXWEB_HTTP_UNKNOWN;
  }
}

// Modifies headers content
int _nxweb_parse_http_request(nxweb_http_request* req, char* headers, char* end_of_headers) {
  nxb_buffer* nxb=req->nxb;
  if (!end_of_headers) return -1; // no body
  *end_of_headers='\0';

  req->content_length=0;

  // first line
  char* pl=strchr(headers, '\n');
  if (pl) *pl='\0';
  else pl=end_of_headers;
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
  req->http11=nx_strcasecmp(req->http_version, "HTTP/1.0")==0? 0 : 1;

  if (strncmp(req->uri, "http://", 7)==0) {
    char* host=(char*)req->uri;
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
  pl++;
  char* name;
  int name_len;
  int header_name_id;
  char* value=0;
  char* expect=0;
  // last header must be nulled
  nxweb_http_header* header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
  while (pl<end_of_headers) {
    name=pl;
    pl=strchr(pl, '\n');
    if (pl) *pl++='\0';
    else pl=end_of_headers;

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
    name_len=value-name;
    *value++='\0';
    //value+=strspn(value, " \t");
    value=nxweb_trunc_space(value);

    header_name_id=identify_http_header(name, name_len);
    switch (header_name_id) {
      case NXWEB_HTTP_HOST: req->host=value; break;
      case NXWEB_HTTP_EXPECT: expect=value; break;
      case NXWEB_HTTP_COOKIE: req->cookie=value; break;
      case NXWEB_HTTP_USER_AGENT: req->user_agent=value; break;
      case NXWEB_HTTP_CONTENT_TYPE: req->content_type=value; break;
      case NXWEB_HTTP_CONTENT_LENGTH: req->content_length=atol(value); break;
      case NXWEB_HTTP_ACCEPT_ENCODING: req->accept_encoding=value; break;
      case NXWEB_HTTP_TRANSFER_ENCODING: req->transfer_encoding=value; break;
      case NXWEB_HTTP_CONNECTION: req->keep_alive=!nx_strcasecmp(value, "keep-alive"); break;
      case NXWEB_HTTP_RANGE: req->range=value; break;
      case NXWEB_HTTP_TRAILER: return -2; // not implemented
      default:
        header=nxb_alloc_obj(nxb, sizeof(nxweb_http_header));
        header->name=name;
        header->value=value;
        break;
    }
  }
  req->headers=header;

  req->path_info=0;
  {
    const char* g;
    for (g=req->accept_encoding; g; g=strchr(g+1, 'g')) {
      if (!strncmp(g, "gzip", 4) && (g==req->accept_encoding || *(g-1)==',' || *(g-1)==' ') && (!g[4] || g[4]==',' || g[4]==' ')) {
        req->accept_gzip_encoding=1;
        break;
      }
    }
  }
  req->chunked_encoding=req->transfer_encoding && !nx_strcasecmp(req->transfer_encoding, "chunked");
  if (req->chunked_encoding) req->content_length=-1;
  req->expect_100_continue=req->content_length && expect && !nx_strcasecmp(expect, "100-continue");
  req->head_method=!nx_strcasecmp(req->method, "HEAD");
  if (req->head_method) req->method="GET";
  req->get_method=req->head_method || !nx_strcasecmp(req->method, "GET");
  req->post_method=!req->head_method && !nx_strcasecmp(req->method, "POST");
  req->other_method=(!req->get_method && !req->post_method);

  return 0;
}

int _nxweb_decode_chunked_stream(nxweb_chunked_decoder_state* decoder_state, char* buf, nxe_size_t* buf_len) {
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
          if (!decoder_state->monitor_only) *buf_len=(end-buf);
          return 0;
        }
      case CDS_CR1:
        if (c!='\r') return -1;
        p++;
        decoder_state->state=CDS_LF1;
        break;
      case CDS_LF1:
        if (c!='\n') return -1;
        if (decoder_state->final_chunk) {
          if (!decoder_state->monitor_only) *buf_len=(d-buf);
          return 1;
        }
        p++;
        decoder_state->state=CDS_SIZE;
        break;
      case CDS_SIZE: // read digits until CR2
        if (c=='\r') {
          if (!decoder_state->chunk_bytes_left) {
            // terminator found
            decoder_state->final_chunk=1;
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
        if (!decoder_state->monitor_only) {
          memmove(d, p, end-p);
          end-=(p-d);
          p=d;
        }
        decoder_state->state=CDS_DATA;
        break;
    }
  }
  if (!decoder_state->monitor_only) *buf_len=(d-buf);
  return 0;
}

nxe_ssize_t _nxweb_decode_chunked(char* buf, nxe_size_t buf_len) {
  char* p=buf;
  char* endp;
  char* buf_end=buf+buf_len;
  char* d=buf;
  nxe_size_t size=0;

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

nxe_ssize_t _nxweb_verify_chunked(const char* buf, nxe_size_t buf_len) {
  const char* p=buf;
  char* endp;
  const char* buf_end=buf+buf_len;
  nxe_size_t size=0;

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

nxweb_http_response* _nxweb_http_response_init(nxweb_http_response* resp, nxb_buffer* nxb, nxweb_http_request* req) {
  resp->nxb=nxb;
  if (req) {
    resp->host=req->host;
    resp->http11=req->http11;
    resp->keep_alive=req->keep_alive;
  }
  return resp;
}

void nxweb_set_response_status(nxweb_http_response* resp, int code, const char* message) {
  resp->status_code=code;
  resp->status=message;
}

void nxweb_set_response_content_type(nxweb_http_response* resp, const char* content_type) {
  resp->content_type=content_type;
}

void nxweb_set_response_charset(nxweb_http_response* resp, const char* charset) {
  resp->content_charset=charset;
}

void nxweb_add_response_header(nxweb_http_response* resp, const char* name, const char* value) {
  nxb_buffer* nxb=resp->nxb;
  if (!resp->headers) resp->headers=nxb_calloc_obj(nxb, NXWEB_MAX_RESPONSE_HEADERS*sizeof(nxweb_http_header));
  nx_simple_map_add(resp->headers, name, value, NXWEB_MAX_RESPONSE_HEADERS);
}

void _nxweb_prepare_response_headers(nxe_loop* loop, nxweb_http_response *resp) {
  char buf[32];
  struct tm tm;

  nxb_buffer* nxb=resp->nxb;

  nxb_make_room(nxb, 200);
  nxb_append_fast(nxb, "HTTP/1.", 7);
  nxb_append_char_fast(nxb, resp->http11? '1':'0');
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str_fast(nxb, uint_to_decimal_string(resp->status_code? resp->status_code : 200, buf, sizeof(buf)));
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str(nxb, resp->status? resp->status : "OK");
  nxb_make_room(nxb, 200);
  nxb_append_str_fast(nxb, "\r\n"
                      "Server: nxweb/" REVISION "\r\n"
                      "Date: ");
  nxb_append_str_fast(nxb, nxe_get_current_http_time(loop));
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, resp->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\n");

  if (resp->headers) {
    // write added headers
    int i;
    const char* name;
    int header_name_id;
    for (i=0; (name=resp->headers[i].name); i++) {
      header_name_id=identify_http_header(name, 0);
      switch (header_name_id) {
        case NXWEB_HTTP_CONNECTION:
        case NXWEB_HTTP_SERVER:
        case NXWEB_HTTP_CONTENT_TYPE:
        case NXWEB_HTTP_CONTENT_LENGTH:
        case NXWEB_HTTP_TRANSFER_ENCODING:
        case NXWEB_HTTP_DATE:
          // skip these specific headers
          continue;
      }

      nxb_append_str(nxb, resp->headers[i].name);
      nxb_append_char(nxb, ':');
      nxb_append_char(nxb, ' ');
      nxb_append_str(nxb, resp->headers[i].value);
      nxb_append_str(nxb, "\r\n");
    }
  }
  nxb_append_str(nxb, "Content-Type: ");
  nxb_append_str(nxb, resp->content_type? resp->content_type : "text/html");
  if (resp->content_charset) {
    nxb_append_str(nxb, "; charset=");
    nxb_append_str(nxb, resp->content_charset);
  }
  nxb_append_str(nxb, "\r\n");
  if (resp->last_modified) {
    gmtime_r(&resp->last_modified, &tm);
    nxb_make_room(nxb, 48);
    nxb_append_str_fast(nxb, "Last-Modified: ");
    // strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);
    // nxb_append_str(nxb, date_buf);
    nxb_blank_fast(nxb, nxweb_format_http_time(nxb_get_room(nxb, 0), &tm));
    nxb_append_str_fast(nxb, "\r\n");
  }
  if (resp->gzip_encoded) {
    nxb_append_str(nxb, "Content-Encoding: gzip\r\n");
  }
  nxb_make_room(nxb, 48);
  if (resp->content_length>=0) {
    nxb_append_str_fast(nxb, "Content-Length: ");
    nxb_append_str_fast(nxb, uint_to_decimal_string(resp->content_length, buf, sizeof(buf)));
  }
  else {
    nxb_append_str_fast(nxb, "Transfer-Encoding: chunked");
  }
  nxb_append_fast(nxb, "\r\n\r\n", 5);

  resp->raw_headers=nxb_finish_stream(nxb, 0);
}

void nxweb_send_redirect(nxweb_http_response *resp, int code, const char* location) {
  char buf[32];

  nxb_buffer* nxb=resp->nxb;

  nxb_make_room(nxb, 250);
  nxb_append_fast(nxb, "HTTP/1.", 7);
  nxb_append_char_fast(nxb, resp->http11? '1':'0');
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str_fast(nxb, uint_to_decimal_string(code, buf, sizeof(buf)));
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str(nxb, code==302? "Found":(code==301? "Moved Permanently":"Redirect"));
  nxb_append_str_fast(nxb, "\r\n"
                      "Server: nxweb/" REVISION "\r\n"
                      "Date: ");
  nxb_append_str_fast(nxb, nxe_get_current_http_time(_nxweb_net_thread_data->loop));
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, resp->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\nContent-Length: 0\r\nLocation: ");
  if (!strncmp(location, "http", 4)) { // absolute uri
    nxb_append_str(nxb, location);
  }
  else {
    nxb_append_str(nxb, "http://");
    nxb_append_str(nxb, resp->host);
    nxb_append_str(nxb, location);
  }
  nxb_append_str(nxb, "\r\n\r\n");

  resp->raw_headers=nxb_finish_stream(nxb, 0);
}

void nxweb_send_http_error(nxweb_http_response *resp, int code, const char* message) {
  static const char* response_body1="<html>\n<head><title>";
  static const char* response_body2="</title></head>\n<body>\n<h1>";
  static const char* response_body3="</h1>\n<p>nxweb/" REVISION "</p>\n</body>\n</html>";
  nxweb_set_response_status(resp, code, message);
  nxb_buffer* nxb=resp->nxb;
  nxb_append_str(nxb, response_body1);
  nxb_append_str(nxb, message);
  nxb_append_str(nxb, response_body2);
  nxb_append_str(nxb, message);
  nxb_append_str(nxb, response_body3);
  int size;
  resp->content=nxb_finish_stream(nxb, &size);
  resp->content_length=size;
  //resp->keep_alive=0; // close connection after error response
}

int nxweb_send_file(nxweb_http_response *resp, char* fpath, struct stat* finfo, int gzip_encoded, off_t offset, size_t size, const char* charset) {
  if (fpath==0) { // cancel sendfile
    if (resp->sendfile_fd) close(resp->sendfile_fd);
    resp->sendfile_fd=0;
    resp->sendfile_offset=0;
    resp->sendfile_end=0;
    resp->content_type=0;
    resp->content_length=0;
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
  resp->sendfile_fd=fd;
  resp->sendfile_offset=offset;
  resp->content_length=size? size : finfo->st_size-offset;
  resp->sendfile_end=offset+resp->content_length;
  resp->last_modified=finfo->st_mtime;
  resp->gzip_encoded=gzip_encoded;
  int flen;
  if (gzip_encoded) {
    flen=strlen(fpath);
    fpath[flen-3]='\0'; // cut .gz
  }
  const nxweb_mime_type* mtype=nxweb_get_mime_type_by_ext(fpath);
  if (gzip_encoded) fpath[flen-3]='.'; // restore
  resp->content_type=mtype->mime;
  if (mtype->charset_required) resp->content_charset=charset;
  return 0;
}

void nxweb_send_data(nxweb_http_response *resp, const void* data, size_t size, const char* content_type) {
  resp->content_type=content_type;
  resp->content=data;
  resp->content_length=size;
}

const char* _nxweb_prepare_client_request_headers(nxweb_http_request *req) {
  char buf[32];

  nxb_buffer* nxb=req->nxb;

  nxb_make_room(nxb, 250);
  nxb_append_str(nxb, req->head_method? "HEAD" : req->method);
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str(nxb, req->uri);
  nxb_append_fast(nxb, " HTTP/1.", 8);
  nxb_append_char_fast(nxb, req->http11? '1':'0');
  nxb_make_room(nxb, 100);
  nxb_append_str_fast(nxb, "\r\n"
                      "Host: ");
  nxb_append_str(nxb, req->host);
  nxb_make_room(nxb, 100);
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, req->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\n");

  if (req->expect_100_continue) {
    nxb_append_str_fast(nxb, "Expect: 100-continue\r\n");
  }

  if (req->x_forwarded_ssl) {
    nxb_append_str_fast(nxb, "X-NXWEB-Forwarded-SSL: ON\r\n");
  }

  if (req->x_forwarded_host) {
    nxb_append_str(nxb, "X-NXWEB-Forwarded-Host: ");
    nxb_append_str(nxb, req->x_forwarded_host);
    nxb_append_str(nxb, "\r\n");
  }

  if (req->x_forwarded_for) {
    nxb_append_str(nxb, "X-NXWEB-Forwarded-IP: ");
    nxb_append_str(nxb, req->x_forwarded_for);
    nxb_append_str(nxb, "\r\n");
  }

  if (req->user_agent) {
    nxb_append_str(nxb, "User-Agent: ");
    nxb_append_str(nxb, req->user_agent);
    nxb_append_str(nxb, "\r\n");
  }
  else {
    nxb_append_str(nxb, "User-Agent: nxweb/" REVISION "\r\n");
  }

  if (req->headers) {
    // write added headers
    int i;
    for (i=0; req->headers[i].name; i++) {
      nxb_append_str(nxb, req->headers[i].name);
      nxb_append_char(nxb, ':');
      nxb_append_char(nxb, ' ');
      nxb_append_str(nxb, req->headers[i].value);
      nxb_append_str(nxb, "\r\n");
    }
  }

  if (req->accept_encoding) {
    nxb_append_str(nxb, "Accept-Encoding: ");
    nxb_append_str(nxb, req->accept_encoding);
    nxb_append_str(nxb, "\r\n");
  }

  if (req->content_length) {
    nxb_append_str(nxb, "Content-Type: ");
    nxb_append_str(nxb, req->content_type? req->content_type : "application/x-www-form-urlencoded");
    nxb_append_str(nxb, "\r\n");
    nxb_make_room(nxb, 48);
    if (req->content_length>=0) {
      nxb_append_str_fast(nxb, "Content-Length: ");
      nxb_append_str_fast(nxb, uint_to_decimal_string(req->content_length, buf, sizeof(buf)));
    }
    else {
      nxb_append_str_fast(nxb, "Transfer-Encoding: chunked");
    }
    nxb_append_str(nxb, "\r\n");
  }
  nxb_append_fast(nxb, "\r\n", 3);

  return nxb_finish_stream(nxb, 0);
}

int _nxweb_parse_http_response(nxweb_http_response* resp, char* headers, char* end_of_headers) {
  nxb_buffer* nxb=resp->nxb;
  *end_of_headers='\0';

  // first line
  char* pl=strchr(headers, '\n');
  if (pl) *pl='\0';
  else pl=end_of_headers;
  char* http_version=headers;
  char* p=headers;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';
  while ((unsigned char)*p<=SPACE && p<pl) p++;
  if (p>=pl) return -1;
  char* code=p;
  while ((unsigned char)*p>SPACE) p++;
  *p++='\0';
  while ((unsigned char)*p<=SPACE && p<pl) p++;
  if (p>=pl) return -1;
  resp->status=p;
  while ((unsigned char)*p>=SPACE && p<pl) p++;
  *p++='\0';

  resp->keep_alive=
  resp->http11=nx_strcasecmp(http_version, "HTTP/1.0")==0? 0 : 1;
  resp->content_length=-1; // unspecified

  resp->status_code=atoi(code);

  // Read headers
  pl++;
  char* name;
  int name_len;
  int header_name_id;
  char* value=0;
  char* transfer_encoding=0;
  // last header must be nulled
  nxweb_http_header* header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
  while (pl<end_of_headers) {
    name=pl;
    pl=strchr(pl, '\n');
    if (pl) *pl++='\0';
    else pl=end_of_headers;

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
    name_len=value-name;
    *value++='\0';
    value=nxweb_trunc_space(value);

    header_name_id=identify_http_header(name, name_len);
    switch (header_name_id) {
      case NXWEB_HTTP_CONTENT_TYPE: resp->content_type=value; break;
      case NXWEB_HTTP_CONTENT_LENGTH: resp->content_length=atol(value); break;
      case NXWEB_HTTP_TRANSFER_ENCODING: transfer_encoding=value; break;
      case NXWEB_HTTP_CONNECTION: resp->keep_alive=!nx_strcasecmp(value, "keep-alive"); break;
      case NXWEB_HTTP_KEEP_ALIVE: /* skip */ break;
      default:
        header=nxb_alloc_obj(nxb, sizeof(nxweb_http_header));
        header->name=name;
        header->value=value;
        break;
    }
  }
  resp->headers=header;

  if (transfer_encoding && !nx_strcasecmp(transfer_encoding, "chunked")) {
    resp->chunked_encoding=1;
    resp->content_length=-1;
  }
  else if (resp->keep_alive && resp->content_length==-1) {
    resp->content_length=0; // until-close not allowed in keep-alive mode
  }

  return 0;
}


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

void _nxweb_register_printf_extensions() __attribute__((constructor));

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
