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

#include <errno.h>

static void ibuffer_data_in_do_read(nxe_ostream* os, nxe_istream* is) {
  nxd_ibuffer* ib=(nxd_ibuffer*)((char*)os-offsetof(nxd_ibuffer, data_in));
  //nxe_loop* loop=os->super.loop;
  nxe_flags_t flags=0;
  if (ib->data_ptr) { // already closed; perhaps reached max_data_size
    // swallow input data
    char buf[16384];
    is->super.cls.is_cls->read(is, os, buf, sizeof(buf), &flags);
    if (flags&NXEF_EOF) {
      nxe_ostream_unset_ready(os);
    }
    return;
  }
  int size;
  nxb_make_room(ib->nxb, 32);
  char* ptr=nxb_get_room(ib->nxb, &size);
  size--; // for null-terminator
  if (ib->data_size+size > ib->max_data_size) size=ib->max_data_size-ib->data_size-1;
  int bytes_received=is->super.cls.is_cls->read(is, os, ptr, size, &flags);
  if (bytes_received>0) {
    nxb_blank_fast(ib->nxb, bytes_received);
    ib->data_size+=bytes_received;
    if (ib->data_size >= ib->max_data_size-1) {
      nxb_append_char_fast(ib->nxb, '\0');
      ib->data_ptr=nxb_finish_stream(ib->nxb, 0);
      nxe_publish(&ib->data_complete, (nxe_data)-1);
      //nxe_ostream_unset_ready(os);
    }
  }
  if (flags&NXEF_EOF) {
    if (!ib->data_ptr) {
      nxb_append_char_fast(ib->nxb, '\0');
      ib->data_ptr=nxb_finish_stream(ib->nxb, 0);
      nxe_publish(&ib->data_complete, (nxe_data)0);
    }
    nxe_ostream_unset_ready(os);
  }
}

static const nxe_ostream_class ibuffer_data_in_class={.do_read=ibuffer_data_in_do_read};

void nxd_ibuffer_init(nxd_ibuffer* ib, nxb_buffer* nxb, int max_data_size) {
  memset(ib, 0, sizeof(nxd_ibuffer));
  nxb_start_stream(nxb);
  ib->nxb=nxb;
  ib->max_data_size=max_data_size;
  ib->data_in.super.cls.os_cls=&ibuffer_data_in_class;
  ib->data_complete.super.cls.pub_cls=NXE_PUB_DEFAULT;
  ib->data_in.ready=1;
}

void nxd_ibuffer_make_room(nxd_ibuffer* ib, int data_size) {
  assert(ib->data_size+data_size <= ib->max_data_size);
  nxb_make_room(ib->nxb, data_size);
}

char* nxd_ibuffer_get_result(nxd_ibuffer* ib, int* size) {
  if (!ib->data_ptr) {
    nxb_append_char(ib->nxb, '\0');
    ib->data_ptr=nxb_finish_stream(ib->nxb, 0);
    //nxe_ostream_unset_ready(&ib->data_in);
  }
  if (size) *size=ib->data_size;
  return ib->data_ptr;
}



static void obuffer_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_obuffer* ob=(nxd_obuffer*)((char*)is-offsetof(nxd_obuffer, data_out));
  //nxe_loop* loop=is->super.loop;
  nxe_flags_t flags=NXEF_EOF;
  if (!ob->data_size) {
    OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)0, 0, &flags);
  }
  else {
    int bytes_sent=OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)(void*)ob->data_ptr, ob->data_size, &flags);
    if (bytes_sent>0) {
      ob->data_ptr+=bytes_sent;
      ob->data_size-=bytes_sent;
    }
  }
}

static const nxe_istream_class obuffer_data_out_class={.do_write=obuffer_data_out_do_write};

void nxd_obuffer_init(nxd_obuffer* ob, const void* data_ptr, int data_size) {
  memset(ob, 0, sizeof(nxd_obuffer));
  ob->data_ptr=data_ptr;
  ob->data_size=data_size;
  ob->data_out.super.cls.is_cls=&obuffer_data_out_class;
  ob->data_out.evt.cls=NXE_EV_STREAM;
  ob->data_out.ready=1;
}


