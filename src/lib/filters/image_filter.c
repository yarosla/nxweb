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

typedef struct nxweb_filter_image {
  nxweb_filter base;
  const char* cache_dir;
  nxweb_image_filter_cmd* allowed_cmds;
  const char* sign_key;
  _Bool use_lock:1;
} nxweb_filter_image;

/**
 * Only commands listed below will be allowed (to protect from DoS).
 * Arbitrary command can also be executed if signed by QUERY_STRING.
 * Eg. GET /is/benchmark/watermark.c500x500.png?1B4A0DF16BFF7050B9C7C6A63576F8F2D1695CF1
 * Signature calculated as SHA1(uri_path+CMD_SIGN_SECRET_KEY)
 */

static nxweb_image_filter_cmd default_allowed_cmds[]={
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

static pthread_mutex_t image_filter_processing_lock;

static int image_filter_init() {
  MagickWandGenesis();
  nxweb_log_error("MagickResourceLimits: mem=%lu, map=%lu, area=%lu", (long)MagickGetResourceLimit(MemoryResource), (long)MagickGetResourceLimit(MemoryResource), (long)MagickGetResourceLimit(AreaResource));
  pthread_mutex_init(&image_filter_processing_lock, 0);
  return 0;
}

static void image_filter_finalize() {
  MagickWandTerminus();
}

static void image_filter_on_config(const nx_json* js);

NXWEB_MODULE(image_filter, .on_server_startup=image_filter_init, .on_server_shutdown=image_filter_finalize, .on_config=image_filter_on_config);

static int locate_watermark(const char* fpath, int doc_root_len, char* wpath) {
  strcpy(wpath, fpath);
  const char* wpath_info=wpath+doc_root_len;
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

static void parse_crop_offset(const unsigned char* instr, size_t len, int* x, int *y) {
  // offset format: "co:100,-120"
  *x=*y=0;
  if (!instr || len<6) return;
  if (instr[0]!='c' || instr[1]!='o' || instr[2]!=':') return; // prefix "co:"
  char buf[16];
  len-=3;
  if (len>sizeof(buf)-1) return;
  strncpy(buf, (char*)(instr+3), len);
  buf[len]='\0';
  char* p=buf;
  while (*p!=',') {
    if (!*p++) return;
  }
  *p++='\0';
  if (!*p) return;
  *x=atoi(buf);
  *y=atoi(p);
}

static unsigned char* find_iptc_record(unsigned char* profile, size_t size, unsigned char record_id, unsigned char tag_id, size_t* len) {
  unsigned char* p=profile;
  unsigned char* end=p+size;
  // http://www.iptc.org/std/IIM/4.1/specification/IIMV4.1.pdf
  while (p+5<=end) {
    if (*p++!=0x1c) break; // wrong marker; corrupted record
    unsigned char rid=*p++;
    unsigned char tid=*p++;
    size_t sz=*p++;
    sz=(sz<<8)|*p++;
    if (sz&0x8000) { // extended tag
      break; // not supported
    }
    if (p+sz>end) break; // overflow; corrupted record
    if (rid==record_id && tid==tag_id) {
      *len=sz;
      return p;
    }
    p+=sz;
  }
  return 0;
}

static void find_crop_offset(MagickWand* image, int* x, int* y) {
  *x=*y=0;
  size_t len;
  unsigned char* profile=MagickGetImageProfile(image, "iptc", &len);
  if (profile) {
    unsigned char* v=find_iptc_record(profile, len, 2, 0x28, &len); // "Iptc.Application2.SpecialInstructions"
    if (v) {
      parse_crop_offset(v, len, x, y);
    }
    MagickRelinquishMemory(profile);
  }
}

static size_t image_width(MagickWand* image) {
  // take image orientation into account
  switch(MagickGetImageOrientation(image))
  {
    default:
    case UndefinedOrientation:
    case TopLeftOrientation:
    case TopRightOrientation:
    case BottomLeftOrientation:
    case BottomRightOrientation:
      return MagickGetImageWidth(image);

    case LeftTopOrientation:
    case RightTopOrientation:
    case RightBottomOrientation:
    case LeftBottomOrientation:
      return MagickGetImageHeight(image);
  }
}

static size_t image_height(MagickWand* image) {
  // take image orientation into account
  switch(MagickGetImageOrientation(image))
  {
    default:
    case UndefinedOrientation:
    case TopLeftOrientation:
    case TopRightOrientation:
    case BottomLeftOrientation:
    case BottomRightOrientation:
      return MagickGetImageHeight(image);

    case LeftTopOrientation:
    case RightTopOrientation:
    case RightBottomOrientation:
    case LeftBottomOrientation:
      return MagickGetImageWidth(image);
  }
}

static void image_auto_orient(MagickWand* image) {
  // fix image orientation
  OrientationType orientation=MagickGetImageOrientation(image);
  // nxweb_log_error("orientation %d -> %d", orientation, TopLeftOrientation);
  PixelWand* pwand=0;
  switch(orientation)
  {
    case UndefinedOrientation:
    case TopLeftOrientation:
    default:
      break;
    case TopRightOrientation:
      MagickFlopImage(image);
      break;
    case BottomRightOrientation:
      pwand=NewPixelWand();
      MagickRotateImage(image, pwand, 180.0);
    case BottomLeftOrientation:
      MagickFlipImage(image);
      break;
    case LeftTopOrientation:
      MagickTransposeImage(image);
      break;
    case RightTopOrientation:
      pwand=NewPixelWand();
      MagickRotateImage(image, pwand, 90.0);
      break;
    case RightBottomOrientation:
      MagickTransverseImage(image);
      break;
    case LeftBottomOrientation:
      pwand=NewPixelWand();
      MagickRotateImage(image, pwand, 270.0);
      break;
  }
  if (pwand) DestroyPixelWand(pwand);
  MagickSetImageOrientation(image, TopLeftOrientation);
}

static int process_cmd(const char* fpath, int doc_root_len, MagickWand* image, nxweb_image_filter_cmd* cmd) {
  size_t width=image_width(image);
  size_t height=image_height(image);
  // nxweb_log_error("orientation=%d, owidth=%d, oheight=%d, width=%d, height=%d", MagickGetImageOrientation(image), MagickGetImageWidth(image), MagickGetImageHeight(image), width, height);
  if (width<=0 || height<=0) {
    nxweb_log_error("illegal width or height of image %s", fpath);
    return 0;
  }
  int dirty=0;
  size_t max_width=(size_t)cmd->width;
  size_t max_height=(size_t)cmd->height;
  if (cmd->cmd=='s') {
    if (max_width==0) max_width=1000000;
    if (max_height==0) max_height=1000000;
    if (width>max_width || height>max_height) { // don't enlarge!
      double scale_x=(double)max_width/(double)width;
      double scale_y=(double)max_height/(double)height;
      if (scale_x<scale_y) {
        width=max_width;
        height=(size_t)round(scale_x*height);
      }
      else {
        width=(size_t)round(scale_y*width);
        height=max_height;
      }
      image_auto_orient(image);
      MagickResizeImage(image, width, height, LanczosFilter, 1);
      dirty=1;
    }
  }
  else if (cmd->cmd=='c') {
    double scale_x=(double)max_width/(double)width;
    double scale_y=(double)max_height/(double)height;
    double scale=max(scale_x, scale_y);
    if (scale<1.) { // don't enlarge!
      width=(size_t)round(scale*width);
      height=(size_t)round(scale*height);
      image_auto_orient(image);
      MagickResizeImage(image, width, height, LanczosFilter, 1);
      dirty=1;
    }
    if (width!=max_width || height!=max_height) { // need to crop or extend
      int crop_offset_x, crop_offset_y;
      find_crop_offset(image, &crop_offset_x, &crop_offset_y);
      if ((crop_offset_x || crop_offset_y) && scale<1.) {
        crop_offset_x=(int)round(scale*crop_offset_x);
        crop_offset_y=(int)round(scale*crop_offset_y);
      }
      if (cmd->gravity_left) crop_offset_x=-(int)width;
      else if (cmd->gravity_right) crop_offset_x=(int)width;
      if (cmd->gravity_top) crop_offset_y=-(int)height;
      else if (cmd->gravity_bottom) crop_offset_y=(int)height;
      PixelWand* pwand=0;
      if (max_width>width || max_height>height) {
        pwand=NewPixelWand();
        char* format=MagickGetImageFormat(image); // returns "JPEG", "GIF", or "PNG"
        _Bool supports_transparency=*format!='J'; // only JPEG does not support transparency
        MagickRelinquishMemory(format);
        PixelSetColor(pwand, cmd->color[0]? cmd->color : (supports_transparency?"none":"white"));
        MagickSetImageBackgroundColor(image, pwand);
      }
      ssize_t offset_x, offset_y;
      ssize_t cut_x=width-max_width;
      if (cut_x<=0) {
        offset_x=cut_x/2;
      }
      else {
        offset_x=cut_x/2 + crop_offset_x;
        if (offset_x<0) offset_x=0;
        else if (offset_x>cut_x) offset_x=cut_x;
      }
      ssize_t cut_y=height-max_height;
      if (cut_y<=0) {
        offset_y=cut_y/2;
      }
      else {
        offset_y=cut_y/2 + crop_offset_y;
        if (offset_y<0) offset_y=0;
        else if (offset_y>cut_y) offset_y=cut_y;
      }
      image_auto_orient(image);
      MagickExtentImage(image, max_width, max_height, offset_x, offset_y);
      width=max_width;
      height=max_height;
      if (pwand) DestroyPixelWand(pwand);
      dirty=1;
    }
  }
  else if (cmd->cmd=='f') {
    if (width>max_width || height>max_height) { // don't enlarge!
      double scale_x=(double)max_width/(double)width;
      double scale_y=(double)max_height/(double)height;
      if (scale_x<scale_y) {
        width=max_width;
        height=(size_t)round(scale_x*height);
      }
      else {
        width=(size_t)round(scale_y*width);
        height=max_height;
      }
      image_auto_orient(image);
      MagickResizeImage(image, width, height, LanczosFilter, 1);
      dirty=1;
    }
    if (width!=max_width || height!=max_height) {
      image_auto_orient(image);
      PixelWand* pwand=NewPixelWand();
      char* format=MagickGetImageFormat(image); // returns "JPEG", "GIF", or "PNG"
      _Bool supports_transparency=*format!='J'; // only JPEG does not support transparency
      MagickRelinquishMemory(format);
      PixelSetColor(pwand, cmd->color[0]? cmd->color : (supports_transparency?"none":"white"));
      MagickSetImageBackgroundColor(image, pwand);
      ssize_t offset_x=cmd->gravity_left? 0 : (cmd->gravity_right? -(ssize_t)(max_width-width) : -(ssize_t)(max_width-width)/2);
      ssize_t offset_y=cmd->gravity_top? 0 : (cmd->gravity_bottom? -(ssize_t)(max_height-height) : -(ssize_t)(max_height-height)/2);
      // nxweb_log_error("%lld %lld MagickExtentImage(%lld, %lld, %lld, %lld)", (long long)width, (long long)height, (long long)max_width, (long long)max_height, (long long)offset_x, (long long)offset_y);
      MagickExtentImage(image, max_width, max_height, offset_x, offset_y);
      width=max_width;
      height=max_height;
      DestroyPixelWand(pwand);
      dirty=1;
    }
  }
  if (!cmd->dont_watermark && width>=200 && height>=200) { // don't watermark images smaller than 200x200
    char wpath[2096];
    assert(strlen(fpath)<sizeof(wpath));
    if (locate_watermark(fpath, doc_root_len, wpath)) {
      MagickWand* watermark=NewMagickWand();
      MagickReadImage(watermark, wpath);
      size_t wwidth=MagickGetImageWidth(watermark);
      size_t wheight=MagickGetImageHeight(watermark);

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
      nxweb_log_info("watermark found: %s size %dx%d page %ldx%ld offs %ldx%ld", wpath, wwidth, wheight, wpage_width, wpage_height, woffset_x, woffset_y);
      if (wpage_width>wwidth && wpage_height>wheight) { // page defined => watermark file has layout
        if (width<wpage_width || height<wpage_height) { // scale watermark down
          double scale_x=(double)width/(double)wpage_width;
          double scale_y=(double)height/(double)wpage_height;
          double scale=scale_x<scale_y? scale_x : scale_y;
          wwidth=(size_t)round(scale*wwidth);
          wheight=(size_t)round(scale*wheight);
          woffset_x=(ssize_t)round(scale*woffset_x);
          woffset_y=(ssize_t)round(scale*woffset_y);
          MagickResizeImage(watermark, wwidth, wheight, LanczosFilter, 1);
        }
        MagickEvaluateImageChannel(watermark, AlphaChannel, MultiplyEvaluateOperator, 0.5);
        ssize_t x=woffset_x<=0? (ssize_t)(width-wwidth+woffset_x) : woffset_x;
        ssize_t y=woffset_y<=0? (ssize_t)(height-wheight+woffset_y) : woffset_y;
        image_auto_orient(image);
        MagickCompositeImage(image, watermark, OverCompositeOp, x, y);
        dirty=1;
      }
      else if (width>=2*wwidth && height>=2*wheight) { // don't watermark images smaller than 2 x watermark
        MagickEvaluateImageChannel(watermark, AlphaChannel, MultiplyEvaluateOperator, 0.5);
        image_auto_orient(image);
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
  cmd->uri_path=nxb_copy_obj(nxb, uri, q? q-uri : strlen(uri));
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
    if (write(dfd, buf, (size_t)cnt)!=cnt) {
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
  nxd_fbuffer fb;
  nxweb_image_filter_cmd cmd;
} img_filter_data;

static nxweb_filter_data* img_init(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxweb_filter_data* fdata=nxb_calloc_obj(req->nxb, sizeof(img_filter_data));
  return fdata;
}

static void img_finalize(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  img_filter_data* ifdata=(img_filter_data*)fdata;
  nxd_fbuffer_finalize(&ifdata->fb);
}

static const char* img_decode_uri(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* uri) {
  img_filter_data* ifdata=(img_filter_data*)fdata;
  char* uri_copy=nxb_copy_obj(req->nxb, uri, strlen(uri)+1);
  decode_cmd(uri_copy, &ifdata->cmd, req->nxb);
  if (!ifdata->cmd.mtype || !ifdata->cmd.mtype->image) {
    fdata->bypass=1;
    return uri;
  }
  return uri_copy;
}

static nxweb_result img_translate_cache_key(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata, const char* cache_key) {
  img_filter_data* ifdata=(img_filter_data*)fdata;
  if (!ifdata->cmd.cmd_string) {
    fdata->cache_key=cache_key;
    return NXWEB_OK;
  }
  // append previously extracted cmd_string
  nxb_buffer* nxb=req->nxb;
  nxb_start_stream(nxb);
  nxb_append_str(nxb, cache_key);
  nxb_append_str(nxb, "$img");
  nxb_append_str(nxb, ifdata->cmd.cmd_string);
  nxb_append_char(nxb, '\0');
  fdata->cache_key=nxb_finish_stream(nxb, 0);
  return NXWEB_OK;
}

static inline void _cleanup_magick_wand(MagickWand* image, _Bool use_lock) {
  DestroyMagickWand(image);
  if (use_lock) pthread_mutex_unlock(&image_filter_processing_lock);
}

static nxweb_result img_do_filter(nxweb_filter* filter, nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxweb_filter_data* fdata) {
  img_filter_data* ifdata=(img_filter_data*)fdata;
  if (resp->status_code && resp->status_code!=200) return NXWEB_OK;
  if (resp->content_length<=0) return NXWEB_OK;
  assert(fdata->cache_key);
  assert(resp->sendfile_path);
  assert(resp->content_length>0 && resp->sendfile_offset==0 && resp->sendfile_end==resp->sendfile_info.st_size &&  resp->sendfile_end==resp->content_length);

  nxweb_filter_image* ifilter=(nxweb_filter_image*)filter;
  assert(ifilter->cache_dir);
  nxb_buffer* nxb=req->nxb;
  nxb_start_stream(nxb);
  nxb_append_str(nxb, ifilter->cache_dir);
  const char* cache_key=ifdata->fdata.cache_key;
  if (cache_key[0]=='.' && cache_key[1]=='.' && cache_key[2]=='/') cache_key+=2; // avoid going up dir tree
  if (*cache_key!='/') nxb_append_char(nxb, '/');
  nxb_append_str(nxb, cache_key);
  nxb_append_char(nxb, '\0');
  /*
  int cmd_len=ifdata->cmd.cmd_string? strlen(ifdata->cmd.cmd_string) : 0;
  nxb_blank(nxb, cmd_len);
  */
  char* fpath=nxb_finish_stream(nxb, 0);
  /*
  if (cmd_len) {
    char* ext=strrchr(fpath, '.');
    int ext_len=strlen(ext);
    assert(ext);
    memmove(ext+cmd_len, ext, ext_len+1);
    memcpy(ext, ifdata->cmd.cmd_string, cmd_len);
  }
  */
  struct stat finfo;

  if (stat(fpath, &finfo)==-1 || finfo.st_mtime!=resp->sendfile_info.st_mtime) {
    if (ifdata->cmd.cmd) {
      // check if cmd is allowed
      const nxweb_image_filter_cmd* ac=ifilter->allowed_cmds;
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
        sha1sign(ifdata->cmd.uri_path, strlen(ifdata->cmd.uri_path), ifilter->sign_key, signature);
        if (!strcmp(ifdata->cmd.query_string, signature)) allowed=1;
        else nxweb_log_warning("img cmd not allowed: path=%s sha1sign=%s query_string=%s", ifdata->cmd.uri_path, signature, ifdata->cmd.query_string);
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

    _Bool use_lock=ifilter->use_lock; // filter config option
    if (use_lock) // serialize calls to ImageMagick to minimize memory footprint; not good for performance
      pthread_mutex_lock(&image_filter_processing_lock);

    MagickWand* image=NewMagickWand();
    if (!MagickReadImage(image, resp->sendfile_path)) {
      _cleanup_magick_wand(image, use_lock);
      nxweb_log_error("MagickReadImage(%s) failed", resp->sendfile_path);
      nxweb_send_http_error(resp, 500, "Internal Server Error");
      return NXWEB_ERROR;
    }

    int doc_root_len;
    if (conn->handler->flags & _NXWEB_HOST_DEPENDENT_DIR) {
      const char* port=strchr(req->host, ':');
      int host_len=port? port - req->host : strlen(req->host);
      doc_root_len=strlen(conn->handler->dir)-(sizeof("{host}")-1)+host_len;
    }
    else {
      doc_root_len=conn->handler->dir? strlen(conn->handler->dir) : 0;
    }
    if (process_cmd(resp->sendfile_path, doc_root_len, image, &ifdata->cmd)) {
      nxweb_log_info("writing image file %s", fpath);
      MagickStripImage(image);
      if (!MagickWriteImage(image, fpath)) {
        _cleanup_magick_wand(image, use_lock);
        nxweb_log_error("MagickWriteImage(%s) failed", fpath);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        return NXWEB_ERROR;
      }
      _cleanup_magick_wand(image, use_lock);
    }
    else {
      // processing changed nothing => just copy the original
      _cleanup_magick_wand(image, use_lock);
      nxweb_log_info("copying image file %s", fpath);
      if (copy_file(resp->sendfile_path, fpath)) {
        nxweb_log_error("error %d copying image file %s", errno, fpath);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        return NXWEB_ERROR;
      }
    }

    struct utimbuf ut={.actime=resp->sendfile_info.st_atime, .modtime=resp->sendfile_info.st_mtime};
    utime(fpath, &ut);

    nxweb_log_info("image processed %s -> %s", resp->sendfile_path, fpath);
    if (stat(fpath, &resp->sendfile_info)==-1) {
      nxweb_log_error("can't stat processed image %s", fpath);
      return NXWEB_ERROR;
    }
  }
  else {
    resp->sendfile_info=finfo;
  }

  resp->sendfile_path=fpath;
  resp->sendfile_end=
  resp->content_length=resp->sendfile_info.st_size;
  resp->last_modified=resp->sendfile_info.st_mtime;
  resp->content=0;

  if (resp->sendfile_fd>0) close(resp->sendfile_fd);
  resp->sendfile_fd=open(resp->sendfile_path, O_RDONLY|O_NONBLOCK);
  if (resp->sendfile_fd!=-1) {
    nxd_fbuffer_init(&ifdata->fb, resp->sendfile_fd, resp->sendfile_offset, resp->sendfile_end);
    resp->content_out=&ifdata->fb.data_out;
  }
  else {
    nxweb_log_error("nxd_http_server_proto_start_sending_response(): can't open %s", resp->sendfile_path);
  }

  return NXWEB_OK;
}

static nxweb_filter* img_config(nxweb_filter* base, const nx_json* json) {
  // handler level config (overrides module-level config for specific handler)
  nxweb_filter_image* f=calloc(1, sizeof(nxweb_filter_image)); // NOTE this will never be freed
  *f=*(nxweb_filter_image*)base;
  f->cache_dir=nx_json_get(json, "cache_dir")->text_value;
  const char* sign_key=nx_json_get(json, "sign_key")->text_value;
  if (sign_key) f->sign_key=sign_key;
  f->use_lock=!!nx_json_get(json, "use_lock")->int_value;
  const nx_json* allowed_cmds_json=nx_json_get(json, "allowed_cmds");
  if (allowed_cmds_json->type!=NX_JSON_NULL) {
    nxweb_image_filter_cmd* allowed_cmds=calloc(allowed_cmds_json->length+1, sizeof(nxweb_image_filter_cmd));
    int i;
    nxweb_image_filter_cmd* cmd=allowed_cmds;
    for (i=0; i<allowed_cmds_json->length; i++) {
      const nx_json* js=nx_json_item(allowed_cmds_json, i);
      const char* c=nx_json_get(js, "cmd")->text_value;
      if (!c || !*c) continue;
      cmd->cmd=*c;
      cmd->width=(int)nx_json_get(js, "width")->int_value;
      cmd->height=(int)nx_json_get(js, "height")->int_value;
      const char* color=nx_json_get(js, "bgcolor")->text_value;
      if (color && *color=='#' && strlen(color)==7) {
        strcpy(cmd->color, color);
      }
      cmd++;
    }
    f->allowed_cmds=allowed_cmds;
  }
  return (nxweb_filter*)f;
}

static nxweb_filter_image image_filter={.base={
        .config=img_config,
        .init=img_init, .finalize=img_finalize,
        .translate_cache_key=img_translate_cache_key,
        .decode_uri=img_decode_uri, .do_filter=img_do_filter},
        .allowed_cmds=default_allowed_cmds, .sign_key=CMD_SIGN_SECRET_KEY};

NXWEB_DEFINE_FILTER(image, image_filter.base);

nxweb_filter* nxweb_image_filter_setup(const char* cache_dir, nxweb_image_filter_cmd* allowed_cmds, const char* sign_key) {
  nxweb_filter_image* f=calloc(1, sizeof(nxweb_filter_image)); // NOTE this will never be freed
  *f=image_filter;
  f->cache_dir=cache_dir;
  if (allowed_cmds) f->allowed_cmds=allowed_cmds;
  if (sign_key) f->sign_key=sign_key;
  return (nxweb_filter*)f;
}

static void image_filter_on_config(const nx_json* json) {
  if (!json) return; // default config
  // module level config
  const char* sign_key=nx_json_get(json, "sign_key")->text_value;
  if (sign_key) image_filter.sign_key=sign_key;
  const nx_json* allowed_cmds_json=nx_json_get(json, "allowed_cmds");
  if (allowed_cmds_json->type!=NX_JSON_NULL) {
    nxweb_image_filter_cmd* allowed_cmds=calloc(allowed_cmds_json->length+1, sizeof(nxweb_image_filter_cmd));
    int i;
    nxweb_image_filter_cmd* cmd=allowed_cmds;
    for (i=0; i<allowed_cmds_json->length; i++) {
      const nx_json* js=nx_json_item(allowed_cmds_json, i);
      const char* c=nx_json_get(js, "cmd")->text_value;
      if (!c || !*c) continue;
      cmd->cmd=*c;
      cmd->width=(int)nx_json_get(js, "width")->int_value;
      cmd->height=(int)nx_json_get(js, "height")->int_value;
      const char* color=nx_json_get(js, "bgcolor")->text_value;
      if (color && *color=='#' && strlen(color)==7) {
        strcpy(cmd->color, color);
      }
      cmd++;
    }
    image_filter.allowed_cmds=allowed_cmds;
  }
}
