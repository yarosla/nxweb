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
#include <errno.h>
#include <pthread.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <utime.h>

#include <wand/MagickWand.h>

#include "sendfile.h"

#ifdef WITH_SSL
#include <nettle/sha.h>
#else
#include "../deps/sha1-c/sha1.h"
#endif

/**
 * image_serv module can do the following:
 *
 *  * generate image thumbnails on the fly using following commands (inserted into file name):
 *    - filename.100x100.ext -- scale filename.ext to fit the box
 *    - filename.c100x100.ext -- scale & crop to fit the box exactly
 *    - filename.f100x100.ext -- scale & fill to fit the box exactly
 *    - filename.f100x100xFF8800.ext -- provide fill color (hex in uppercase only)
 *    - filename.f100x100tl.ext -- specify gravity for fill & crop (t, r, b, l, tl, tr, bl, br)
 *  * watermark images by watermark.png file if such a file exists up in directory tree
 *  * cache generated files in cdir (see handler config record)
 *  * transparently serve all static files using sendfile module (including its memory caching abilities)
 *
 * Only GIF, PNG and JPG files supported.
 *
 * Not all commands will be allowed by default. See notes for allowed_cmds[] below.
 */

/**
 * ImageMagick Notes
 *
 * Tested with ImageMagick 6.7.5-3 Q16 built from source.
 * Lots of valgrind errors when using default config (need to investigate more).
 * Using the following config have reduced errors to a minimum:
 *   ./configure --prefix=/opt/ImageMagick-6.7.5-3-noomp --disable-openmp --disable-installed \
 *     --without-magick-plus-plus --without-rsvg --without-wmf --with-included-ltdl
 *
 * Perhaps need to disable more unneeded modules like ps, x, etc.
 */

// don't forget to change secret key in your setup
#define CMD_SIGN_SECRET_KEY "XbLBZeqSsUgfKWooMKoh0r1gjzqG856yVCMLf1pz"

#define WATERMARK_FILE_NAME "watermark.png"
#define MAX_PATH 1024

#ifndef max
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#endif

#ifndef min
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
#endif

typedef struct isrv_cmd {
  char cmd;
  _Bool dont_watermark:1;
  _Bool gravity_top:1;
  _Bool gravity_right:1;
  _Bool gravity_bottom:1;
  _Bool gravity_left:1;
  int width;
  int height;
  char color[8]; // "#FF00AA\0"
} isrv_cmd;

/**
 * Only commands listed below will be allowed (to protect from DoS).
 * Arbitrary command can also be executed if signed by QUERY_STRING.
 * Eg. GET /is/benchmark/watermark.c500x500.png?1B4A0DF16BFF7050B9C7C6A63576F8F2D1695CF1
 * Signature calculated as SHA1(uri_path+CMD_SIGN_SECRET_KEY)
 */

static isrv_cmd allowed_cmds[]={
  {.cmd='s', .width=50, .height=50},
  {.cmd='s', .width=100, .height=100},
  {.cmd='s', .width=100, .height=0},
  {.cmd='s', .width=500, .height=0},
  {.cmd='s', .width=500, .height=500},
  {.cmd='c', .width=50, .height=50},
  {.cmd='c', .width=100, .height=100},
  {.cmd='f', .width=50, .height=50},
  {.cmd='f', .width=100, .height=100},
  {.cmd='f', .width=100, .height=100, .color="#FF8800"},
  {.cmd=0}
};


static int image_serve_init() {
  MagickWandGenesis();
  return 0;
}

static void image_serve_finalize() {
  MagickWandTerminus();
}

NXWEB_MODULE(image_serve, .on_server_startup=image_serve_init, .on_server_shutdown=image_serve_finalize);

static int locate_watermark(const char* fpath, const char* path_info, char* wpath) {
  strcpy(wpath, fpath);
  const char* wpath_info=wpath+(path_info-fpath);
  char* p;
  struct stat finfo;
  while ((p=strrchr(wpath_info, '/'))) {
    strcpy(p+1, WATERMARK_FILE_NAME);
    if (stat(wpath, &finfo)==0 && !S_ISDIR(finfo.st_mode)) {
      return !!finfo.st_size;
    }
    *p='\0';
  }
  return 0; // not found
}

