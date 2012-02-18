#!/usr/bin/perl -w

use encoding "UTF-8";

$COPYRIGHT=<<EOT;
Copyright (c) 2011-2012 Yaroslav Stavnichiy <yarosla\@gmail.com>

This file is part of NXWEB.

NXWEB is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License
as published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

NXWEB is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with NXWEB. If not, see <http://www.gnu.org/licenses/>.
EOT

$COPYRIGHT =~ s/^/ * /gm;
$COPYRIGHT =~ s/\s+$//gm;
$COPYRIGHT="/*\n".$COPYRIGHT."\n */\n\n";

@FILELIST = qw#
./src/bin/modules/hello.c
./src/bin/modules/benchmark.c
./src/bin/main.c
./src/lib/nxd_ssl_socket.c
./src/lib/modules/sendfile.c
./src/lib/modules/http_proxy.c
./src/lib/daemon.c
./src/lib/nx_pool.c
./src/lib/nxd_buffer.c
./src/lib/nx_event.c
./src/lib/nx_file_reader.c
./src/lib/http_utils.c
./src/lib/nxd_socket.c
./src/lib/nxd_http_proxy.c
./src/lib/nxd_http_server_proto.c
./src/lib/misc.c
./src/lib/nx_workers.c
./src/lib/mime.c
./src/lib/nx_buffer.c
./src/lib/nxd_http_client_proto.c
./src/lib/http_server.c
./src/lib/cache.c
./src/lib/filters/image_filter.c
./src/lib/filters/gzip_filter.c
./src/include/nxweb/nxweb_config.h
./src/include/nxweb/nx_alloc.h
./src/include/nxweb/nx_workers.h
./src/include/nxweb/nx_event.h
./src/include/nxweb/misc.h
./src/include/nxweb/nx_file_reader.h
./src/include/nxweb/nx_pool.h
./src/include/nxweb/nx_queue_tpl.h
./src/include/nxweb/nxweb.h
./src/include/nxweb/nx_buffer.h
./src/include/nxweb/nxd.h
./src/include/nxweb/http_server.h
#;

sub upd_cprt {
  my ($fname)=@_;
  print "Updating $fname...";
  local($/);
  open(F, '+<:encoding(UTF-8)', $fname) or die " can't open the file\n";
  $text=<F>;
  $text =~ s/^(\s*\/\*[*\s]*Copyright.*?\*\/\s*)//si;
  if ($1 ne $COPYRIGHT) {
    $text=$COPYRIGHT.$text;
    seek(F, 0, 0);
    truncate(F, 0);
    print F $text;
    print " done\n";
  }
  else {
    print " skipped\n";
  }
  close(F);
}

if (@ARGV) {
  @FILELIST=@ARGV;
}

for $fname (@FILELIST) {
  upd_cprt($fname);
}
