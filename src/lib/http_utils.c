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
  // eg. Tue, 24 Jan 2012 13:05:54 GMT (max 29 chars)
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
  uint_to_decimal_string_zeropad(tm->tm_hour, p, 2, 0);
  p+=2;
  *p++=':';
  uint_to_decimal_string_zeropad(tm->tm_min, p, 2, 0);
  p+=2;
  *p++=':';
  uint_to_decimal_string_zeropad(tm->tm_sec, p, 2, 0);
  p+=2;
  *p++=' ';
  *p++='G';
  *p++='M';
  *p++='T';
  *p='\0';
  return p-buf;
}

time_t nxweb_parse_http_time(const char* str) { // must be GMT
  // eg. Tue, 24 Jan 2012 13:05:54 GMT
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  const char* p=str+3;
  if (*p++!=',') return 0;
  if (*p++!=' ') return 0;
  tm.tm_mday=strtol(p, (char**)&p, 10);
  if (tm.tm_mday<=0 || tm.tm_mday>31) return 0;
  if (*p++!=' ') return 0;
  switch (*p++) {
    case 'J':
      if (*p=='a') {
        p++;
        if (*p++!='n') return 0;
        tm.tm_mon=0;
        break;
      }
      if (*p++!='u') return 0;
      if (*p=='n') tm.tm_mon=5;
      else if (*p=='l') tm.tm_mon=6;
      else return 0;
      p++;
      break;
    case 'F':
      tm.tm_mon=1;
      if (*p++!='e') return 0;
      if (*p++!='b') return 0;
      break;
    case 'M':
      if (*p++!='a') return 0;
      if (*p=='y') tm.tm_mon=4;
      else if (*p=='r') tm.tm_mon=2;
      else return 0;
      p++;
      break;
    case 'A':
      if (*p=='p') {
        p++;
        if (*p++!='r') return 0;
        tm.tm_mon=3;
        break;
      }
      if (*p=='u') {
        p++;
        if (*p++!='g') return 0;
        tm.tm_mon=7;
        break;
      }
      return 0;
    case 'S':
      tm.tm_mon=8;
      if (*p++!='e') return 0;
      if (*p++!='p') return 0;
      break;
    case 'O':
      tm.tm_mon=9;
      if (*p++!='c') return 0;
      if (*p++!='t') return 0;
      break;
    case 'N':
      tm.tm_mon=10;
      if (*p++!='o') return 0;
      if (*p++!='v') return 0;
      break;
    case 'D':
      tm.tm_mon=11;
      if (*p++!='e') return 0;
      if (*p++!='c') return 0;
      break;
    default:
      return 0;
  }
  if (*p++!=' ') return 0;
  tm.tm_year=strtol(p, (char**)&p, 10)-1900;
  if (tm.tm_year<0 || tm.tm_year>1000) return 0; // from 1900 to 2900
  if (*p++!=' ') return 0;
  tm.tm_hour=strtol(p, (char**)&p, 10);
  if (tm.tm_hour<0 || tm.tm_hour>=24) return 0;
  if (*p++!=':') return 0;
  tm.tm_min=strtol(p, (char**)&p, 10);
  if (tm.tm_min<0 || tm.tm_min>=60) return 0;
  if (*p++!=':') return 0;
  tm.tm_sec=strtol(p, (char**)&p, 10);
  if (tm.tm_sec<0 || tm.tm_sec>=60) return 0;
  if (*p++!=' ') return 0;
  if (*p++!='G') return 0;
  if (*p++!='M') return 0;
  if (*p++!='T') return 0;
  tm.tm_isdst=-1; // auto-detect
  time_t t=timegm(&tm); // mktime(&tm) - timezone;
  if (t==-1) return 0;
  return t;
}

int nxweb_format_iso8601_time(char* buf, struct tm* tm) { // ISO 8601
  // eg. 2012-01-24T13:05:54 (19 chars)
  char* p=buf;
  uint_to_decimal_string_zeropad(tm->tm_year+1900, p, 4, 0);
  p+=4;
  *p++='-';
  uint_to_decimal_string_zeropad(tm->tm_mon+1, p, 2, 0);
  p+=2;
  *p++='-';
  uint_to_decimal_string_zeropad(tm->tm_mday, p, 2, 0);
  p+=2;
  *p++='T';
  uint_to_decimal_string_zeropad(tm->tm_hour, p, 2, 0);
  p+=2;
  *p++=':';
  uint_to_decimal_string_zeropad(tm->tm_min, p, 2, 0);
  p+=2;
  *p++=':';
  uint_to_decimal_string_zeropad(tm->tm_sec, p, 2, 0);
  p+=2;
  *p='\0';
  assert(p-buf==19);
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
  nxweb_http_parameter* param_map=0;
  nxweb_http_parameter* param;
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
        param=nxb_calloc_obj(nxb, sizeof(nxweb_http_parameter));
        param->name=name;
        param->value=value;
        param_map=nx_simple_map_add(param_map, param);
      }
    }
  }
  if (req->content && req->content_type && nx_strcasecmp(req->content_type, "application/x-www-form-urlencoded")==0) {
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
        param=nxb_calloc_obj(nxb, sizeof(nxweb_http_parameter));
        param->name=name;
        param->value=value;
        param_map=nx_simple_map_add(param_map, param);
      }
    }
    req->content=0;
  }
  req->parameters=param_map;
}

