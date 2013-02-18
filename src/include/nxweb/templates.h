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

#ifndef NXWEB_TEMPLATES_H
#define	NXWEB_TEMPLATES_H

#ifdef	__cplusplus
extern "C" {
#endif

// max number of directive arguments:
#define NXT_MAX_ARGS 32
// max number of block names in template tree:
#define NXT_MAX_BLOCKS 2048
// max nesting level of blocks inside blocks:
#define NXT_MAX_BLOCK_NESTING 64
// max inheritance (extends/use) level (to prevent recursion):
#define NXT_MAX_INHERITANCE_LEVEL 32

enum nxt_cmd {
  NXT_EOF=-1,
  NXT_NONE=0,
  NXT_RAW,
  NXT_ENDRAW,
  NXT_BLOCK,
  NXT_ENDBLOCK,
  NXT_USE, // same as "extends"
  NXT_INCLUDE,
  NXT_INCLUDE_PARENT
};

typedef struct nxt_block_name {
//  union {
//    uint64_t h;
//    char pfx[8];
//  } name_hash;
  const char* name;
  struct nxt_block* block; // assigned on merge
} nxt_block_name;

struct nxt_context;
struct nxt_file;
struct nxt_block;

typedef int (*nxt_loader)(struct nxt_context* ctx, const char* uri, struct nxt_file* dst_file, struct nxt_block* dst_block); // function to make subrequests

typedef struct nxt_context {
  struct nxt_file* start_file;
  nxt_loader load; // function to make subrequests
  nxe_data loader_data;
  nxb_buffer* nxb;
  nxt_block_name block_names[NXT_MAX_BLOCKS];
  int next_free_block_id;
  int files_pending;
  unsigned error:1;
} nxt_context;

typedef struct nxt_file {
  nxt_context* ctx;
  const char* uri;
  struct nxt_file* parents;
  struct nxt_file* next_parent; // sibling
  //struct nxt_file* next; // within context
  struct nxt_block* first_block;
  char* content;
  int content_length;
  int inheritance_level;
} nxt_file;

typedef struct nxt_value_part {
  const char* text;
  int text_len;
  int insert_after_text_id;
  struct nxt_value_part* next;
} nxt_value_part;

typedef struct nxt_block {
  int id;
  _Bool clear_on_append:1;
  nxt_value_part* value;
  struct nxt_block* parent; // assigned on merge
  struct nxt_block* next; // within file
} nxt_block;

void nxt_init(nxt_context* ctx, nxb_buffer* nxb, nxt_loader loader, nxe_data loader_data);
int nxt_parse(nxt_context* ctx, const char* uri, char* buf, int buf_len);
nxt_file* nxt_file_create(nxt_context* ctx, const char* uri);
int nxt_parse_file(nxt_file* file, char* buf, int buf_len);
nxt_value_part* nxt_block_append_value(nxt_context* ctx, nxt_block* blk, const char* text, int text_len, int insert_after_text_id);
void nxt_merge(nxt_context* ctx);
char* nxt_serialize(nxt_context* ctx);
void nxt_serialize_to_cs(nxt_context* ctx, nxweb_composite_stream* cs);

static inline int nxt_is_complete(nxt_context* ctx) { return !ctx->files_pending; }

#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_TEMPLATES_H */