static int process_cmd(const char* fpath, const char* path_info, MagickWand* image, isrv_cmd* cmd) {
  int width=MagickGetImageWidth(image);
  int height=MagickGetImageHeight(image);
  if (width<=0 || height<=0) {
    nxweb_log_error("illegal width or height of image %s", fpath);
    return 0;
  }
  int dirty=0;
  int max_width=cmd->width;
  int max_height=cmd->height;
  if (cmd->cmd=='s') {
    if (max_width==0) max_width=1000000;
    if (max_height==0) max_height=1000000;
    if (width>max_width || height>max_height) { // don't enlarge!
      double scale_x=(double)max_width/(double)width;
      double scale_y=(double)max_height/(double)height;
      if (scale_x<scale_y) {
        width=max_width;
        height=round(scale_x*height);
      }
      else {
        width=round(scale_y*width);
        height=max_height;
      }
      MagickResizeImage(image, width, height, LanczosFilter, 1);
      dirty=1;
    }
  }
  else if (cmd->cmd=='c') {
    double scale_x=(double)max_width/(double)width;
    double scale_y=(double)max_height/(double)height;
    double scale=max(scale_x, scale_y);
    if (scale<1.) { // don't enlarge!
      width=round(scale*width);
      height=round(scale*height);
      MagickResizeImage(image, width, height, LanczosFilter, 1);
      dirty=1;
    }
    if (width!=max_width || height!=max_height) {
      PixelWand* pwand=NewPixelWand();
      char* format=MagickGetImageFormat(image); // returns "JPEG", "GIF", or "PNG"
      _Bool supports_transparency=*format!='J'; // only JPEG does not support transparency
      MagickRelinquishMemory(format);
      PixelSetColor(pwand, cmd->color[0]? cmd->color : (supports_transparency?"none":"white"));
      MagickSetImageBackgroundColor(image, pwand);
      int offset_x=cmd->gravity_left? 0 : (cmd->gravity_right? -(max_width-width) : -(max_width-width)/2);
      int offset_y=cmd->gravity_top? 0 : (cmd->gravity_bottom? -(max_height-height) : -(max_height-height)/2);
      MagickExtentImage(image, max_width, max_height, offset_x, offset_y);
      width=max_width;
      height=max_height;
      DestroyPixelWand(pwand);
      dirty=1;
    }
  }
  else if (cmd->cmd=='f') {
    if (width>max_width || height>max_height) { // don't enlarge!
      double scale_x=(double)max_width/(double)width;
      double scale_y=(double)max_height/(double)height;
      if (scale_x<scale_y) {
        width=max_width;
        height=round(scale_x*height);
      }
      else {
        width=round(scale_y*width);
        height=max_height;
      }
      MagickResizeImage(image, width, height, LanczosFilter, 1);
      dirty=1;
    }
    if (width!=max_width || height!=max_height) {
      PixelWand* pwand=NewPixelWand();
     _Bool supports_transparency=MagickGetImageFormat(image)[0]!='J'; // only JPEG does not support transparency
      PixelSetColor(pwand, cmd->color[0]? cmd->color : (supports_transparency?"none":"white"));
      MagickSetImageBackgroundColor(image, pwand);
      int offset_x=cmd->gravity_left? 0 : (cmd->gravity_right? -(max_width-width) : -(max_width-width)/2);
      int offset_y=cmd->gravity_top? 0 : (cmd->gravity_bottom? -(max_height-height) : -(max_height-height)/2);
      MagickExtentImage(image, max_width, max_height, offset_x, offset_y);
      width=max_width;
      height=max_height;
      DestroyPixelWand(pwand);
      dirty=1;
    }
  }
  if (!cmd->dont_watermark && width>=200 && height>=200) { // don't watermark images smaller than 200x200
    char wpath[2096];
    if (locate_watermark(fpath, path_info, wpath)) {
      MagickWand* watermark=NewMagickWand();
      MagickReadImage(watermark, wpath);
      int wwidth=MagickGetImageWidth(watermark);
      int wheight=MagickGetImageHeight(watermark);
      if (width>=2*wwidth && height>=2*wheight) { // don't watermark images smaller than 2 x watermark
        MagickEvaluateImageChannel(watermark, AlphaChannel, MultiplyEvaluateOperator, 0.5);
        MagickCompositeImage(image, watermark, OverCompositeOp, width-wwidth-10, height-wheight-10);
        dirty=1;
      }
      DestroyMagickWand(watermark);
    }
  }
  return dirty;
}