// Modifies conn->cookie content (does url_decode inplace)
void nxweb_parse_request_cookies(nxweb_http_request *req) {

  if (req->cookies) return; // already parsed

  nxb_buffer* nxb=req->nxb;
  char *name, *value, *next;
  // last cookie must be nulled (nx_buffer allocates objects in reverse direction)
  nxweb_http_cookie* cookie_map=0;
  nxweb_http_cookie* cookie;
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
        cookie=nxb_calloc_obj(nxb, sizeof(nxweb_http_cookie));
        cookie->name=name;
        cookie->value=value;
        cookie_map=nx_simple_map_add(cookie_map, cookie);
      }
    }
    req->cookie=0;
  }
  req->cookies=cookie_map;
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
  NXWEB_HTTP_ETAG,
  NXWEB_HTTP_RANGE,
  NXWEB_HTTP_COOKIE,
  NXWEB_HTTP_EXPECT,
  NXWEB_HTTP_SERVER,
  NXWEB_HTTP_EXPIRES,
  NXWEB_HTTP_TRAILER,
  NXWEB_HTTP_CONNECTION,
  NXWEB_HTTP_KEEP_ALIVE,
  NXWEB_HTTP_USER_AGENT,
  NXWEB_HTTP_CONTENT_TYPE,
  NXWEB_HTTP_LAST_MODIFIED,
  NXWEB_HTTP_CACHE_CONTROL,
  NXWEB_HTTP_ACCEPT_RANGES,
  NXWEB_HTTP_CONTENT_LENGTH,
  NXWEB_HTTP_ACCEPT_ENCODING,
  NXWEB_HTTP_IF_MODIFIED_SINCE,
  NXWEB_HTTP_TRANSFER_ENCODING,
  NXWEB_HTTP_X_NXWEB_SSI,
  NXWEB_HTTP_X_NXWEB_TEMPLATES
};