static void rbuffer_data_in_do_read(nxe_ostream* os, nxe_istream* is) {
  nxd_rbuffer* rb=(nxd_rbuffer*)((char*)os-offsetof(nxd_rbuffer, data_in));
  //nxe_loop* loop=os->super.loop;

  nxweb_log_debug("rbuffer_data_in_do_read");

  nxe_size_t size;
  char* ptr=nxd_rbuffer_get_write_ptr(rb, &size);
  assert(size);
/*
  if (!size) {
    // this should never happen
    nxe_ostream_unset_ready(os);
    return;
  }
*/
  nxe_flags_t flags=0;
  nxe_ssize_t bytes_received=ISTREAM_CLASS(is)->read(is, os, ptr, size, &flags);
  if (bytes_received>0) {
    nxd_rbuffer_write(rb, bytes_received);
  }
  if (flags&NXEF_EOF) {
    //nxweb_log_error("rb[%p] received EOF", rb);
    rb->eof=1;
    nxe_istream_set_ready(os->super.loop, &rb->data_out); // even when no bytes received make sure we signal readiness on EOF
  }
}


static void rbuffer_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_rbuffer* rb=(nxd_rbuffer*)((char*)is-offsetof(nxd_rbuffer, data_out));
  //nxe_loop* loop=os->super.loop;

  nxweb_log_debug("rbuffer_data_out_do_write");

  nxe_size_t size;
  const void* ptr;
  nxe_flags_t flags=0;
  ptr=nxd_rbuffer_get_read_ptr(rb, &size, &flags);
  nxe_ssize_t bytes_sent=OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)ptr, size, &flags);
  if (bytes_sent>0) {
    nxd_rbuffer_read(rb, bytes_sent);
  }
}

static const nxe_ostream_class rbuffer_data_in_class={.do_read=rbuffer_data_in_do_read};
static const nxe_istream_class rbuffer_data_out_class={.do_write=rbuffer_data_out_do_write};

void nxd_rbuffer_init(nxd_rbuffer* rb, void* buf, int size) {
  memset(rb, 0, sizeof(nxd_rbuffer));
  nxd_rbuffer_init_ptr(rb, buf, size);
  rb->data_out.super.cls.is_cls=&rbuffer_data_out_class;
  rb->data_in.super.cls.os_cls=&rbuffer_data_in_class;
  rb->data_out.evt.cls=NXE_EV_STREAM;
  rb->data_in.ready=1;
}

void nxd_rbuffer_read(nxd_rbuffer* rb, int size) {
  nxweb_log_debug("nxd_rbuffer_read");

  if (size) {
    rb->read_ptr+=size;
    assert(rb->read_ptr <= rb->end_ptr);
    if (rb->read_ptr >= rb->end_ptr) rb->read_ptr=rb->start_ptr;
    rb->last_write=0;
    if (!rb->eof) {
      if (rb->data_in.super.loop) nxe_ostream_set_ready(rb->data_in.super.loop, &rb->data_in);
      else rb->data_in.ready=1;
    }
    if (rb->read_ptr==rb->write_ptr && !rb->eof) { // nxd_rbuffer_is_empty(rb)
      //nxweb_log_error("rb->read_ptr==rb->write_ptr && !rb->eof => closing rb[%p].data_out", rb);
      if (rb->data_out.super.loop) nxe_istream_unset_ready(&rb->data_out);
      else rb->data_out.ready=0;
    }
  }
}

void nxd_rbuffer_write(nxd_rbuffer* rb, int size) {
  nxweb_log_debug("nxd_rbuffer_write");

  if (size) {
    rb->write_ptr+=size;
    assert(rb->write_ptr <= rb->end_ptr);
    if (rb->write_ptr >= rb->end_ptr) rb->write_ptr=rb->start_ptr;
    rb->last_write=1;
    if (rb->data_out.super.loop) nxe_istream_set_ready(rb->data_out.super.loop, &rb->data_out);
    else rb->data_out.ready=1;
    if (rb->read_ptr==rb->write_ptr) { // nxd_rbuffer_is_full(rb)
      if (rb->data_in.super.loop) nxe_ostream_unset_ready(&rb->data_in);
      else rb->data_in.ready=0;
    }
  }
}