static int decode_cmd(char* path, isrv_cmd* cmd) { // modifies path
  memset(cmd, 0, sizeof(*cmd));
  char* filename=strrchr(path, '/');
  if (filename) filename++;
  else filename=path;
  if (!strcmp(filename, WATERMARK_FILE_NAME)) {
    cmd->dont_watermark=1;
    return -1;
  }
  else {
    char* p;
    for (p=strchr(filename, '.'); p; p=strchr(p+1, '.')) {
      if (p[1]=='n' && p[2]=='o' && p[3]=='w' && p[4]=='m' && p[5]=='k') {
        cmd->dont_watermark=1;
        break;
      }
    }
  }
  char* ext=strrchr(filename, '.');
  if (!ext) return -1;
  *ext='\0'; // cut
  char* cp=strrchr(filename, '.');
  *ext='.'; // restore
  if (!cp) return -1;
  char* cc=cp;
  char c=*++cp;
  int w=0, h=0;
  if (c=='c' || c=='f') {
    cp++;
  }
  else {
    if (!(c=='x' || (c>='1' && c<='9'))) return -1;
    c='s';
  }
  while (*cp>='0' && *cp<='9') {
    if (*cp=='0' && w==0) return -1; // no leading zeros
    w=w*10+(*cp-'0');
    cp++;
  }
  if (*cp!='x') return -1;
  cp++;
  while (*cp>='0' && *cp<='9') {
    if (*cp=='0' && h==0) return -1; // no leading zeros
    h=h*10+(*cp-'0');
    cp++;
  }
  if (c=='c' || c=='f') {
    if (*cp=='t') {
      cmd->gravity_top=1;
      cp++;
      if (*cp=='r') {
        cmd->gravity_right=1;
        cp++;
      }
      else if (*cp=='l') {
        cmd->gravity_left=1;
        cp++;
      }
    }
    else if (*cp=='b') {
      cmd->gravity_bottom=1;
      cp++;
      if (*cp=='r') {
        cmd->gravity_right=1;
        cp++;
      }
      else if (*cp=='l') {
        cmd->gravity_left=1;
        cp++;
      }
    }
    else if (*cp=='r') {
      cmd->gravity_right=1;
      cp++;
    }
    else if (*cp=='l') {
      cmd->gravity_left=1;
      cp++;
    }
  }
  if (*cp=='.') {
    if ((c!='s' && (w==0 || h==0)) || (c=='s' && w==0 && h==0)) return -1; // wildcards allowed for scale command only and only for one dimension
    cmd->cmd=c;
    cmd->width=w;
    cmd->height=h;
    memmove(cc, ext, strlen(ext)+1);
    if (!strcmp(filename, WATERMARK_FILE_NAME)) cmd->dont_watermark=1;
    return 0;
  }
  if (c!='f' && c!='c') return -1;
  if (*cp!='x') return -1;
  cp++;
  int i;
  cmd->color[0]='#';
  for (i=1; i<=6; i++, cp++) {
    if ((*cp>='0' && *cp<='9') || (*cp>='A' && *cp<='F')) cmd->color[i]=*cp;
    else return -1;
  }
  if (*cp!='.') return -1;
  cmd->cmd=c;
  cmd->width=w;
  cmd->height=h;
  memmove(cc, ext, strlen(ext)+1);
  if (!strcmp(filename, WATERMARK_FILE_NAME)) cmd->dont_watermark=1;
  return 0;
}

