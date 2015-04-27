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

#define BN_NONE_ID (-1)
#define BN_PARENT_ID (-2)

//#define MISSING_BLOCK_STUB "<!--[missing block]-->"
#define MISSING_BLOCK_STUB ""

void nxt_init(nxt_context* ctx, nxb_buffer* nxb, nxt_loader loader, nxe_data loader_data) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->nxb=nxb;
  ctx->load=loader;
  ctx->loader_data=loader_data;
}

static enum nxt_cmd nxt_parse_cmd(char* buf, int buf_len, char** args) {
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

static enum nxt_cmd nxt_get_next_cmd(nxt_file* file, int *raw_mode, char** ptr, char** text, int* text_len, char** args) {
  if (!file->content_length) return NXT_EOF;
  nxt_context* ctx=file->ctx;
  char* end=file->content+file->content_length;
  char *pb, *pc, *pd;
  pb=*ptr? *ptr+1 : file->content+1;
REPEAT:
  if (pb-1>=end) return NXT_EOF;
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
  if (text_len) *text_len=end-*text;
  *ptr=end;
  return NXT_NONE;
}

static nxt_block* nxt_block_create(nxt_file* file, const char* name) {
  nxt_context* ctx=file->ctx;
  int id;
  if (name) {
    for (id=0; id<ctx->next_free_block_id; id++) {
      if (ctx->block_names[id].name && !strcmp(name, ctx->block_names[id].name)) break;
    }
    if (id==ctx->next_free_block_id) {
      ctx->next_free_block_id++;
      ctx->block_names[id].name=name;
    }
  }
  else {
    id=ctx->next_free_block_id++;
  }

  nxt_block* blk;
  if (file->first_block) {
    blk=file->first_block;
    while (1) {
      if (blk->id==id) return blk; // return existing block
      if (!blk->next) break;
      blk=blk->next;
    }
    blk=blk->next=nxb_calloc_obj(ctx->nxb, sizeof(nxt_block));
  }
  else {
    blk=file->first_block=nxb_calloc_obj(ctx->nxb, sizeof(nxt_block));
  }
  blk->id=id;

  return blk;
}

nxt_value_part* nxt_block_append_value(nxt_context* ctx, nxt_block* blk, const char* text, int text_len, int insert_after_text_id) {
  nxt_value_part* vp;
  if (blk->value && !blk->clear_on_append) {
    vp=blk->value;
    while (vp->next) vp=vp->next;
    vp=vp->next=nxb_calloc_obj(ctx->nxb, sizeof(nxt_value_part));
  }
  else {
    vp=blk->value=nxb_calloc_obj(ctx->nxb, sizeof(nxt_value_part));
    blk->clear_on_append=0;
  }
  vp->text=text;
  vp->text_len=text_len;
  vp->insert_after_text_id=insert_after_text_id;
  return vp;
}

static const char* nxt_resolve_uri(nxt_context* ctx, const char* cur_uri, const char* new_uri) {
  if (*new_uri=='/') return new_uri;
  const char* query=strchr(cur_uri, '?');
  const char* slash=query? query : cur_uri+strlen(cur_uri);
  assert(*cur_uri=='/');
  while (*(--slash)!='/') ; // find last slash
  int base_len=slash-cur_uri+1;
  char* buf=nxb_alloc_obj(ctx->nxb, base_len+strlen(new_uri)+1);
  memcpy(buf, cur_uri, base_len);
  strcpy(buf+base_len, new_uri);
  if (nxweb_remove_dots_from_uri_path(buf)) { // invalid path
    nxweb_log_error("template error: invalid path %s resolving %s from %s", buf, new_uri, cur_uri);
    return 0;
  }
  return buf;
}

nxt_file* nxt_file_create(nxt_context* ctx, const char* uri) {
  nxt_file* new_file=nxb_calloc_obj(ctx->nxb, sizeof(nxt_file));
  new_file->ctx=ctx;
  new_file->uri=uri;
  if (!ctx->start_file) {
    ctx->start_file=new_file;
    ctx->files_pending++; // increment for start file only
  }
  return new_file;
}

static nxt_block* nxt_include(nxt_file* cur_file, nxt_block* cur_block, const char* new_uri) {
  nxt_context* ctx=cur_file->ctx;
  new_uri=nxt_resolve_uri(ctx, cur_file->uri, new_uri);
  if (!new_uri) return 0;
  nxt_block* include_blk=nxt_block_create(cur_file, 0);
  if (ctx->load(ctx, new_uri, 0, include_blk)) return 0;
  ctx->files_pending++; // increment upon successful load
  return include_blk;
}

static nxt_file* nxt_use(nxt_file* cur_file, const char* new_uri) {
  nxt_context* ctx=cur_file->ctx;
  new_uri=nxt_resolve_uri(ctx, cur_file->uri, new_uri);
  if (!new_uri) return 0;
  if (cur_file->inheritance_level>=NXT_MAX_INHERITANCE_LEVEL) {
    nxweb_log_error("template error (%s): maximum inheritance level reached, check for infinite recursion or increase NXT_MAX_INHERITANCE_LEVEL", cur_file->uri);
    return 0;
  }
  nxt_file* new_file=nxt_file_create(ctx, new_uri);
  new_file->inheritance_level=cur_file->inheritance_level+1;
  if (cur_file->parents) {
    nxt_file* f=cur_file->parents;
    while (f->next_parent) f=f->next_parent;
    f->next_parent=new_file;
  }
  else {
    cur_file->parents=new_file;
  }
  if (ctx->load(ctx, new_uri, new_file, 0)) return 0;
  ctx->files_pending++; // increment upon successful load
  return new_file;
}

int nxt_parse_file(nxt_file* file, char* buf, int buf_len) {
  file->content=buf;
  file->content_length=buf_len;
  nxt_context* ctx=file->ctx;
  char* args[NXT_MAX_ARGS*2]; // start and end pointers for each arg; ends with a pair of null-pointers
  nxt_block* block_stack[NXT_MAX_BLOCK_NESTING];
  int block_stack_idx=0;
  nxt_block* cur_block;
  char* ptr=0;
  char* text;
  int text_len;
  int raw_mode=0;
  int i;
  enum nxt_cmd cmd=NXT_NONE;

  cur_block=nxt_block_create(file, "_top_");

  for (;;) {
    cmd=nxt_get_next_cmd(file, &raw_mode, &ptr, &text, &text_len, args);
    if (cmd==NXT_EOF) break;
    switch (cmd) {
      case NXT_NONE:
        if (text_len) {
          nxt_block_append_value(ctx, cur_block, text, text_len, BN_NONE_ID);
        }
        break;
      case NXT_BLOCK:
        if (!args[0] || !args[1]) {
          nxweb_log_error("template error (%s): block without name; cur_block=%s", file->uri, ctx->block_names[cur_block->id].name);
          goto ERROR;
        }
        if (block_stack_idx>=NXT_MAX_BLOCK_NESTING-1) {
          nxweb_log_error("template error (%s): block nesting over the limit (NXT_MAX_BLOCK_NESTING=%d); cur_block=%s", file->uri, NXT_MAX_BLOCK_NESTING, ctx->block_names[cur_block->id].name);
          goto ERROR;
        }
        block_stack[block_stack_idx++]=cur_block;
        *args[1]='\0';
        nxt_block* new_block=nxt_block_create(file, args[0]);
        nxt_block_append_value(ctx, cur_block, text, text_len, new_block->id);
        new_block->clear_on_append=1;
        cur_block=new_block;
        break;
      case NXT_ENDBLOCK:
        if (text_len) {
          nxt_block_append_value(ctx, cur_block, text, text_len, BN_NONE_ID);
        }
        if (!block_stack_idx) {
          nxweb_log_error("template error (%s): endblock without block; cur_block=%s", file->uri, ctx->block_names[cur_block->id].name);
          goto ERROR;
        }
        cur_block=block_stack[--block_stack_idx];
        break;
      case NXT_USE:
        if (text_len) {
          nxt_block_append_value(ctx, cur_block, text, text_len, BN_NONE_ID);
        }
        for (i=0; i<NXT_MAX_ARGS; i++) {
          if (!args[2*i] || !args[2*i+1]) break;
          *args[2*i+1]='\0';
          if (!nxt_use(file, args[2*i])) {
            goto ERROR;
          }
        }
        break;
      case NXT_INCLUDE:
        if (!args[0] || !args[1]) {
          nxweb_log_error("template error (%s): block without name; cur_block=%s", file->uri, ctx->block_names[cur_block->id].name);
          goto ERROR;
        }
        *args[1]='\0';
        nxt_block* include_blk=nxt_include(file, cur_block, args[0]);
        if (!include_blk) {
          goto ERROR;
        }
        nxt_block_append_value(ctx, cur_block, text, text_len, include_blk->id);
        break;
      case NXT_INCLUDE_PARENT:
        nxt_block_append_value(ctx, cur_block, text, text_len, BN_PARENT_ID);
        break;
    }
    // nxweb_log_error("nxt_parse: cmd=%d text=%.*s arg1=%.*s arg2=%.*s", cmd, text_len, text, (int)(args[1]-args[0]), args[0], (int)(args[3]-args[2]), args[2]);
  }

  if (block_stack_idx) {
    nxweb_log_error("template error (%s): block without endblock; cur_block=%s", file->uri, ctx->block_names[cur_block->id].name);
    goto ERROR;
  }

#if 0
  // debug print out
  nxt_block* blk;
  for (blk=file->first_block; blk; blk=blk->next) {
    nxweb_log_info("BLOCK %s: %.*s", ctx->block_names[blk->id].name, blk->value? blk->value->text_len:1, blk->value? blk->value->text:"-");
  }
#endif

  ctx->files_pending--; // current file done
  return 0;

  ERROR:
  ctx->error=1;
  ctx->files_pending--; // current file done
  return -1;
}

int nxt_parse(nxt_context* ctx, const char* uri, char* buf, int buf_len) {
  nxt_file* file=nxt_file_create(ctx, uri);
  return nxt_parse_file(file, buf, buf_len);
}

static int nxt_is_top_block_empty(nxt_block* blk) {
  nxt_value_part* vp;
  const char* p;
  int i;
  for (vp=blk->value; vp; vp=vp->next) {
    for (i=0, p=vp->text; i<vp->text_len; i++, p++) {
      if ((unsigned char)*p>' ') return 0;
    }
  }
  return 1;
}

static void nxt_merge_file(nxt_file* file) {
  nxt_context* ctx=file->ctx;
  nxt_file* parent;
  for (parent=file->parents; parent; parent=parent->next_parent) {
    nxt_merge_file(parent);
  }
  nxt_block* blk;
  nxt_block_name* bnames=ctx->block_names;
  nxt_block_name* bn;
  for (blk=file->first_block; blk; blk=blk->next) {
    if (!blk->value) continue; // just a placeholder without value
    if (!blk->id && nxt_is_top_block_empty(blk)) continue;
    bn=&bnames[blk->id];
    blk->parent=bn->block;
    bn->block=blk;
  }
}

void nxt_merge(nxt_context* ctx) {
  if (ctx->error) return; // no need
  if (ctx->start_file) nxt_merge_file(ctx->start_file);
}

static void nxt_serialize_block(nxt_context* ctx, nxt_block* blk) {
  if (!blk) {
    nxb_append_str(ctx->nxb, MISSING_BLOCK_STUB);
    return;
  }
  nxt_value_part* vp;
  const char* p;
  int i;
  for (vp=blk->value; vp; vp=vp->next) {
    if (vp->text_len) nxb_append(ctx->nxb, vp->text, vp->text_len);
    if (vp->insert_after_text_id>0) {
      nxt_serialize_block(ctx, ctx->block_names[vp->insert_after_text_id].block);
    }
    else if (vp->insert_after_text_id==BN_PARENT_ID) {
      nxt_serialize_block(ctx, blk->parent);
    }
  }
}

char* nxt_serialize(nxt_context* ctx) {
  if (ctx->error) return "<!--[template error; check error log]-->";
  nxb_start_stream(ctx->nxb);
  if (ctx->block_names[0].block) {
    nxt_serialize_block(ctx, ctx->block_names[0].block);
  }
  nxb_append_char(ctx->nxb, '\0');
  return nxb_finish_stream(ctx->nxb, 0);
}

static void nxt_serialize_block_to_cs(nxt_context* ctx, nxt_block* blk, nxweb_composite_stream* cs) {
  if (!blk) {
    nxweb_composite_stream_append_bytes(cs, MISSING_BLOCK_STUB, sizeof(MISSING_BLOCK_STUB)-1);
    return;
  }
  nxt_value_part* vp;
  const char* p;
  int i;
  for (vp=blk->value; vp; vp=vp->next) {
    if (vp->text_len) {
      nxweb_composite_stream_append_bytes(cs, vp->text, vp->text_len);
    }
    if (vp->insert_after_text_id>0) {
      nxt_serialize_block_to_cs(ctx, ctx->block_names[vp->insert_after_text_id].block, cs);
    }
    else if (vp->insert_after_text_id==BN_PARENT_ID) {
      nxt_serialize_block_to_cs(ctx, blk->parent, cs);
    }
  }
}

void nxt_serialize_to_cs(nxt_context* ctx, nxweb_composite_stream* cs) {
  if (ctx->error) {
    nxweb_composite_stream_append_bytes(cs, "<!--[template error; check error log]-->", sizeof("<!--[template error; check error log]-->")-1);
    return;
  }
  nxt_serialize_block_to_cs(ctx, ctx->block_names[0].block, cs);
}
