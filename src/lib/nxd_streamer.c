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

#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>


static void streamer_data_out_do_write(nxe_istream* is, nxe_ostream* os) {
  nxd_streamer* strm=(nxd_streamer*)((char*)is-offsetof(nxd_streamer, data_out));
  nxd_streamer_node* snode=strm->current;
  nxe_loop* loop=is->super.loop;

  nxweb_log_debug("streamer_data_out_do_write");

  if (!snode) {
    assert(strm->force_eof);
    // write EOF
    nxe_flags_t flags=NXEF_EOF;
    OSTREAM_CLASS(os)->write(os, is, 0, 0, (nxe_data)"", 0, &flags);
    nxe_istream_unset_ready(is);
    return;
  }

  nxe_istream* prev_is=snode->data_in.pair;
  if (prev_is) {
    if (prev_is->ready) {
      snode->data_in.ready=1;
      ISTREAM_CLASS(prev_is)->do_write(prev_is, &snode->data_in);
    }
    if (!prev_is->ready) {
      nxe_istream_unset_ready(is);
      nxe_ostream_set_ready(loop, &snode->data_in); // get notified when prev_is becomes ready again
    }
  }
  else {
    // nxweb_log_error("no connected device for snode->data_in"); -- this is OK for streamer
    nxe_istream_unset_ready(is);
  }
}

static nxe_ssize_t streamer_data_in_write(nxe_ostream* os, nxe_istream* is, int fd, nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags) {
  nxd_streamer_node* snode=(nxd_streamer_node*)((char*)os-offsetof(nxd_streamer_node, data_in));
  nxd_streamer* strm=snode->strm;
  nxe_loop* loop=os->super.loop;

  nxweb_log_debug("streamer_data_in_write");

  if (snode!=strm->current) {
    nxe_ostream_unset_ready(os); // wait in queue for nxd_streamer_node_start()
    return 0;
  }
  nxe_ssize_t bytes_sent=0;
  if (size>0 || *flags&NXEF_EOF) {
    nxe_ostream* next_os=strm->data_out.pair;
    if (next_os) {
      nxe_flags_t wflags=*flags;
      if (!snode->final) wflags&=~NXEF_EOF;
      if (next_os->ready) {
        bytes_sent=OSTREAM_CLASS(next_os)->write(next_os, &strm->data_out, fd, fr, ptr, size, &wflags);
      }
      if (!next_os->ready) {
        nxe_ostream_unset_ready(os);
        nxe_istream_set_ready(loop, &strm->data_out); // get notified when next_os becomes ready again
      }
    }
    else {
      // nxweb_log_error("no connected device for strm->data_out"); -- this is OK
      nxe_ostream_unset_ready(os);
    }
  }
  if (*flags&NXEF_EOF && bytes_sent==size) {
    // end of node's stream => switch to next
    snode->complete=1;
    if (snode->final) { // real EOF
      // EOF just written
    }
    else {
      if (snode->next) {
        nxd_streamer_node_start(snode->next);
      }
      else {
        // new node shall autostart upon add
        nxe_istream_unset_ready(&strm->data_out);
        nxe_ostream_unset_ready(os);
      }
    }
    return bytes_sent;
  }
  return bytes_sent;
}

static const nxe_istream_class streamer_data_out_class={.do_write=streamer_data_out_do_write};
static const nxe_ostream_class streamer_data_in_class={.write=streamer_data_in_write};

void nxd_streamer_init(nxd_streamer* strm) {

  nxweb_log_debug("nxd_streamer_init");

  memset(strm, 0, sizeof(nxd_streamer));
  strm->data_out.super.cls.is_cls=&streamer_data_out_class;
  strm->data_out.evt.cls=NXE_EV_STREAM;
}

void nxd_streamer_node_init(nxd_streamer_node* snode) {

  nxweb_log_debug("nxd_streamer_node_init");

  memset(snode, 0, sizeof(nxd_streamer_node));
  snode->data_in.super.cls.os_cls=&streamer_data_in_class;
  //snode->data_in.ready=1;
}

void nxd_streamer_add_node(nxd_streamer* strm, nxd_streamer_node* snode, int final) {

  nxweb_log_debug("nxd_streamer_add_node");

  if (!strm->head) {
    strm->head=snode;
  }
  else {
    nxd_streamer_node* prev=strm->head;
    while (prev->next) prev=prev->next;
    prev->next=snode;
  }
  snode->strm=strm;
  snode->final=!!final;
  if (strm->running && (!strm->current || strm->current->complete)) {
    nxd_streamer_node_start(snode);
  }
}

void nxd_streamer_node_finalize(nxd_streamer_node* snode) {

  nxweb_log_debug("nxd_streamer_node_finalize");

  if (snode->data_in.pair) nxe_disconnect_streams(snode->data_in.pair, &snode->data_in);
  if (snode->next) nxd_streamer_node_finalize(snode->next);
}

void nxd_streamer_finalize(nxd_streamer* strm) {
  if (strm->data_out.pair) nxe_disconnect_streams(&strm->data_out, strm->data_out.pair);
  if (strm->head) nxd_streamer_node_finalize(strm->head);
}

void nxd_streamer_node_start(nxd_streamer_node* snode) {

  nxweb_log_debug("nxd_streamer_node_start");

  assert(!snode->complete);
  snode->strm->current=snode;
  nxe_ostream_set_ready(snode->data_in.super.loop, &snode->data_in);
}

void nxd_streamer_start(nxd_streamer* strm) {
  strm->running=1;
  if (strm->head) nxd_streamer_node_start(strm->head);
  else if (strm->force_eof) {
    // empty streamer has been closed beforehand => activate data_out to send EOF
    nxe_istream_set_ready(strm->data_out.super.loop, &strm->data_out);
  }
}

void nxd_streamer_close(nxd_streamer* strm) {
  nxd_streamer_node* snode=strm->head;
  if (!snode) { // closing empty streamer
    strm->force_eof=1;
  }
  else {
    while (snode->next) snode=snode->next; // find last node
    snode->final=1;
    if (snode->complete) {
      // final node has already been transmitted => force EOF
      strm->force_eof=1;
    }
  }
  if (strm->running && strm->force_eof) {
    // activate data_out to send EOF
    if (strm->data_out.super.loop) nxe_istream_set_ready(strm->data_out.super.loop, &strm->data_out);
    else strm->data_out.ready=1;
  }
}
