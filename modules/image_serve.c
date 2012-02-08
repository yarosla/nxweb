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

#include "../nxweb/nxweb.h"
#include <malloc.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <wand/MagickWand.h>

#include "sendfile.h"

/**
 * ImageMagick Notes
 *
 * Using ImageMagick 6.7.5-3 Q16 built from source.
 * Lots of valgrind errors when using default config (need to investigate more).
 * Using the following config have reduced errors to a minimum:
 *   ./configure --prefix=/opt/ImageMagick-6.7.5-3-noomp --disable-openmp --disable-installed \
 *     --without-magick-plus-plus --without-rsvg --without-wmf --with-included-ltdl
 *
 * Perhaps need to disable more unneeded modules like ps, x, etc.
 */


static MagickWand* watermark;

static int image_serve_init() {
  MagickWandGenesis();
  watermark=NewMagickWand();
  MagickReadImage(watermark, "test/image_serve/watermark.png");
  MagickEvaluateImageChannel(watermark, AlphaChannel, MultiplyEvaluateOperator, 0.5);
  return 0;
}

static void image_serve_finalize() {
  DestroyMagickWand(watermark);
  MagickWandTerminus();
}

NXWEB_MODULE(image_serve, .on_server_startup=image_serve_init, .on_server_shutdown=image_serve_finalize);


static nxweb_result image_serve_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_NEXT; // do not respond to POST requests, etc.

  nxweb_handler* handler=conn->handler;
  const char* document_root=handler->dir;
  assert(document_root);

  char fpath[2048];
  int rlen=strlen(document_root);
  assert(rlen<sizeof(fpath));
  strcpy(fpath, document_root);
  char* path_info=fpath+rlen;
  const char* q=strchr(req->path_info, '?');
  int plen=q? q-req->path_info : strlen(req->path_info);
  if (rlen+plen>sizeof(fpath)-64) { // leave room for index file name etc.
    nxweb_send_http_error(resp, 414, "Request-URI Too Long");
    return NXWEB_ERROR;
  }
  strncat(path_info, req->path_info, plen);
  nxweb_url_decode(path_info, 0);
  plen=strlen(path_info);
  if (plen>0 && path_info[plen-1]=='/') { // directory index
    strcat(path_info+plen, INDEX_FILE);
    plen=strlen(path_info);
  }

  if (nxweb_remove_dots_from_uri_path(path_info)) {
    //nxweb_send_http_error(resp, 404, "Not Found");
    return NXWEB_NEXT;
  }

  const nxweb_mime_type* mtype=nxweb_get_mime_type_by_ext(path_info);
  if (!strcmp(mtype->ext, "png") || !strcmp(mtype->ext, "jpg") || !strcmp(mtype->ext, "gif")) {
    MagickWand* image;
    image=NewMagickWand();
    MagickReadImage(image, fpath);
    MagickStripImage(image);

    MagickCompositeImage(image, watermark, OverCompositeOp, 10, 10);

    strcat(strcat(path_info, ".wmk."), mtype->ext);
    nxweb_log_error("writing %s image file", fpath);
    MagickWriteImage(image, fpath);
    DestroyMagickWand(image);

    return nxweb_sendfile_try(conn, resp, fpath, path_info, handler->cache? DEFAULT_CACHED_TIME : 0, 0, 0, mtype);
  }
  else {
    return nxweb_sendfile_try(conn, resp, fpath, path_info, handler->cache? DEFAULT_CACHED_TIME : 0, req->accept_gzip_encoding, 0, mtype);
  }
}

nxweb_handler image_serve_handler={.on_select=image_serve_on_select, .flags=NXWEB_HANDLE_GET};

NXWEB_SET_HANDLER(image_serve, "/is", &image_serve_handler, .priority=10000, .dir="html", .cache=1);
