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

#include <string.h>
#include <assert.h>

#include "deps/ulib/alignhash_tpl.h"
#include "deps/ulib/hash.h"

#define mime_cache_hash_fn(key) hash_djb2((const unsigned char*)(key))
#define mime_cache_eq_fn(a, b) (!strcmp((a), (b)))

DECLARE_ALIGNHASH(mime_cache, const char*, const nxweb_mime_type*, 1, mime_cache_hash_fn, mime_cache_eq_fn)

static alignhash_t(mime_cache) *_mime_cache_by_ext;
static alignhash_t(mime_cache) *_mime_cache_by_type;

static const nxweb_mime_type const mime_types[] = {
  // BEWARE that the first entry here is the default type (unless overriden with nxweb_set_default_mime_type),
  // which is gonna be used for all static files with unrecognized file extensions
  // enabling gzip or ssi filter on it might significantly affect your server's performance
  {"htm", "text/html", 1, .gzippable=1, .ssi_on=0}, // default mime type
  {"html", "text/html", 1, .gzippable=1},
  {"shtml", "text/html", 1, .gzippable=1, .ssi_on=1},
  {"shtm", "text/html", 1, .gzippable=1, .ssi_on=1},
  {"thtml", "text/html", 1, .gzippable=1, .templates_on=1},
  {"thtm", "text/html", 1, .gzippable=1, .templates_on=1},
  {"txt", "text/plain", 1, .gzippable=1},
  {"c", "text/plain", 1, .gzippable=1},
  {"h", "text/plain", 1, .gzippable=1},
  {"java", "text/plain", 1, .gzippable=1},
  {"bmp", "image/bmp", 0},
  {"gif", "image/gif", 0, .image=1},
  {"jpg", "image/jpeg", 0, .image=1},
  {"jpeg", "image/jpeg", 0, .image=1},
  {"jpe", "image/jpeg", 0, .image=1},
  {"png", "image/png", 0, .image=1},
  {"svg", "image/svg+xml", 0, .gzippable=1},
  {"tif", "image/tiff", 0},
  {"tiff", "image/tiff", 0},
  {"wbmp", "image/vnd.wap.wbmp", 0},
  {"ico", "image/x-icon", 0, .gzippable=1},
  {"js", "application/x-javascript", 1, .gzippable=1},
  {"json", "application/json", 1, .gzippable=1},
  {"css", "text/css", 1, 1},
  {"xhtml", "application/xhtml+xml", 1, .gzippable=1},
  {"xht", "application/xhtml+xml", 1, .gzippable=1},
  {"xml", "application/xml", 1, .gzippable=1},
  {"xml", "text/xml", 1, .gzippable=1},
  {"xsl", "application/xml", 1, .gzippable=1},
  {"xslt", "application/xml", 1, .gzippable=1},
  {"atom", "application/atom+xml", 1, .gzippable=1},
  {"dtd", "application/xml-dtd", 1, .gzippable=1},
  {"doc", "application/msword", 0, .gzippable=1},
  {"pdf", "application/pdf", 0},
  {"ps", "application/postscript", 0},
  {"eps", "application/postscript", 0},
  {"ai", "application/postscript", 0},
  {"rdf", "application/rdf+xml", 1},
  {"smil", "application/smil", 0},
  {"xls", "application/vnd.ms-excel", 0, .gzippable=1},
  {"ppt", "application/vnd.ms-powerpoint", 0, .gzippable=1},
  {"pps", "application/vnd.ms-powerpoint", 0, .gzippable=1},
  {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document", 0},
  {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", 0},
  {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation", 0},
  {"ppsx", "application/vnd.openxmlformats-officedocument.presentationml.slideshow", 0},
  {"swf", "application/x-shockwave-flash", 0},
  {"sit", "application/x-stuffit", 0},
  {"tar", "application/x-tar", 0},
  {"zip", "application/zip", 0},
  {"rar", "application/rar", 0},
  {"gz", "application/x-gunzip", 0},
  {"tgz", "application/x-tar-gz", 0},
  {"bz2", "application/x-bzip2", 0},
  {"mid", "audio/midi", 0},
  {"midi", "audio/midi", 0},
  {"kar", "audio/midi", 0},
  {"mp3", "audio/mpeg", 0},
  {"mpga", "audio/mpeg", 0},
  {"mp2", "audio/mpeg", 0},
  {"wav", "audio/x-wav", 0},
  {"rtf", "text/rtf", 0, .gzippable=1},
  {"wml", "text/vnd.wap.wml", 1},
  {"avi", "video/x-msvideo", 0},
  {"jad", "text/vnd.sun.j2me.app-descriptor", 0},
  {"jar", "application/java-archive", 0},
  {"mmf", "application/vnd.smaf", 0},
  {"sis", "application/vnd.symbian.install", 0},
  {"flv", "video/x-flv", 0},
  {"mp4", "video/mp4", 0},
  {"3gp", "video/3gpp", 0},
  {"wma", "audio/x-ms-wma", 0},
  {"wax", "audio/x-ms-wax", 0},
  {"wmv", "video/x-ms-wmv", 0},
  {"wvx", "video/x-ms-wvx", 0},
  {"mpg", "video/mpeg", 0},
  {"mpeg", "video/mpeg", 0},
  {"mpe", "video/mpeg", 0},
  {"mov", "video/quicktime", 0},
  {"qt", "video/quicktime", 0},
  {"asf", "video/x-ms-asf", 0},
  {"asx", "video/x-ms-asf", 0},
  {"woff", "application/x-font-woff", 0},
  {"eot", "application/vnd.ms-fontobject", 0},
  {"ttf", "application/x-font-ttf", 0},
  {"otf", "application/x-font-otf", 0},
  {"dat", "application/octet-stream", 0},
};

static const nxweb_mime_type* default_mime_type=&mime_types[0];

void nxweb_add_mime_type(const nxweb_mime_type* type) {
  assert(!_nxweb_net_thread_data); // mime cache is not thread safe
                                   // this function must be called before nxweb threads launched,
                                   // i.e. before nxweb_run() or from on_server_startup
  ah_iter_t ci;
  int ret=0;
  ci=alignhash_set(mime_cache, _mime_cache_by_ext, type->ext, &ret);
  if (/*ret!=AH_INS_ERR &&*/ ci!=alignhash_end(_mime_cache_by_ext)) {
    alignhash_value(_mime_cache_by_ext, ci)=type;
  }
  ci=alignhash_set(mime_cache, _mime_cache_by_type, type->mime, &ret);
  if (/*ret!=AH_INS_ERR &&*/ ci!=alignhash_end(_mime_cache_by_type)) {
    alignhash_value(_mime_cache_by_type, ci)=type;
  }
}

static void init_mime_cache() __attribute__((constructor));
static void init_mime_cache() {
  _mime_cache_by_ext=alignhash_init(mime_cache);
  _mime_cache_by_type=alignhash_init(mime_cache);
  const nxweb_mime_type* type=mime_types;
  int i;
  for (i=sizeof(mime_types)/sizeof(nxweb_mime_type)-1; i>=0; i--) {
    // go from array end so first entries have higher priority (override last ones)
    nxweb_add_mime_type(&mime_types[i]);
  }
}

static void finalize_mime_cache() __attribute__((destructor));
static void finalize_mime_cache() {
  alignhash_destroy(mime_cache, _mime_cache_by_ext);
  alignhash_destroy(mime_cache, _mime_cache_by_type);
}

const nxweb_mime_type* nxweb_get_default_mime_type() {
  return default_mime_type;
}

void nxweb_set_default_mime_type(const nxweb_mime_type* mtype) {
  assert(mtype && mtype->mime && mtype->ext);
  default_mime_type=mtype;
}

const nxweb_mime_type* nxweb_get_mime_type_by_ext(const char* fpath_or_ext) {
  const char* ext=strrchr(fpath_or_ext, '.');
  ext=ext? ext+1 : fpath_or_ext;
  int ext_len=strlen(ext);
  char _ext[32];
  if (ext_len>sizeof(_ext)-1) return default_mime_type;
  nx_strtolower(_ext, ext);
  ah_iter_t ci;
  if ((ci=alignhash_get(mime_cache, _mime_cache_by_ext, _ext))!=alignhash_end(_mime_cache_by_ext)) {
    return alignhash_value(_mime_cache_by_ext, ci);
  }
  return default_mime_type;
}

const nxweb_mime_type* nxweb_get_mime_type(const char* type_name) {
  if (!type_name) return default_mime_type;
  const char* e=strchr(type_name, ';');
  char buf[512];
  if (e) {
    int len=e-type_name;
    if (len>sizeof(buf)-1) return 0;
    strncpy(buf, type_name, len);
    buf[len]='\0';
    type_name=nxweb_trunc_space(buf);
  }
  ah_iter_t ci;
  if ((ci=alignhash_get(mime_cache, _mime_cache_by_type, type_name))!=alignhash_end(_mime_cache_by_type)) {
    return alignhash_value(_mime_cache_by_type, ci);
  }
  return 0;
}
