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

static const nxweb_mime_type mime_types[] = {
  {"htm", "text/html", 1}, // default mime type
  {"html", "text/html", 1},
  {"txt", "text/plain", 1},
  {"c", "text/plain", 1},
  {"h", "text/plain", 1},
  {"java", "text/plain", 1},
  {"bmp", "image/bmp", 0},
  {"gif", "image/gif", 0},
  {"jpg", "image/jpeg", 0},
  {"jpeg", "image/jpeg", 0},
  {"jpe", "image/jpeg", 0},
  {"png", "image/png", 0},
  {"svg", "image/svg+xml", 0},
  {"tif", "image/tiff", 0},
  {"tiff", "image/tiff", 0},
  {"wbmp", "image/vnd.wap.wbmp", 0},
  {"ico", "image/x-icon", 0},
  {"js", "application/x-javascript", 1},
  {"css", "text/css", 1},
  {"xhtml", "application/xhtml+xml", 1},
  {"xht", "application/xhtml+xml", 1},
  {"xml", "application/xml", 1},
  {"xsl", "application/xml", 1},
  {"xslt", "application/xml", 1},
  {"atom", "application/atom+xml", 1},
  {"dtd", "application/xml-dtd", 1},
  {"doc", "application/msword", 0},
  {"pdf", "application/pdf", 0},
  {"eps", "application/postscript", 0},
  {"ps", "application/postscript", 0},
  {"ai", "application/postscript", 0},
  {"rdf", "application/rdf+xml", 1},
  {"smil", "application/smil", 0},
  {"xls", "application/vnd.ms-excel", 0},
  {"ppt", "application/vnd.ms-powerpoint", 0},
  {"pps", "application/vnd.ms-powerpoint", 0},
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
  {0, "text/html", 0}
  // {0, "application/octet-stream", 0}
};

const nxweb_mime_type* nxweb_get_mime_type_by_ext(const char* fpath_or_ext) {
  const char* ext=strrchr(fpath_or_ext, '.');
  ext=ext? ext+1 : fpath_or_ext;
  const nxweb_mime_type* type=mime_types;
  while (type->ext) {
    if (!strcasecmp(type->ext, ext)) break;
    type++;
  }
  return type;
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
    nxweb_trunc_space(buf);
    type_name=buf;
  }
  const nxweb_mime_type* type=mime_types;
  while (type->ext) {
    if (!strcmp(type->mime, type_name)) break;
    type++;
  }
  return type->ext? type : 0;
}