static int identify_http_header(const char* name, int name_len) {
  if (!name_len) name_len=strlen(name);
  char first_char=nx_tolower(*name);
  switch (name_len) {
    case 4:
      if (first_char=='h') return nx_strcasecmp(name, "Host")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_HOST;
      if (first_char=='d') return nx_strcasecmp(name, "Date")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_DATE;
      if (first_char=='e') return nx_strcasecmp(name, "ETag")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_ETAG;
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
      if (first_char=='e') return nx_strcasecmp(name, "Expires")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_EXPIRES;
      return NXWEB_HTTP_UNKNOWN;
    case 10:
      if (first_char=='c') return nx_strcasecmp(name, "Connection")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CONNECTION;
      if (first_char=='k') return nx_strcasecmp(name, "Keep-Alive")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_KEEP_ALIVE;
      if (first_char=='u') return nx_strcasecmp(name, "User-Agent")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_USER_AGENT;
      return NXWEB_HTTP_UNKNOWN;
    case 11:
      if (first_char=='x') return nx_strcasecmp(name, "X-NXWEB-SSI")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_X_NXWEB_SSI;
      return NXWEB_HTTP_UNKNOWN;
    case 12:
      if (first_char=='c') return nx_strcasecmp(name, "Content-Type")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CONTENT_TYPE;
      return NXWEB_HTTP_UNKNOWN;
    case 13:
      if (first_char=='c') return nx_strcasecmp(name, "Cache-Control")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CACHE_CONTROL;
      if (first_char=='l') return nx_strcasecmp(name, "Last-Modified")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_LAST_MODIFIED;
      if (first_char=='a') return nx_strcasecmp(name, "Accept-Ranges")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_ACCEPT_RANGES;
      return NXWEB_HTTP_UNKNOWN;
    case 14:
      if (first_char=='c') return nx_strcasecmp(name, "Content-Length")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_CONTENT_LENGTH;
      return NXWEB_HTTP_UNKNOWN;
    case 15:
      if (first_char=='a') return nx_strcasecmp(name, "Accept-Encoding")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_ACCEPT_ENCODING;
      return NXWEB_HTTP_UNKNOWN;
    case 17:
      if (first_char=='t') return nx_strcasecmp(name, "Transfer-Encoding")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_TRANSFER_ENCODING;
      if (first_char=='i') return nx_strcasecmp(name, "If-Modified-Since")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_IF_MODIFIED_SINCE;
      if (first_char=='x') return nx_strcasecmp(name, "X-NXWEB-Templates")? NXWEB_HTTP_UNKNOWN : NXWEB_HTTP_X_NXWEB_TEMPLATES;
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
    nx_strntolower(host, host+7, host_len); // memmove(host, host+7, host_len);
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
  nxweb_http_header* header_map=0;
  nxweb_http_header* header;
  while (pl<end_of_headers) {
    name=pl;
    pl=strchr(pl, '\n');
    if (pl) *pl++='\0';
    else pl=end_of_headers;

    if (*name && (unsigned char)*name<=SPACE) {
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
      case NXWEB_HTTP_HOST: nx_strtolower(value, value); req->host=value; break;
      case NXWEB_HTTP_EXPECT: expect=value; break;
      case NXWEB_HTTP_COOKIE: req->cookie=value; break;
      case NXWEB_HTTP_USER_AGENT: req->user_agent=value; break;
      case NXWEB_HTTP_CONTENT_TYPE: req->content_type=value; break;
      case NXWEB_HTTP_CONTENT_LENGTH: req->content_length=atol(value); break;
      case NXWEB_HTTP_ACCEPT_ENCODING: req->accept_encoding=value; break;
      case NXWEB_HTTP_TRANSFER_ENCODING: req->transfer_encoding=value; break;
      case NXWEB_HTTP_IF_MODIFIED_SINCE: req->if_modified_since=nxweb_parse_http_time(value); break;
      case NXWEB_HTTP_CONNECTION: req->keep_alive=!nx_strcasecmp(value, "keep-alive"); break;
      case NXWEB_HTTP_RANGE: req->range=value; break;
      case NXWEB_HTTP_TRAILER: return -2; // not implemented
      default:
        header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
        header->name=name;
        header->value=value;
        header_map=nx_simple_map_add(header_map, header);
        break;
    }
  }
  req->headers=header_map;

  if (!req->host || !*req->host) return -1; // host is required

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

void _nxweb_encode_chunked_init(nxweb_chunked_encoder_state* encoder_state) {
  encoder_state->chunk_size=0;
  assert(sizeof(encoder_state->buf)==8);
  memcpy(encoder_state->buf, "\r\n0000\r\n", sizeof(encoder_state->buf));
  encoder_state->pos=2;
  encoder_state->header_prepared=0;
  encoder_state->final_chunk=0;
}

int _nxweb_encode_chunked_stream(nxweb_chunked_encoder_state* encoder_state, nxe_size_t* offered_size, void** send_ptr, nxe_size_t* send_size, nxe_flags_t* flags) {
  if (encoder_state->final_chunk) {
    assert(*flags&NXEF_EOF);
    *send_size=7-encoder_state->pos;
    *send_ptr=encoder_state->buf+encoder_state->pos;
    return 1;
  }
  else if (encoder_state->pos<=2 && !encoder_state->header_prepared) { // can start new chunk
    if (!*offered_size) {
      if (*flags&NXEF_EOF) {
        encoder_state->final_chunk=1;
        memcpy(encoder_state->buf+2, "0\r\n\r\n", 5);
        *send_size=7-encoder_state->pos;
        *send_ptr=encoder_state->buf+encoder_state->pos;
        return 1;
      }
      else {
        // do not send final chunk until eof is set
        *send_size=2-encoder_state->pos;
        *send_ptr=encoder_state->buf+encoder_state->pos;
        return !!*send_size;
      }
    }
    else {
      if (*offered_size > 0xffff) *offered_size=0xffff; // max chunk size
      encoder_state->chunk_size=*offered_size;
      uint_to_hex_string_zeropad(*offered_size, encoder_state->buf+2, 4, 0);
      *send_size=sizeof(encoder_state->buf) - encoder_state->pos;
      *send_ptr=encoder_state->buf+encoder_state->pos;
      *flags&=~NXEF_EOF;
      encoder_state->header_prepared=1;
      return 1;
    }
  }
  else if (encoder_state->pos<sizeof(encoder_state->buf)) { // still inside header
    assert(*offered_size >= encoder_state->chunk_size); // can't reduce offered_size
    if (*offered_size > encoder_state->chunk_size) *offered_size=encoder_state->chunk_size; // stick to previously defined chunk size
    *send_size=sizeof(encoder_state->buf) - encoder_state->pos;
    *send_ptr=encoder_state->buf+encoder_state->pos;
    *flags&=~NXEF_EOF;
    return 1;
  }
  else { // sending chunk data
    nxe_ssize_t chunk_bytes_left=encoder_state->chunk_size - (encoder_state->pos - sizeof(encoder_state->buf));
    assert(*offered_size >= chunk_bytes_left); // can't reduce offered_size
    if (*offered_size > chunk_bytes_left) *offered_size=chunk_bytes_left;
    *send_size=0;
    *send_ptr=0;
    *flags&=~NXEF_EOF;
    return 0;
  }
}

void _nxweb_encode_chunked_advance(nxweb_chunked_encoder_state* encoder_state, nxe_ssize_t pos_delta) {
  if (!pos_delta) return;
  encoder_state->pos+=pos_delta;
  if (encoder_state->pos==sizeof(encoder_state->buf)+encoder_state->chunk_size) {
    encoder_state->pos=0;
    encoder_state->header_prepared=0;
  }
}

int _nxweb_encode_chunked_is_complete(nxweb_chunked_encoder_state* encoder_state) {
  return encoder_state->final_chunk && encoder_state->pos==7;
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

nxweb_http_request_data* nxweb_find_request_data(nxweb_http_request* req, nxe_data key) {
  nxweb_http_request_data* rdata=req->data_chain;
  while (rdata) {
    if (rdata->key.cptr && rdata->key.cptr==key.cptr) break; // null keys never found
    rdata=rdata->next;
  }
  return rdata;
}

void nxweb_set_request_data(nxweb_http_request* req, nxe_data key, nxe_data value, nxweb_http_request_data_finalizer finalize) {
  // key can be null, meaning that only finalizer is installed
  nxweb_http_request_data* rdata=nxweb_find_request_data(req, key);
  if (!rdata) {
    rdata=nxb_calloc_obj(req->nxb, sizeof(nxweb_http_request_data));
    rdata->next=req->data_chain;
    req->data_chain=rdata;
    rdata->key=key;
  }
  rdata->value=value;
  rdata->finalize=finalize;
}

nxe_data nxweb_get_request_data(nxweb_http_request* req, nxe_data key) {
  nxweb_http_request_data* rdata=nxweb_find_request_data(req, key);
  return rdata? rdata->value : (nxe_data)0;
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
  nx_simple_map_entry* header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
  header->name=name;
  header->value=value;
  resp->headers=nx_simple_map_add(resp->headers, header);
}

void nxweb_add_response_header_safe(nxweb_http_response* resp, const char* name, const char* value) {
  int header_name_id=identify_http_header(name, strlen(name));
  nxb_buffer* nxb=resp->nxb;
  switch (header_name_id) {
    case NXWEB_HTTP_CONTENT_TYPE: resp->content_type=nxb_copy_str(nxb, value); break;
    case NXWEB_HTTP_CONTENT_LENGTH: /* skip */ break;
    case NXWEB_HTTP_TRANSFER_ENCODING: /* skip */ break;
    case NXWEB_HTTP_CONNECTION: /* skip */ break;
    case NXWEB_HTTP_KEEP_ALIVE: /* skip */ break;
    case NXWEB_HTTP_X_NXWEB_SSI: resp->ssi_on=!nx_strcasecmp(value, "ON"); break;
    case NXWEB_HTTP_X_NXWEB_TEMPLATES: resp->templates_on=!nx_strcasecmp(value, "ON"); break;
    case NXWEB_HTTP_DATE: /* skip */ break;
    case NXWEB_HTTP_LAST_MODIFIED: resp->last_modified=nxweb_parse_http_time(value); break;
    case NXWEB_HTTP_EXPIRES: resp->expires=nxweb_parse_http_time(value); break;
    case NXWEB_HTTP_CACHE_CONTROL:
      if (*value) {
        char* p1=nxb_copy_str(nxb, value);
        char *p, *cc_name, *cc_value;
        _Bool dirty=0;
        while (p1) {
          p=strchr(p1, ',');
          if (p) *p++='\0';
          while (*p1 && (unsigned char)*p1<=SPACE) p1++;
          cc_name=p1;
          cc_value=strchr(p1, '=');
          if (cc_value) {
            *cc_value++='\0';
            cc_value=nxweb_trunc_space(cc_value);
          }
          if (!nx_strcasecmp(cc_name, "no-cache")) resp->no_cache=1;
          else if (!nx_strcasecmp(cc_name, "private")) resp->cache_private=1;
          else if (!nx_strcasecmp(cc_name, "max-age") && cc_value) {
            if (cc_value[0]=='0' && !cc_value[1]) resp->max_age=-1;
            else resp->max_age=atol(cc_value);
          }
          else dirty=1;
          p1=p;
        }
        if (dirty) { // there is something we could not recognize
          resp->cache_control=nxb_copy_str(nxb, value);
        }
      }
      break;
    case NXWEB_HTTP_ETAG: resp->etag=nxb_copy_str(nxb, value); break;
    default:
      {
        nx_simple_map_entry* header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
        header->name=nxb_copy_str(nxb, name);
        header->value=nxb_copy_str(nxb, value);
        resp->headers=nx_simple_map_add(resp->headers, header);
        break;
      }
  }
}

void _nxweb_add_extra_response_headers(nxb_buffer* nxb, nxweb_http_header *headers) {
  nx_simple_map_entry* itr;
  const char* name;
  int header_name_id;
  for (itr=nx_simple_map_itr_begin(headers); itr; itr=nx_simple_map_itr_next(itr)) {
    name=itr->name;
    header_name_id=identify_http_header(name, 0);
    switch (header_name_id) {
      case NXWEB_HTTP_CONNECTION:
      case NXWEB_HTTP_SERVER:
      case NXWEB_HTTP_CONTENT_TYPE:
      case NXWEB_HTTP_CONTENT_LENGTH:
      case NXWEB_HTTP_TRANSFER_ENCODING:
      case NXWEB_HTTP_DATE:
      case NXWEB_HTTP_CACHE_CONTROL:
      case NXWEB_HTTP_EXPIRES:
      case NXWEB_HTTP_LAST_MODIFIED:
      case NXWEB_HTTP_ETAG:
      case NXWEB_HTTP_ACCEPT_RANGES: // it is not right to always filter this out; file cache filter requires this
        // skip these specific headers
        continue;
    }

    nxb_append_str(nxb, name);
    nxb_append(nxb, ": ", 2);
    nxb_append_str(nxb, itr->value);
    nxb_append_str(nxb, "\r\n");
  }
}

void _nxweb_prepare_response_headers(nxe_loop* loop, nxweb_http_response *resp) {
  char buf[32];
  struct tm tm;

  nxb_buffer* nxb=resp->nxb;
  nxb_start_stream(nxb);

  _Bool must_not_have_body=(resp->status_code==304 || resp->status_code==204 || resp->status_code==205);
  if (must_not_have_body) {
    if (resp->content_length) nxweb_log_warning("content_length specified for response that must not contain entity body");
    if (resp->gzip_encoded) nxweb_log_warning("gzip encoding specified for response that must not contain entity body");
  }

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
  nxb_append_str_fast(nxb, nxe_get_current_http_time_str(loop));
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, resp->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\n");

  if (resp->headers) {
    // write added headers
    _nxweb_add_extra_response_headers(nxb, resp->headers);
  }
  if (resp->extra_raw_headers) {
    nxb_append_str(nxb, resp->extra_raw_headers);
  }
  if (resp->content_length) {
    nxb_append_str(nxb, "Content-Type: ");
    nxb_append_str(nxb, resp->content_type? resp->content_type : "text/html");
    if (resp->content_charset) {
      nxb_append_str(nxb, "; charset=");
      nxb_append_str(nxb, resp->content_charset);
    }
    nxb_append_str(nxb, "\r\n");
    if (resp->gzip_encoded) {
      nxb_append_str(nxb, "Content-Encoding: gzip\r\n");
    }
  }
  if (resp->last_modified) {
    gmtime_r(&resp->last_modified, &tm);
    nxb_make_room(nxb, 48);
    nxb_append_str_fast(nxb, "Last-Modified: ");
    // strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);
    // nxb_append_str(nxb, date_buf);
    nxb_blank_fast(nxb, nxweb_format_http_time(nxb_get_room(nxb, 0), &tm));
    nxb_append_str_fast(nxb, "\r\n");
  }
  if (resp->etag) {
    nxb_append_str(nxb, "ETag: ");
    nxb_append_str(nxb, resp->etag);
    nxb_append(nxb, "\r\n", 2);
  }
  if (resp->expires) {
    gmtime_r(&resp->expires, &tm);
    nxb_make_room(nxb, 42);
    nxb_append_str_fast(nxb, "Expires: ");
    // strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %T %Z", &tm);
    // nxb_append_str(nxb, date_buf);
    nxb_blank_fast(nxb, nxweb_format_http_time(nxb_get_room(nxb, 0), &tm));
    nxb_append_str_fast(nxb, "\r\n");
  }
  if (resp->cache_control) {
    nxb_append_str(nxb, "Cache-Control: ");
    nxb_append_str(nxb, resp->cache_control);
    nxb_append(nxb, "\r\n", 2);
  }
  else if (resp->no_cache || resp->cache_private || resp->max_age) {
    _Bool comma=0;
    nxb_append_str(nxb, "Cache-Control: ");
    if (resp->cache_private) {
      if (comma) nxb_append(nxb, ", ", 2);
      nxb_append_str(nxb, "private");
      comma=1;
    }
    if (resp->no_cache) {
      if (comma) nxb_append(nxb, ", ", 2);
      nxb_append_str(nxb, "no-cache");
      comma=1;
    }
    if (resp->max_age) {
      if (comma) nxb_append(nxb, ", ", 2);
      nxb_append_str(nxb, "max-age=");
      if (resp->max_age==-1) { // special case => response is cacheable but must be revalidated every time
        nxb_append_char(nxb, '0');
      }
      else {
        nxb_append_str(nxb, uint_to_decimal_string(resp->max_age, buf, sizeof(buf)));
      }
    }
    nxb_append(nxb, "\r\n", 2);
  }
  if (resp->content_length || !must_not_have_body) {
    nxb_make_room(nxb, 48);
    if (resp->content_length>=0) {
      nxb_append_str_fast(nxb, "Content-Length: ");
      nxb_append_str_fast(nxb, uint_to_decimal_string(resp->content_length, buf, sizeof(buf)));
    }
    else {
      nxb_append_str_fast(nxb, "Transfer-Encoding: chunked");
    }
    nxb_append_fast(nxb, "\r\n", 2);
  }
  nxb_append(nxb, "\r\n", 3); // add null-terminator

  resp->raw_headers=nxb_finish_stream(nxb, 0);
}

void nxweb_send_redirect(nxweb_http_response *resp, int code, const char* location, int secure) {
  nxweb_send_redirect2(resp, code, location, 0, secure);
}

void nxweb_send_redirect2(nxweb_http_response *resp, int code, const char* location, const char* location_path_info, int secure) {
  char buf[32];

  nxb_buffer* nxb=resp->nxb;
  nxb_start_stream(nxb);

  resp->status_code=code;
  resp->status=code==302? "Found":(code==301? "Moved Permanently":"Redirect");
  resp->content=0;
  resp->content_type=0;
  resp->content_length=0;
  resp->sendfile_path=0;
  if (resp->sendfile_fd>0) {
    close(resp->sendfile_fd);
  }
  resp->sendfile_fd=0;
  resp->content_out=0;

  nxb_make_room(nxb, 250);
  nxb_append_fast(nxb, "HTTP/1.", 7);
  nxb_append_char_fast(nxb, resp->http11? '1':'0');
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str_fast(nxb, uint_to_decimal_string(code, buf, sizeof(buf)));
  nxb_append_char_fast(nxb, ' ');
  nxb_append_str(nxb, resp->status);
  nxb_append_str_fast(nxb, "\r\n"
                      "Server: nxweb/" REVISION "\r\n"
                      "Date: ");
  nxb_append_str_fast(nxb, nxe_get_current_http_time_str(_nxweb_net_thread_data->loop));
  nxb_append_str_fast(nxb, "\r\n"
                      "Connection: ");
  nxb_append_str_fast(nxb, resp->keep_alive?"keep-alive":"close");
  nxb_append_str_fast(nxb, "\r\nContent-Length: 0\r\nLocation: ");
  if (!strncmp(location, "http://", 7) || !strncmp(location, "https://", 7)) { // absolute uri
    nxb_append_str(nxb, location);
  }
  else {
    nxb_append_str(nxb, "http");
    if (secure) nxb_append_char(nxb, 's');
    nxb_append_str(nxb, "://");
    nxb_append_str(nxb, resp->host);
    nxb_append_str(nxb, location);
  }
  if (location_path_info) nxb_append_str(nxb, location_path_info);
  nxb_append(nxb, "\r\n\r\n", 5); // add null-terminator

  resp->raw_headers=nxb_finish_stream(nxb, 0);
}

void nxweb_send_http_error(nxweb_http_response *resp, int code, const char* message) {
  static const char* response_body1="<html>\n<head><title>";
  static const char* response_body2="</title></head>\n<body>\n<h1>";
  static const char* response_body3="</h1>\n<p>nxweb/" REVISION "</p>\n</body>\n</html>";
  nxweb_set_response_status(resp, code, message);
  nxb_buffer* nxb=resp->nxb;
  nxb_start_stream(nxb);
  nxb_append_str(nxb, response_body1);
  nxb_append_str(nxb, message);
  nxb_append_str(nxb, response_body2);
  nxb_append_str(nxb, message);
  nxb_append_str(nxb, response_body3);
  int size;
  resp->content=nxb_finish_stream(nxb, &size);
  resp->content_length=size;
  resp->content_type="text/html";
  resp->sendfile_path=0;
  if (resp->sendfile_fd>0) {
    close(resp->sendfile_fd);
  }
  resp->sendfile_fd=0;
  resp->content_out=0;
  //resp->keep_alive=0; // close connection after error response
}

int nxweb_send_file(nxweb_http_response *resp, char* fpath, const struct stat* finfo, int gzip_encoded, off_t offset, size_t size, const nxweb_mime_type* mtype, const char* charset) {
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
  if (!finfo || !finfo->st_ino) {
    if (stat(fpath, &resp->sendfile_info)==-1) return -1;
    finfo=&resp->sendfile_info;
  }
  if (S_ISDIR(finfo->st_mode)) {
    return -2;
  }
  if (!S_ISREG(finfo->st_mode)) {
    return -3;
  }

  //int fd=open(fpath, O_RDONLY|O_NONBLOCK);
  //if (fd==-1) return -1;
  resp->sendfile_path=fpath;
  //resp->sendfile_fd=fd;
  resp->sendfile_offset=offset;
  resp->content_length=size? size : finfo->st_size-offset;
  resp->sendfile_end=offset+resp->content_length;
  resp->last_modified=finfo->st_mtime;
  resp->gzip_encoded=gzip_encoded;
  if (!mtype) {
    int flen;
    int ends_with_gz=0;
    if (gzip_encoded) {
      flen=strlen(fpath);
      ends_with_gz=(fpath[flen-3]=='.' && fpath[flen-2]=='g' && fpath[flen-1]=='z');
      if (ends_with_gz) fpath[flen-3]='\0'; // cut .gz
    }
    mtype=nxweb_get_mime_type_by_ext(fpath);
    if (ends_with_gz) fpath[flen-3]='.'; // restore
  }
  resp->sendfile_info=*finfo;
  resp->mtype=mtype;
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
  nxb_start_stream(nxb);

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

  if (req->if_modified_since) {
    struct tm tm;
    gmtime_r(&req->if_modified_since, &tm);
    nxb_make_room(nxb, 52);
    nxb_append_str_fast(nxb, "If-Modified-Since: ");
    nxb_blank_fast(nxb, nxweb_format_http_time(nxb_get_room(nxb, 0), &tm));
    nxb_append_str_fast(nxb, "\r\n");
  }

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

  if (req->uid) {
    nxb_append_str(nxb, "X-NXWEB-Request-ID: ");
    nxb_append_uint64_hex_zeropad(nxb, req->uid, 16);
    nxb_append_str(nxb, "\r\n");
  }

  if (req->parent_req) {
    nxweb_http_request* preq=req->parent_req;
    while (preq->parent_req) preq=preq->parent_req; // find root request
    if (preq->uid) {
      nxb_append_str(nxb, "X-NXWEB-Root-Request-ID: ");
      nxb_append_uint64_hex_zeropad(nxb, preq->uid, 16);
      nxb_append_str(nxb, "\r\n");
    }
  }

  if (req->user_agent) {
    nxb_append_str(nxb, "User-Agent: ");
    nxb_append_str(nxb, req->user_agent);
    nxb_append_str(nxb, "\r\n");
  }
  else {
    nxb_append_str(nxb, "User-Agent: nxweb/" REVISION "\r\n");
  }

  if (req->cookie) {
    nxb_append_str(nxb, "Cookie: ");
    nxb_append_str(nxb, req->cookie);
    nxb_append_str(nxb, "\r\n");
  }

  if (req->headers) {
    // write added headers
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->headers); itr; itr=nx_simple_map_itr_next(itr)) {
      nxb_append_str(nxb, itr->name);
      nxb_append_char(nxb, ':');
      nxb_append_char(nxb, ' ');
      nxb_append_str(nxb, itr->value);
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
  nxb_append_fast(nxb, "\r\n", 3); // add null-terminator

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
  nxweb_http_header* header_map=0;
  nxweb_http_header* header;
  while (pl<end_of_headers) {
    name=pl;
    pl=strchr(pl, '\n');
    if (pl) *pl++='\0';
    else pl=end_of_headers;

    if (*name && (unsigned char)*name<=SPACE) {
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
      case NXWEB_HTTP_X_NXWEB_SSI: resp->ssi_on=!nx_strcasecmp(value, "ON"); break;
      case NXWEB_HTTP_X_NXWEB_TEMPLATES: resp->templates_on=!nx_strcasecmp(value, "ON"); break;
      case NXWEB_HTTP_DATE: resp->date=nxweb_parse_http_time(value); break;
      case NXWEB_HTTP_LAST_MODIFIED: resp->last_modified=nxweb_parse_http_time(value); break;
      case NXWEB_HTTP_EXPIRES: resp->expires=nxweb_parse_http_time(value); break;
      case NXWEB_HTTP_CACHE_CONTROL: resp->cache_control=value; break;
      case NXWEB_HTTP_ETAG: resp->etag=value; break;
      default:
        header=nxb_calloc_obj(nxb, sizeof(nxweb_http_header));
        header->name=name;
        header->value=value;
        header_map=nx_simple_map_add(header_map, header);
        break;
    }
  }
  resp->headers=header_map;

  if (resp->cache_control) {
    char* p1=nxb_copy_obj(nxb, resp->cache_control, strlen(resp->cache_control)+1);
    char *p, *name, *value;
    while (p1) {
      p=strchr(p1, ',');
      if (p) *p++='\0';
      while (*p1 && (unsigned char)*p1<=SPACE) p1++;
      name=p1;
      value=strchr(p1, '=');
      if (value) {
        *value++='\0';
        value=nxweb_trunc_space(value);
      }
      if (!nx_strcasecmp(name, "no-cache")) resp->no_cache=1;
      else if (!nx_strcasecmp(name, "private")) resp->cache_private=1;
      else if (!nx_strcasecmp(name, "max-age") && value) {
        if (value[0]=='0' && !value[1]) resp->max_age=-1;
        else resp->max_age=atol(value);
      }
      p1=p;
    }
  }

  if (transfer_encoding && !nx_strcasecmp(transfer_encoding, "chunked")) {
    resp->chunked_encoding=1;
    resp->content_length=-1;
  }
  else if (resp->keep_alive && resp->content_length==-1) {
    resp->content_length=0; // until-close not allowed in keep-alive mode
  }

  return 0;
}


static char _CHAR_MAP[256];

#define _CM_LETTER 0x10
#define _CM_DIGIT 0x20
#define _CM_URI_CHAR 0x40
#define _CM_FILE_PATH_CHAR 0x80
#define _CM_HEX_VALUE_MASK 0xf

static void init_char_map() __attribute__((constructor));

static void init_char_map() {
  unsigned c, m;
  _CHAR_MAP[0]=0;
  for (c=1; c<256; c++) {
    m=0;
    if (c>='0' && c<='9') m|=(c-'0')&_CM_HEX_VALUE_MASK;
    if (c>='a' && c<='f') m|=(c-'a'+0xa)&_CM_HEX_VALUE_MASK;
    if (c>='A' && c<='F') m|=(c-'A'+0xa)&_CM_HEX_VALUE_MASK;
    if (c>='0' && c<='9') m|=_CM_DIGIT|_CM_URI_CHAR|_CM_FILE_PATH_CHAR;
    if ((c>='a' && c<='z') || (c>='A' && c<='Z')) m|=_CM_LETTER|_CM_URI_CHAR|_CM_FILE_PATH_CHAR;
    if (strchr(".-_~", c)) m|=_CM_URI_CHAR;
    if (strchr(".-_/", c)) m|=_CM_FILE_PATH_CHAR;
    _CHAR_MAP[c]=m;
  }
}

// already defined in misc.h:
// static inline char HEX_DIGIT(char n) { n&=0xf; return n<10? n+'0' : n-10+'A'; }

static inline char HEX_DIGIT_VALUE(char c) {
  return _CHAR_MAP[(unsigned char)c]&_CM_HEX_VALUE_MASK;
}

static inline int IS_URI_CHAR(char c) {
  return _CHAR_MAP[(unsigned char)c]&_CM_URI_CHAR;
}

static inline int IS_FILE_PATH_CHAR(char c) {
  return _CHAR_MAP[(unsigned char)c]&_CM_FILE_PATH_CHAR;
}

void _nxb_append_escape_url(nxb_buffer* nxb, const char* url) {
  int max_size=strlen(url)*3;
  nxb_make_room(nxb, max_size);

  const char* pt=url;
  char c;
  while ((c=*pt++)) {
    if (IS_URI_CHAR(c)) nxb_append_char_fast(nxb, c);
    else {
      nxb_append_char_fast(nxb, '%');
      nxb_append_char_fast(nxb, HEX_DIGIT(c>>4));
      nxb_append_char_fast(nxb, HEX_DIGIT(c));
    }
  }
}

// allow up to ~22 chars to be appended as extensions
#define MAX_PATH_SEGMENT 230

void _nxb_append_encode_file_path(nxb_buffer* nxb, const char* path) {
  if (!path || !*path) return;
  int path_len=strlen(path);
  int max_size=path_len*3+path_len/MAX_PATH_SEGMENT;
  nxb_make_room(nxb, max_size);

  const char* pt=path;
  char c;
  int fname_len_count=0;
  while ((c=*pt++)) {
    if (c=='/') {
      fname_len_count=0;
      nxb_append_char_fast(nxb, c);
    }
    else {
      if (fname_len_count>=MAX_PATH_SEGMENT) { // break long names into ~MAX_PATH_SEGMENT char segments (ext3/4 limit)
        nxb_append_char_fast(nxb, '/');
        fname_len_count=0;
      }
      if ((c=='.' && fname_len_count==0) || !IS_FILE_PATH_CHAR(c)) {
        nxb_append_char_fast(nxb, '$');
        nxb_append_char_fast(nxb, HEX_DIGIT(c>>4));
        nxb_append_char_fast(nxb, HEX_DIGIT(c));
        fname_len_count+=3; // might go over MAX_PATH_SEGMENT by 2 chars but that is OK
      }
      else {
        nxb_append_char_fast(nxb, c);
        fname_len_count++;
      }
    }
  }
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

int nxweb_remove_dots_from_uri_path(char* path) { // returns 0=OK
  if (!*path) return 0; // end of path
  if (*path!='/') return -1; // invalid path

  char* src=path+1; // skip first '/'
  char* dst=src;

  while (*src) {
    // process path segment
    if (*src=='/') { // two '/' in a row => skip
      src++;
      continue;
    }
    if (*src=='.') {
      switch (src[1]) {
        case '/': src+=2; continue;
        case '\0': src++; continue;
        case '.':
          if (src[2]=='/' || src[2]=='\0') { // /..(/.*)?$
            if (dst==path+1) { // we are at root already
              return -1; // invalid path
            }
            // cut last path segment from dst
            while ((--dst)[-1] != '/');
            src+=3;
            continue;
          }
          break;
      }
    }
    // copy segment
    *dst++=*src++;
    while (*src && *src!='/') {
      *dst++=*src++;
    }
    if (!(*dst++=*src++)) break;
  }
  return 0;
}