static int copy_file(const char* src, const char* dst) {
  int sfd=open(src, O_RDONLY);
  if (sfd==-1) return -1;
  int dfd=open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (dfd==-1) {
    close(sfd);
    return -1;
  }
  char buf[4096];
  ssize_t cnt;
  while ((cnt=read(sfd, buf, sizeof(buf)))>0) {
    if (write(dfd, buf, cnt)!=cnt) {
      close(sfd);
      close(dfd);
      unlink(dst);
      return -1;
    }
  }
  close(sfd);
  close(dfd);
  return 0;
}

static inline char HEX_DIGIT(char n) { n&=0xf; return n<10? n+'0' : n-10+'A'; }

#ifdef WITH_SSL

static void sha1sign(const char* str, unsigned str_len, char* result) {
  struct sha1_ctx sha;
  uint8_t res[SHA1_DIGEST_SIZE];
  sha1_init(&sha);
  sha1_update(&sha, str_len, (uint8_t*)str);
  sha1_update(&sha, sizeof(CMD_SIGN_SECRET_KEY)-1, (uint8_t*)CMD_SIGN_SECRET_KEY);
  sha1_digest(&sha, SHA1_DIGEST_SIZE, res);
  int i;
  char* p=result;
  for (i=0; i<SHA1_DIGEST_SIZE; i++) {
    uint32_t n=res[i];
    *p++=HEX_DIGIT(n>>4);
    *p++=HEX_DIGIT(n);
  }
  *p='\0';
}

#else

static void sha1sign(const char* str, unsigned str_len, char* result) {
  SHA1Context sha;
  SHA1Reset(&sha);
  SHA1Input(&sha, (const unsigned char*)str, str_len);
  SHA1Input(&sha, (const unsigned char*)CMD_SIGN_SECRET_KEY, sizeof(CMD_SIGN_SECRET_KEY)-1);
  SHA1Result(&sha);
  int i;
  char* p=result;
  for (i=0; i<5; i++) {
    uint32_t n=sha.Message_Digest[i];
    *p++=HEX_DIGIT(n>>28);
    *p++=HEX_DIGIT(n>>24);
    *p++=HEX_DIGIT(n>>20);
    *p++=HEX_DIGIT(n>>16);
    *p++=HEX_DIGIT(n>>12);
    *p++=HEX_DIGIT(n>>8);
    *p++=HEX_DIGIT(n>>4);
    *p++=HEX_DIGIT(n);
  }
  *p='\0';
}

#endif

