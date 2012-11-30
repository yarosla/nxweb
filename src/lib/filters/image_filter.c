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

#ifdef WITH_NETTLE
#include <nettle/sha.h>
#else
#include "deps/sha1-c/sha1.h"
#endif

/**
 * image filter can do the following:
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

/**
 * Only commands listed below will be allowed (to protect from DoS).
 * Arbitrary command can also be executed if signed by QUERY_STRING.
 * Eg. GET /is/benchmark/watermark.c500x500.png?1B4A0DF16BFF7050B9C7C6A63576F8F2D1695CF1
 * Signature calculated as SHA1(uri_path+CMD_SIGN_SECRET_KEY)
 */

static nxweb_image_filter_cmd allowed_cmds[]={
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
  {.cmd='f', .width=500, .height=500, .color="#FF8800"},
  {.cmd=0}
};


static int image_filter_init() {
  MagickWandGenesis();
  return 0;
}

static void image_filter_finalize() {
  MagickWandTerminus();
}

NXWEB_MODULE(image_filter, .on_server_startup=image_filter_init, .on_server_shutdown=image_filter_finalize);

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

static int process_cmd(const char* fpath, const char* path_info, MagickWand* image, nxweb_image_filter_cmd* cmd) {
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
  if (!cmd->dont_watermark && width>=200 && height>=200) { // don't watermark images smaller than 200x200
    char wpath[2096];
    if (locate_watermark(fpath, path_info, wpath)) {
      MagickWand* watermark=NewMagickWand();
      MagickReadImage(watermark, wpath);
      int wwidth=MagickGetImageWidth(watermark);
      int wheight=MagickGetImageHeight(watermark);

      /*
       * Now use image page definition to scale down and position watermark.
       * Use ImageMagick command line tool to setup:
       *   convert watermark.png -page '800x800-20-20' watermark.png
       * This means for nxweb that watermark image is for 800x800 images and above.
       * For images smaller than 800x800 the watermark should be scaled down proportionally.
       * Offset -20-20 means that watermark should be put in the lower-right corner 20px from the edge.
       * When watermark gets scaled down offsets should be scaled down too.
       */
      size_t wpage_width=0, wpage_height=0;
      ssize_t woffset_x=0, woffset_y=0;
      MagickGetImagePage(watermark, &wpage_width, &wpage_height, &woffset_x, &woffset_y);
      woffset_x=(int32_t)woffset_x;
      woffset_y=(int32_t)woffset_y;
      nxweb_log_error("watermark found: %s size %dx%d page %ldx%ld offs %ldx%ld", wpath, wwidth, wheight, wpage_width, wpage_height, woffset_x, woffset_y);
      if (wpage_width>wwidth && wpage_height>wheight) { // page defined => watermark file has layout
        if (width<wpage_width || height<wpage_height) { // scale watermark down
          double scale_x=(double)width/(double)wpage_width;
          double scale_y=(double)height/(double)wpage_height;
          double scale=scale_x<scale_y? scale_x : scale_y;
          wwidth=round(scale*wwidth);
          wheight=round(scale*wheight);
          woffset_x=round(scale*woffset_x);
          woffset_y=round(scale*woffset_y);
          MagickResizeImage(watermark, wwidth, wheight, LanczosFilter, 1);
        }
        MagickEvaluateImageChannel(watermark, AlphaChannel, MultiplyEvaluateOperator, 0.5);
        ssize_t x=woffset_x<=0? width-wwidth+woffset_x : woffset_x;
        ssize_t y=woffset_y<=0? height-wheight+woffset_y : woffset_y;
        MagickCompositeImage(image, watermark, OverCompositeOp, x, y);
        dirty=1;
      }
      else if (width>=2*wwidth && height>=2*wheight) { // don't watermark images smaller than 2 x watermark
        MagickEvaluateImageChannel(watermark, AlphaChannel, MultiplyEvaluateOperator, 0.5);
        MagickCompositeImage(image, watermark, OverCompositeOp, width-wwidth-10, height-wheight-10);
        dirty=1;
      }
      DestroyMagickWand(watermark);
    }
  }
  return dirty;
}

