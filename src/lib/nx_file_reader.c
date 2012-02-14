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

#define _FILE_OFFSET_BITS 64

#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "nx_file_reader.h"

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

static nxfr_size_t PAGE_SIZE_MASK=0;

static void nx_file_reader_global_init(void) __attribute__ ((constructor));
static void nx_file_reader_global_init(void) {
  PAGE_SIZE_MASK=~(sysconf(_SC_PAGE_SIZE)-1);
}

void nx_file_reader_finalize(nx_file_reader* fr) {
  if (fr->fd) {
    if (fr->mbuf) {
      if (fr->mmapped) munmap(fr->mbuf, fr->mbuf_size);
      else nx_free(fr->mbuf);
      fr->mbuf=0;
    }
    fr->fd=0;
  }
}

static inline int load_page(nx_file_reader* fr, nxfr_size_t offset) {
#ifdef NXFR_USE_MMAP
  if (fr->file_size-offset <= NX_FILE_READER_MMAP_THRESHOLD) {
#endif
    fr->mmapped=0;
    fr->mbuf_offset=offset;
    fr->mbuf_size=min(NX_FILE_READER_MALLOC_SIZE, fr->file_size - fr->mbuf_offset);
    fr->mbuf=nx_alloc(fr->mbuf_size);
    if (lseek(fr->fd, fr->mbuf_offset, SEEK_SET)!=fr->mbuf_offset) return -1;
    if (read(fr->fd, fr->mbuf, fr->mbuf_size) < fr->mbuf_size) return -1;
#ifdef NXFR_USE_MMAP
  }
  else {
    fr->mmapped=1;
    fr->mbuf_offset=offset & PAGE_SIZE_MASK;
    fr->mbuf_size=min(NX_FILE_READER_MMAP_SIZE, fr->file_size - fr->mbuf_offset);
    fr->mbuf=mmap(0, fr->mbuf_size, PROT_READ, MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, fr->fd, fr->mbuf_offset); // MAP_SHARED or MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE
    if (fr->mbuf==(void *)-1) return -1;
    madvise(fr->mbuf, fr->mbuf_size, MADV_WILLNEED);
  }
#endif
  return 0;
}

const char* nx_file_reader_get_mbuf_ptr(nx_file_reader* fr, int fd, nxfr_size_t file_size, nxfr_size_t offset, nxfr_size_t* size) {
  if (offset>=file_size) { // EOF
    *size=0;
    return (char*)fr; // some non-null pointer as null would mean error
  }
  if (fr->fd && (fd!=fr->fd || fr->file_size!=file_size
          || offset < fr->mbuf_offset || offset >= fr->mbuf_offset+fr->mbuf_size)) nx_file_reader_finalize(fr);
  if (!fr->fd) {
    fr->fd=fd;
    fr->file_size=file_size;
    if (load_page(fr, offset)) return 0;
  }
  offset-=fr->mbuf_offset;
  *size=fr->mbuf_size-offset;
  return fr->mbuf+offset;
}