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

#include <math.h>

#include <wand/MagickWand.h>


static int on_module_startup() {
  MagickWandGenesis();
  return 0;
}

static void on_module_shutdown() {
  MagickWandTerminus();
}

static void on_module_config(const nx_json* js);

NXWEB_MODULE(draw_filter, .on_server_startup=on_module_startup, .on_server_shutdown=on_module_shutdown, .on_config=on_module_config);


typedef struct nxweb_filter_draw {
  nxweb_filter base;
  const char* font_file;
} nxweb_filter_draw;

typedef struct draw_filter_data {
  nxweb_filter_data fdata;
  unsigned char* blob;
  int input_fd;
  nxd_obuffer ob;
} draw_filter_data;


static nxweb_filter_data* draw_init(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(draw_filter_data));
  return fdata;
}

static void draw_finalize(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  draw_filter_data* dfdata=(draw_filter_data*)fdata;
  if (dfdata->blob) {
    MagickRelinquishMemory(dfdata->blob);
    dfdata->blob=0;
  }
  if (dfdata->input_fd) {
    close(dfdata->input_fd);
    dfdata->input_fd=0;
  }
}

static inline nxweb_http_header* nxweb_remove_response_header(nxweb_http_response *resp, const char* name) {
  return resp->headers? nx_simple_map_remove_nocase(&resp->headers, name) : 0;
}

#define DegreesToRadians(a) (a*M_PI/180.)
#define RadiansToDegrees(a) (a*180./M_PI)

static void set_rotate_affine(AffineMatrix* affine, double degrees, double tx, double ty) {
  affine->sx=cos(DegreesToRadians(fmod(degrees, 360.0)));
  affine->rx=sin(DegreesToRadians(fmod(degrees, 360.0)));
  affine->ry=-sin(DegreesToRadians(fmod(degrees, 360.0)));
  affine->sy=cos(DegreesToRadians(fmod(degrees, 360.0)));
  affine->tx=tx;
  affine->ty=ty;
}

static nxweb_result draw_do_filter(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;
  nxweb_http_header* h=nxweb_remove_response_header(resp, "X-NXWEB-Draw");
  if (!h) {
    fdata->bypass=1;
    return NXWEB_NEXT;
  }
  const char* cmd=h->value;
  int cmd_len=strlen(cmd);
  int width=10+cmd_len*20;
  int height=40;

  draw_filter_data* dfdata=(draw_filter_data*)fdata;
  MagickWand* bg=NewMagickWand();
  MagickWand* text=NewMagickWand();
  MagickWand* rect=NewMagickWand();
  DrawingWand* d_wand=NewDrawingWand();
  PixelWand* p_black=NewPixelWand();
  PixelWand* p_transparent=NewPixelWand();
  AffineMatrix affine;

  PixelSetColor(p_black, "black");
  PixelSetColor(p_transparent, "transparent");
  MagickNewImage(rect, width, height, p_transparent);
  MagickNewImage(bg, width, height, p_transparent);
  MagickNewImage(text, width+10, height+10, p_transparent);

  int i;
  double d, x, y, w;
  for (i=0; i<cmd_len+4; i++) {
    d=90.0*(((double)rand()/(double)RAND_MAX)-0.5);
    x=((double)rand()/(double)RAND_MAX)*(double)width;
    y=((double)rand()/(double)RAND_MAX)*(double)height;
    w=((double)rand()/(double)RAND_MAX+1.0)*(double)height;
    set_rotate_affine(&affine, d, x, y);

    //MagickSetImageColor(rect, p_transparent);
    ClearDrawingWand(d_wand);
    DrawSetFillColor(d_wand, p_black);
    DrawAffine(d_wand, &affine);
    DrawRoundRectangle(d_wand, -w, -w, w, w, w/4, w/4);
    MagickDrawImage(rect, d_wand);
    MagickCompositeImage(bg, rect, XorCompositeOp, 0, 0);
  }
  ClearDrawingWand(d_wand);
  if (((nxweb_filter_draw*)filter)->font_file) DrawSetFont(d_wand, ((nxweb_filter_draw*)filter)->font_file);
  DrawSetFontSize(d_wand, 30);
  DrawSetFillColor(d_wand, p_black);
  char t[2];
  t[1]='\0';
  for (i=0; cmd[i]; i++) {
    t[0]=cmd[i];
    d=30.0*(((double)rand()/(double)RAND_MAX)-0.5);
    MagickAnnotateImage(text, d_wand, 5+5+i*20, height-11+5, d, t);
  }

  MagickWaveImage(text, 1, 15);
  MagickCompositeImage(bg, text, XorCompositeOp, -5, -5);
  MagickEvaluateImageChannel(bg, AlphaChannel, MultiplyEvaluateOperator, 0.5);
  MagickSetImageFormat(bg, "PNG");
  size_t blob_size;
  unsigned char* blob=MagickGetImageBlob(bg, &blob_size);
  dfdata->blob=blob;

  DestroyMagickWand(bg);
  DestroyMagickWand(text);
  DestroyMagickWand(rect);
  DestroyDrawingWand(d_wand);
  DestroyPixelWand(p_black);
  DestroyPixelWand(p_transparent);

  resp->content_type="image/png";
  resp->content_length=blob_size;
  resp->content=blob;
  nxd_obuffer_init(&dfdata->ob, resp->content, resp->content_length);
  resp->content_out=&dfdata->ob.data_out;

  // reset previous response content
  resp->sendfile_path=0;
  if (resp->sendfile_fd) {
    // save it to close on finalize
    dfdata->input_fd=resp->sendfile_fd;
    resp->sendfile_fd=0;
  }
  resp->last_modified=0;

  return NXWEB_OK;
}

static nxweb_filter* draw_config(nxweb_filter* base, const nx_json* json) {
  nxweb_filter_draw* f=calloc(1, sizeof(nxweb_filter_draw)); // NOTE this will never be freed
  *f=*(nxweb_filter_draw*)base;
  const char* font_file=nx_json_get(json, "font_file")->text_value;
  if (font_file) f->font_file=font_file;
  return (nxweb_filter*)f;
}

static nxweb_filter_draw draw_filter={.base={
        .config=draw_config,
        .init=draw_init, .finalize=draw_finalize,
        .do_filter=draw_do_filter}};

NXWEB_DEFINE_FILTER(draw, draw_filter.base);

nxweb_filter* nxweb_draw_filter_setup(const char* font_file) {
  nxweb_filter_draw* f=nx_alloc(sizeof(nxweb_filter_draw)); // NOTE this will never be freed
  *f=draw_filter;
  f->font_file=font_file;
  return (nxweb_filter*)f;
}

static void on_module_config(const nx_json* json) {
  if (!json) return; // default config
  const char* font_file=nx_json_get(json, "font_file")->text_value;
  if (font_file) draw_filter.font_file=font_file;
}
