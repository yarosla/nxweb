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
#include "misc.h"

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
    if ( (lseek(fr->fd, fr->mbuf_offset, SEEK_SET)!=fr->mbuf_offset)
      || (read(fr->fd, fr->mbuf, fr->mbuf_size) < fr->mbuf_size) ) {
      // file read failed: file content disappeared, concurrent modification, etc.
      // fill in mbuf with spaces to be on a safe side
      memset(fr->mbuf, ' ', fr->mbuf_size);
      return -1;
    }
#ifdef NXFR_USE_MMAP
  }
  else {
    fr->mmapped=1;
    fr->mbuf_offset=offset & PAGE_SIZE_MASK;
    fr->mbuf_size=min(NX_FILE_READER_MMAP_SIZE, fr->file_size - fr->mbuf_offset);
    fr->mbuf=mmap(0, fr->mbuf_size, PROT_READ, MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, fr->fd, fr->mbuf_offset); // MAP_SHARED or MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE
    if (fr->mbuf==(void *)-1) {
      // file read failed: file content disappeared, concurrent modification, etc.
      // fill in mbuf with spaces to be on a safe side
      fr->mmapped=0;
      fr->mbuf=nx_alloc(fr->mbuf_size);
      memset(fr->mbuf, ' ', fr->mbuf_size);
      return -1;
    }
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
    if (load_page(fr, offset)) {
      nxweb_log_error("nx_file_reader: load_page() failed for fd=%d, file_size=%ld, offeset=%ld; concurrent file modification?", fd, file_size, offset);
      // return 0; -- do not do this as it may cause segfaults; no safe checks by clients
    }
  }
  offset-=fr->mbuf_offset;
  *size=fr->mbuf_size-offset;
  return fr->mbuf+offset;
}