static int decode_cmd(char* uri, nxweb_image_filter_cmd* cmd, nxb_buffer* nxb) { // modifies uri; extracts cmd string allocating it in nxb
  memset(cmd, 0, sizeof(*cmd));
  char* q=strchr(uri, '?');
  if (q) *q='\0'; // cut query
  char* filename=strrchr(uri, '/');
  if (filename) filename++;
  else filename=uri;
  int fnlen=q? (q-filename):strlen(filename);
  char* ext=strrchr(filename, '.');
  if (ext) cmd->mtype=nxweb_get_mime_type_by_ext(ext+1);
  if (q) *q='?'; // restore
  if (!ext || !cmd->mtype->image) return 0;
  if (!strncmp(filename, WATERMARK_FILE_NAME, fnlen)) {
    cmd->dont_watermark=1;
    return 0;
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
  *ext='\0'; // cut
  char* cp=strrchr(filename, '.');
  *ext='.'; // restore
  if (!cp) return 0;
  char* cc=cp;
  char c=*++cp;
  int w=0, h=0;
  if (c=='c' || c=='f') {
    cp++;
  }
  else {
    if (!(c=='x' || (c>='1' && c<='9'))) return 0;
    c='s';
  }
  while (*cp>='0' && *cp<='9') {
    if (*cp=='0' && w==0) return 0; // no leading zeros allowed
    w=w*10+(*cp-'0');
    cp++;
  }
  if (*cp!='x') return 0;
  cp++;
  while (*cp>='0' && *cp<='9') {
    if (*cp=='0' && h==0) return 0; // no leading zeros
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
  if ((c!='s' && (w==0 || h==0)) || (c=='s' && w==0 && h==0)) return 0; // wildcards allowed for scale command only and only for one dimension
  if (*cp!='.') {
    if (c!='f' && c!='c') return 0;
    if (*cp!='x') return 0;
    cp++;
    int i;
    cmd->color[0]='#';
    for (i=1; i<=6; i++, cp++) {
      if ((*cp>='0' && *cp<='9') || (*cp>='A' && *cp<='F')) cmd->color[i]=*cp;
      else return 0;
    }
    if (*cp!='.') return 0;
  }
  cmd->cmd=c;
  cmd->width=w;
  cmd->height=h;
  int cmd_string_len=ext-cc;
  cmd->cmd_string=nxb_copy_obj(nxb, cc, cmd_string_len+1);
  cmd->cmd_string[cmd_string_len]='\0';
  if (q) cmd->query_string=nxb_copy_obj(nxb, q+1, strlen(q));
  //*q='\0';
  memmove(cc, ext, strlen(ext)+1);
  fnlen-=cmd_string_len;
  if (!strncmp(filename, WATERMARK_FILE_NAME, fnlen)) cmd->dont_watermark=1;
  return 1;
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

#ifdef WITH_NETTLE

static void sha1sign(const char* str, unsigned str_len, const char* key, char* result) {
  struct sha1_ctx sha;
  uint8_t res[SHA1_DIGEST_SIZE];
  sha1_init(&sha);
  sha1_update(&sha, str_len, (uint8_t*)str);
  sha1_update(&sha, strlen(key), (uint8_t*)key);
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

static void sha1sign(const char* str, unsigned str_len, const char* key, char* result) {
  SHA1Context sha;
  SHA1Reset(&sha);
  SHA1Input(&sha, (const unsigned char*)str, str_len);
  SHA1Input(&sha, (const unsigned char*)key, strlen(key));
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

typedef struct img_filter_data {
  nxweb_filter_data fdata;
  nxweb_image_filter_cmd cmd;
} img_filter_data;

static nxweb_filter_data* img_init(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(img_filter_data));
  return fdata;
}

static const char* img_decode_uri(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* uri) {
  img_filter_data* ifdata=(img_filter_data*)fdata;
  char* uri_copy=nxb_copy_obj(req->nxb, uri, strlen(uri)+1);
  decode_cmd(uri_copy, &ifdata->cmd, req->nxb);
  if (!ifdata->cmd.mtype || !ifdata->cmd.mtype->image) fdata->bypass=1;
  return uri_copy;
}

static nxweb_result img_translate_cache_key(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* key, int root_len) {
  // if filter does not store its own cache file
  // it must not store cache_key and just return NXWEB_NEXT
  // it must not implement serve_from_cache either
  if (!*key) return NXWEB_OK;
  if (*key==' ') { // virtual key
    fdata->bypass=1;
    return NXWEB_NEXT;
  }
  assert(conn->handler->img_dir);
  img_filter_data* ifdata=(img_filter_data*)fdata;
  int plen=strlen(key)-root_len;
  int rlen=strlen(conn->handler->img_dir);
  int cmd_len=ifdata->cmd.cmd_string? strlen(ifdata->cmd.cmd_string) : 0;
  char* img_key=nxb_calloc_obj(req->nxb, rlen+plen+cmd_len+1);
  strcpy(img_key, conn->handler->img_dir);
  strcpy(img_key+rlen, key+root_len);
  const char* ext=strrchr(key+root_len, '.');
  assert(ext);
  int ext_len=plen+root_len-(ext-key);
  if (cmd_len) strcpy(img_key+rlen+plen-ext_len, ifdata->cmd.cmd_string);
  strcpy(img_key+rlen+plen-ext_len+cmd_len, ext);
  fdata->cache_key=img_key;
  fdata->cache_key_root_len=rlen;
  return NXWEB_OK;
}

static nxweb_result img_serve_from_cache(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  nxweb_send_file(resp, (char*)fdata->cache_key, fdata->cache_key_root_len, &fdata->cache_key_finfo, 0, 0, 0, resp->mtype, conn->handler->charset);
  return NXWEB_OK;
}

static nxweb_result img_do_filter(struct nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;
  assert(fdata->cache_key);
  assert(resp->sendfile_path);
  assert(resp->content_length>0 && resp->sendfile_offset==0 && resp->sendfile_end==resp->sendfile_info.st_size &&  resp->sendfile_end==resp->content_length);
  int rlen=fdata->cache_key_root_len;
  const char* fpath=fdata->cache_key;
  img_filter_data* ifdata=(img_filter_data*)fdata;

  if (ifdata->cmd.cmd) {
    // check if cmd is allowed
    const nxweb_image_filter_cmd* ac=conn->handler->allowed_cmds? conn->handler->allowed_cmds : allowed_cmds;
    _Bool allowed=0;
    while (ac->cmd) {
      if (ifdata->cmd.cmd==ac->cmd && ifdata->cmd.width==ac->width && ifdata->cmd.height==ac->height
              && ifdata->cmd.gravity_top==ac->gravity_top && ifdata->cmd.gravity_bottom==ac->gravity_bottom
              && ifdata->cmd.gravity_left==ac->gravity_left && ifdata->cmd.gravity_right==ac->gravity_right
              && !strcmp(ifdata->cmd.color, ac->color)) {
        allowed=1;
        break;
      }
      ac++;
    }
    if (!allowed && ifdata->cmd.query_string) {
      char signature[41];
      sha1sign(fpath+rlen, strlen(fpath+rlen), conn->handler->key? conn->handler->key : CMD_SIGN_SECRET_KEY, signature);
      nxweb_log_error("path=%s sha1sign=%s query_string=%s", fpath+rlen, signature, ifdata->cmd.query_string);
      if (!strcmp(ifdata->cmd.query_string, signature)) allowed=1;
    }
    if (!allowed) {
      nxweb_send_http_error(resp, 403, "Forbidden");
      return NXWEB_ERROR;
    }
  }

  if (nxweb_mkpath((char*)fpath, 0755)<0) {
    nxweb_log_error("nxweb_mkpath(%s) failed", fpath);
    return NXWEB_ERROR;
  }

  if (resp->sendfile_fd>0) {
    close(resp->sendfile_fd);
  }
  resp->sendfile_fd=0;

  MagickWand* image=NewMagickWand();
  if (!MagickReadImage(image, resp->sendfile_path)) {
    DestroyMagickWand(image);
    nxweb_log_error("MagickReadImage(%s) failed", resp->sendfile_path);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    return NXWEB_ERROR;
  }
  MagickStripImage(image);

  if (process_cmd(resp->sendfile_path, resp->sendfile_path+resp->sendfile_path_root_len, image, &ifdata->cmd)) {
    nxweb_log_error("writing image file %s", fpath);
    if (!MagickWriteImage(image, fpath)) {
      DestroyMagickWand(image);
      nxweb_log_error("MagickWriteImage(%s) failed", fpath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
    DestroyMagickWand(image);
  }
  else {
    // processing changed nothing => just copy the original
    DestroyMagickWand(image);
    nxweb_log_error("copying image file %s", fpath);
    if (copy_file(resp->sendfile_path, fpath)) {
      nxweb_log_error("error %d copying image file %s", errno, fpath);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }
  }

  struct utimbuf ut={.actime=resp->sendfile_info.st_atime, .modtime=resp->sendfile_info.st_mtime};
  utime(fpath, &ut);

  nxweb_log_error("image processed %s -> %s", resp->sendfile_path, fpath);

  resp->sendfile_path=fpath;
  resp->sendfile_path_root_len=rlen;
  if (stat(fpath, &resp->sendfile_info)==-1) {
    nxweb_log_error("can't stat processed image %s", fpath);
    return NXWEB_ERROR;
  }
  resp->sendfile_end=
  resp->content_length=resp->sendfile_info.st_size;
  resp->last_modified=resp->sendfile_info.st_mtime;
  resp->content_out=0; // reset content_out
  resp->content=0;
  return NXWEB_OK;
}

nxweb_filter image_filter={.name="image", .init=img_init, .translate_cache_key=img_translate_cache_key,
        .decode_uri=img_decode_uri, .serve_from_cache=img_serve_from_cache, .do_filter=img_do_filter};

