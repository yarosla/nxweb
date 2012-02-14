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

