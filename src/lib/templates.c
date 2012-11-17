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

void nxt_init(nxt_context* ctx, nxb_buffer* nxb) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->nxb=nxb;
}

void nxt_content_loaded(nxt_context* ctx, struct nxt_file* file) { // this must be called back by the loader

}

enum nxt_cmd nxt_parse_cmd(char* buf, int buf_len, char** args) {
  char *p, *pc, *pe, *pq, *pn;
  char* end=buf+buf_len;
  p=buf;
  while ((unsigned char)*p<=' ' && p<end) p++;
  pc=p;
  while ((unsigned char)*p>' ' && p<end) p++;
  int len=p-pc;
  enum nxt_cmd cmd=NXT_NONE;
  int expect_args=0;
  switch (len) {
    case 3:
      if (memcmp(pc, "raw", 3)==0) cmd=NXT_RAW;
      else if (memcmp(pc, "use", 3)==0) cmd=NXT_USE, expect_args=NXT_MAX_ARGS-1;
      break;
    case 5:
      if (memcmp(pc, "block", 5)==0) cmd=NXT_BLOCK, expect_args=1;
      break;
    case 6:
      if (memcmp(pc, "endraw", 6)==0) cmd=NXT_ENDRAW;
      else if (memcmp(pc, "parent", 6)==0) cmd=NXT_INCLUDE_PARENT;
      break;
    case 7:
      if (memcmp(pc, "extends", 7)==0) cmd=NXT_USE, expect_args=NXT_MAX_ARGS-1;
      else if (memcmp(pc, "include", 7)==0) cmd=NXT_INCLUDE, expect_args=1;
      break;
    case 8:
      if (memcmp(pc, "endblock", 8)==0) cmd=NXT_ENDBLOCK;
      break;
  }
  if (args) memset(args, 0, sizeof(char*)*NXT_MAX_ARGS*2);
  int arg_idx=0;
  while (arg_idx<expect_args) {
    while ((unsigned char)*p<=' ' && p<end) p++;
    if (p==end) break;
    char q=*p;
    if (q=='"' || q=='\'') { // quoted string
      pq=memchr(p+1, q, end-p-1);
      if (!pq) break; // error
      args[2*arg_idx]=p+1;
      args[2*arg_idx+1]=pq;
      p=pq+1;
    }
    else {
      args[2*arg_idx]=p;
      while ((unsigned char)*p>' ' && p<end) p++;
      args[2*arg_idx+1]=p;
    }
    arg_idx++;
  }
  return cmd;
}

enum nxt_cmd nxt_get_next_cmd(nxt_file* file, int *raw_mode, char** ptr, char** text, int* text_len, char** args) {
  if (!file->content_length) return NXT_EOF;
  nxt_context* ctx=file->ctx;
  char* end=file->content+file->content_length;
  char *pb, *pc, *pd;
  pb=*ptr? *ptr+1 : file->content+1;
REPEAT:
  if (pb>=end) return NXT_EOF;
  for (;;) {
    pc=memchr(pb, '%', end-pb);
    if (!pc) goto NOT_FOUND;
    if (pc && *(pc-1)=='{') {
      char* pe=pc+1;
      for (;;) {
        pd=memchr(pe, '}', end-pe);
        if (!pd) goto NOT_FOUND;
        if (pd && *(pd-1)=='%') {
          // found cmd [pc-1 : pd+1)
          goto FOUND_CMD;
        }
        pe=pd+2;
      }
    }
    pb=pc+2;
  }

  enum nxt_cmd cmd;
FOUND_CMD:
  cmd=nxt_parse_cmd(pc+1, (pd-pc)-2, args);
  if (*raw_mode) {
    if (cmd!=NXT_ENDRAW) {
      pb=pc+2;
      goto REPEAT;
    }
    *raw_mode=0;
    cmd=NXT_NONE;
  }
  else {
    if (cmd==NXT_RAW) {
      cmd=NXT_NONE;
      *raw_mode=1;
    }
  }
  *text=*ptr? *ptr : file->content;
  if (text_len) *text_len=(pc-1)-*text;
  *ptr=pd+1;
  return cmd;

NOT_FOUND:
  *text=*ptr? *ptr : file->content;
  if (text_len) *text_len=(pc-1)-*text;
  *ptr=end;
  return NXT_NONE;
}

void nxt_parse(nxt_context* ctx, const char* uri, char* buf, int buf_len) {
  char* args[NXT_MAX_ARGS*2]; // start and end pointers for each arg; ends with a pair of null-pointers
  char* ptr=0;
  char* text;
  int text_len;
  int raw_mode=0;
  nxt_file* file=nxb_calloc_obj(ctx->nxb, sizeof(*file));
  file->ctx=ctx;
  file->uri=uri;
  file->content=buf;
  file->content_length=buf_len;
  enum nxt_cmd cmd=NXT_NONE;
  for (;;) {
    cmd=nxt_get_next_cmd(file, &raw_mode, &ptr, &text, &text_len, args);
    if (cmd==NXT_EOF) break;
    nxweb_log_error("nxt_parse: cmd=%d text=%.*s arg1=%.*s arg2=%.*s", cmd, text_len, text, (int)(args[1]-args[0]), args[0], (int)(args[3]-args[2]), args[2]);
  }
}

void nxt_merge(nxt_context* ctx, const char* uri, char* buf, int buf_len) {

}

void nxt_serialize(nxt_context* ctx, const char* uri, char* buf, int buf_len) {

}
