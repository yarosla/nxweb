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

#include "nxweb.h"

#include <string.h>
#include <assert.h>

#include "../deps/ulib/alignhash_tpl.h"
#include "../deps/ulib/hash.h"

#define mime_cache_hash_fn(key) hash_djb2((const unsigned char*)(key))
#define mime_cache_eq_fn(a, b) (!strcmp((a), (b)))

DECLARE_ALIGNHASH(mime_cache, const char*, const nxweb_mime_type*, 1, mime_cache_hash_fn, mime_cache_eq_fn)

static alignhash_t(mime_cache) *_mime_cache_by_ext;
static alignhash_t(mime_cache) *_mime_cache_by_type;

static const nxweb_mime_type const mime_types[] = {
  {"htm", "text/html", 1, .gzippable=1}, // default mime type
  {"html", "text/html", 1, .gzippable=1},
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
  {"css", "text/css", 1, 1},
  {"xhtml", "application/xhtml+xml", 1, .gzippable=1},
  {"xht", "application/xhtml+xml", 1, .gzippable=1},
  {"xml", "application/xml", 1, .gzippable=1},
  {"xsl", "application/xml", 1, .gzippable=1},
  {"xslt", "application/xml", 1, .gzippable=1},
  {"atom", "application/atom+xml", 1, .gzippable=1},
  {"dtd", "application/xml-dtd", 1, .gzippable=1},
  {"doc", "application/msword", 0, .gzippable=1},
  {"pdf", "application/pdf", 0},
  {"eps", "application/postscript", 0},
  {"ps", "application/postscript", 0},
  {"ai", "application/postscript", 0},
  {"rdf", "application/rdf+xml", 1},
  {"smil", "application/smil", 0},
  {"xls", "application/vnd.ms-excel", 0, .gzippable=1},
  {"ppt", "application/vnd.ms-powerpoint", 0, .gzippable=1},
  {"pps", "application/vnd.ms-powerpoint", 0, .gzippable=1},
  {"swf", "application/x-shockwave-flash", 0},
  {"sit", "application/x-stuffit", 0},
  {"tar", "application/x-tar", 0},
  {"zip", "application/zip", 0},
  {"rar", "application/rar", 0},
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
  {"rtf", "text/rtf", 0},
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
  {0}
};

void nxweb_add_mime_type(const nxweb_mime_type* type) {
  assert(!_nxweb_net_thread_data); // mime cache is not thread safe
                                   // this function must be called before nxweb threads launched,
                                   // i.e. before nxweb_run() or from on_server_startup
  ah_iter_t ci;
  int ret=0;
  ci=alignhash_set(mime_cache, _mime_cache_by_ext, type->ext, &ret);
  if (ci!=alignhash_end(_mime_cache_by_ext)) {
    alignhash_value(_mime_cache_by_ext, ci)=type;
  }
  ci=alignhash_set(mime_cache, _mime_cache_by_type, type->mime, &ret);
  if (ci!=alignhash_end(_mime_cache_by_type)) {
    alignhash_value(_mime_cache_by_type, ci)=type;
  }
}

static void init_mime_cache() __attribute__((constructor));
static void init_mime_cache() {
  _mime_cache_by_ext=alignhash_init(mime_cache);
  _mime_cache_by_type=alignhash_init(mime_cache);
  const nxweb_mime_type* type=mime_types;
  while (type->ext) {
    nxweb_add_mime_type(type);
    type++;
  }
}

static void finalize_mime_cache() __attribute__((destructor));
static void finalize_mime_cache() {
  alignhash_destroy(mime_cache, _mime_cache_by_ext);
  alignhash_destroy(mime_cache, _mime_cache_by_type);
}

const nxweb_mime_type* nxweb_get_mime_type_by_ext(const char* fpath_or_ext) {
  const char* ext=strrchr(fpath_or_ext, '.');
  ext=ext? ext+1 : fpath_or_ext;
  int ext_len=strlen(ext);
  char _ext[32];
  if (ext_len>sizeof(_ext)-1) return &mime_types[0];
  nx_tolower_str(_ext, ext);
  ah_iter_t ci;
  if ((ci=alignhash_get(mime_cache, _mime_cache_by_ext, _ext))!=alignhash_end(_mime_cache_by_ext)) {
    return alignhash_value(_mime_cache_by_ext, ci);
  }
  return &mime_types[0];
}

const nxweb_mime_type* nxweb_get_mime_type(const char* type_name) {
  if (!type_name) return mime_types; // first one is default
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
