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

#ifndef NX_FILE_READER_H
#define	NX_FILE_READER_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "nx_alloc.h"

#define NXFR_USE_MMAP
#define NX_FILE_READER_MALLOC_SIZE (32768L)
#define NX_FILE_READER_MMAP_THRESHOLD (32768L*8)
#define NX_FILE_READER_MMAP_SIZE (67108864UL)

typedef uint64_t nxfr_size_t;

typedef struct nx_file_reader {
  int fd;
  char* mbuf;
  nxfr_size_t mbuf_offset;
  nxfr_size_t mbuf_size;
  nxfr_size_t file_size;
  _Bool mmapped:1;
} nx_file_reader;

//int nx_file_reader_open(nx_file_reader* fr, const char* path);
static inline void nx_file_reader_init(nx_file_reader* fr) {
  memset(fr, 0, sizeof(*fr));
}

void nx_file_reader_finalize(nx_file_reader* fr);
const char* nx_file_reader_get_mbuf_ptr(nx_file_reader* fr, int fd, nxfr_size_t file_size, nxfr_size_t offset, nxfr_size_t* size);


#ifdef	__cplusplus
}
#endif

#endif	/* NX_FILE_READER_H */