static nxweb_result image_serve_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!req->get_method || req->content_length) return NXWEB_NEXT; // do not respond to POST requests, etc.

  nxweb_handler* handler=conn->handler;
  const char* document_root=handler->dir;
  const char* image_cache_root=handler->cdir;
  assert(document_root);
  assert(image_cache_root);

  char fpath[MAX_PATH];
  int rlen=strlen(document_root);
  assert(rlen<sizeof(fpath));
  strcpy(fpath, document_root);
  char* path_info=fpath+rlen;
  const char* query_string=strchr(req->path_info, '?');
  int plen=query_string? query_string-req->path_info : strlen(req->path_info);
  if (rlen+plen>sizeof(fpath)-64) { // leave room for index file name etc.
    nxweb_send_http_error(resp, 414, "Request-URI Too Long");
    return NXWEB_ERROR;
  }
  strncat(path_info, req->path_info, plen);
  nxweb_url_decode(path_info, 0);
  plen=strlen(path_info);
  if (plen>0 && path_info[plen-1]=='/') { // directory index
    strcat(path_info+plen, INDEX_FILE);
  }

  if (nxweb_remove_dots_from_uri_path(path_info)) {
    //nxweb_send_http_error(resp, 404, "Not Found");
    return NXWEB_NEXT;
  }
  plen=strlen(path_info);

  const nxweb_mime_type* mtype=nxweb_get_mime_type_by_ext(path_info);
  if (!strcmp(mtype->ext, "jpg") || !strcmp(mtype->ext, "png") || !strcmp(mtype->ext, "gif") || !strcmp(mtype->ext, "jpeg")) {
    char ipath[MAX_PATH];
    int clen=strlen(image_cache_root);
    assert(clen<sizeof(ipath));
    strcpy(ipath, image_cache_root);
    char* ipath_info=ipath+clen;
    if (clen+plen>sizeof(ipath)-16) { // leave room for .gz?
      nxweb_send_http_error(resp, 414, "Request-URI Too Long");
      return NXWEB_ERROR;
    }
    strcpy(ipath_info, path_info);

    nxweb_result res=nxweb_sendfile_try(conn, resp, ipath, ipath_info, req->if_modified_since, handler->cache? DEFAULT_CACHED_TIME : 0, 0, 0, 0, mtype);
    if (res!=NXWEB_NEXT) {
      return res;
    }

    if (nxweb_mkpath(ipath, 0755)) {
      nxweb_log_error("can't make path to store file %s", ipath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }

    isrv_cmd cmd;
    if (!decode_cmd(path_info, &cmd)) {
      // check if cmd is allowed
      isrv_cmd* ac=allowed_cmds;
      _Bool allowed=0;
      while (ac->cmd) {
        if (cmd.cmd==ac->cmd && cmd.width==ac->width && cmd.height==ac->height
                && cmd.gravity_top==ac->gravity_top && cmd.gravity_bottom==ac->gravity_bottom
                && cmd.gravity_left==ac->gravity_left && cmd.gravity_right==ac->gravity_right
                && !strcmp(cmd.color, ac->color)) {
          allowed=1;
          break;
        }
        ac++;
      }
      if (!allowed && query_string) {
        char signature[41];
        sha1sign(req->uri, query_string - req->uri, signature);
        //nxweb_log_error("uri=%s sha1sign=%s query_string=%s", req->uri, signature, query_string+1);
        if (!strcmp(query_string+1, signature)) allowed=1;
      }
      if (!allowed) {
        nxweb_send_http_error(resp, 403, "Forbidden");
        return NXWEB_ERROR;
      }
    }

    struct stat finfo;
    if (stat(fpath, &finfo)==-1) {
      // source image not found
      return NXWEB_NEXT;
    }

    MagickWand* image=NewMagickWand();
    if (!MagickReadImage(image, fpath)) {
      DestroyMagickWand(image);
      nxweb_log_error("MagickReadImage(%s) failed", fpath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
    MagickStripImage(image);

    if (process_cmd(fpath, path_info, image, &cmd)) {
      nxweb_log_error("writing image file %s", ipath);
      if (!MagickWriteImage(image, ipath)) {
        DestroyMagickWand(image);
        nxweb_log_error("MagickWriteImage(%s) failed", ipath);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        return NXWEB_ERROR;
      }
      DestroyMagickWand(image);
    }
    else {
      // processing changed nothing => just copy the original
      DestroyMagickWand(image);
      nxweb_log_error("copying image file %s", ipath);
      if (copy_file(fpath, ipath)) {
        nxweb_log_error("error %d copying image file %s", errno, ipath);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        return NXWEB_ERROR;
      }
    }

    struct utimbuf ut={.actime=finfo.st_atime, .modtime=finfo.st_mtime};
    utime(ipath, &ut);

    return nxweb_sendfile_try(conn, resp, ipath, ipath_info, req->if_modified_since, handler->cache? DEFAULT_CACHED_TIME : 0, 1, 0, 0, mtype);
  }
  return nxweb_sendfile_try(conn, resp, fpath, path_info, req->if_modified_since, handler->cache? DEFAULT_CACHED_TIME : 0, 1, req->accept_gzip_encoding, 0, mtype);
}

nxweb_handler image_serve_handler={.on_select=image_serve_on_select, .flags=NXWEB_HANDLE_GET};

NXWEB_SET_HANDLER(image_serve, "/is", &image_serve_handler, .priority=10000, .dir="html", .cdir="test/image_serve/cache", .cache=1);
