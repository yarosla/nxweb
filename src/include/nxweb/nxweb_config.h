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

#ifndef NXWEB_CONFIG_H
#define	NXWEB_CONFIG_H

#ifdef	__cplusplus
extern "C" {
#endif

//#ifdef HAVE_CONFIG_H
#include "config.h"
//#endif

#define NXWEB_MAX_LISTEN_SOCKETS 4
#define NXWEB_MAX_PROXY_POOLS 4
#define NXWEB_MAX_REQUEST_HEADERS_SIZE 4096
#define NXWEB_MAX_REQUEST_BODY_SIZE 512000
#define NXWEB_RBUF_SIZE 16384
#define NXWEB_PROXY_RETRY_COUNT 4
#define NXWEB_CONN_NXB_SIZE (NXWEB_MAX_REQUEST_HEADERS_SIZE+1024)
#define NXWEB_MAX_FILTERS 16
#define NXWEB_DEFAULT_CACHED_TIME 30000000
#define NXWEB_MAX_CACHED_ITEMS 500
#define NXWEB_MAX_CACHED_ITEM_SIZE 32768

#ifdef NX_DEBUG
#define NXWEB_MAX_NET_THREADS 1
#else
#define NXWEB_MAX_NET_THREADS 16
#endif

// timeouts can be overriden by calling nxweb_set_timeout() before starting the server
// timeouts are in micro-seconds:
#define NXWEB_DEFAULT_KEEP_ALIVE_TIMEOUT 60000000
#define NXWEB_DEFAULT_WRITE_TIMEOUT 30000000
#define NXWEB_DEFAULT_READ_TIMEOUT 30000000
#define NXWEB_DEFAULT_BACKEND_TIMEOUT 2000000
#define NXWEB_DEFAULT_100CONTINUE_TIMEOUT 1500000


#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_CONFIG_H */

