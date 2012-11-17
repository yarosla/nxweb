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

#define NXT_MAX_ARGS 32

typedef struct nxt_block_name {
  const char* name;
  struct nxt_block* block; // assigned on merge
} nxt_block_name;

struct nxt_context;
struct nxt_file;

typedef int (*nxt_loader)(struct nxt_context* ctx, struct nxt_file* file); // function to make subrequests

typedef struct nxt_context {
  struct nxt_file* start_file;
  nxt_loader load; // function to make subrequests
  nxt_block_name* block_names;
  nxb_buffer* nxb;
  int next_free_id;
} nxt_context;

typedef struct nxt_file {
  nxt_context* ctx;
  const char* uri;
  struct nxt_file* parents;
  struct nxt_file* next_parent; // sibling
  char* content;
  int content_length;
} nxt_file;

typedef struct nxt_value_part {
  const char* text;
  nxt_block_name* insert_after_text;
  struct nxt_value_part* next;
} nxt_value_part;

typedef struct nxt_block {
  int id;
  nxt_value_part* value;
  struct nxt_block* parent;
} nxt_block;

void nxt_init(nxt_context* ctx, nxb_buffer* nxb);
void nxt_content_loaded(nxt_context* ctx, struct nxt_file* file); // this must be called back by the loader
void nxt_parse(nxt_context* ctx, const char* uri, char* buf, int buf_len);


#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_TEMPLATES_H */