static void fbuffer_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_fbuffer* fb=OBJ_PTR_FROM_FLD_PTR(nxd_fbuffer, data_out, is);
  //nxe_loop* loop=is->super.loop;
  nxe_flags_t flags=NXEF_EOF;
  size_t size=fb->end - fb->offset;

  if (!size) { // EOF
    OSTREAM_CLASS(os)->write(os, is, fb->fd, &fb->fr, (nxe_data)fb->offset, 0, &flags);
  }
  else {
    nxe_ssize_t bytes_sent=OSTREAM_CLASS(os)->write(os, is, fb->fd, &fb->fr, (nxe_data)fb->offset, size, &flags);
    if (bytes_sent>0) {
      fb->offset+=bytes_sent;
    }
  }
}

static const nxe_istream_class fbuffer_data_out_class={.do_write=fbuffer_data_out_do_write};

void nxd_fbuffer_init(nxd_fbuffer* fb, int fd, off_t offset, off_t end) {
  memset(fb, 0, sizeof(nxd_fbuffer));
  assert(fd>0);
  fb->fd=fd;
  fb->offset=offset;
  fb->end=end;
  fb->data_out.super.cls.is_cls=&fbuffer_data_out_class;
  fb->data_out.evt.cls=NXE_EV_STREAM;
  fb->data_out.ready=1;
}

void nxd_fbuffer_finalize(nxd_fbuffer* fb) {
  if (fb->data_out.pair) nxe_disconnect_streams(&fb->data_out, fb->data_out.pair);
  nx_file_reader_finalize(&fb->fr);
  fb->fd=0;
}


static void fwbuffer_data_in_do_read(nxe_ostream* os, nxe_istream* is) {
  nxd_fwbuffer* fwb=OBJ_PTR_FROM_FLD_PTR(nxd_fwbuffer, data_in, os);
  char buf[16384];
  nxe_size_t max_bytes_to_store=fwb->max_size > fwb->size ? fwb->max_size-fwb->size : 0;
  if (max_bytes_to_store>sizeof(buf)) max_bytes_to_store=sizeof(buf);
  // NB: continue reading even after reaching max_size; just swallow the input
  nxe_flags_t flags=0;
  nxe_ssize_t bytes_received=ISTREAM_CLASS(is)->read(is, os, buf, sizeof(buf), &flags);
  if (bytes_received>0) {
    fwb->size+=bytes_received;
    size_t bytes_to_store;
    if (fwb->error) {
      bytes_to_store=0;
    }
    else if (bytes_received>max_bytes_to_store) { // max_size reached
      fwb->error=EFBIG;
      bytes_to_store=max_bytes_to_store;
    }
    else {
      bytes_to_store=bytes_received;
    }
    if (bytes_to_store) {
      if (write(fwb->fd, buf, bytes_to_store)!=bytes_to_store) {
        fwb->error=errno;
      }
    }
  }
  if (flags&NXEF_EOF) {
    nxe_ostream_unset_ready(os);
  }
}

static const nxe_ostream_class fwbuffer_data_in_class={.do_read=fwbuffer_data_in_do_read};

void nxd_fwbuffer_init(nxd_fwbuffer* fwb, int fd, nxe_size_t max_size) {
  memset(fwb, 0, sizeof(nxd_fwbuffer));
  fwb->fd=fd;
  fwb->max_size=max_size;
  fwb->data_in.super.cls.os_cls=&fwbuffer_data_in_class;
  fwb->data_in.ready=1;
}

void nxd_fwbuffer_finalize(nxd_fwbuffer* fwb) {
  fwb->fd=0;
}
