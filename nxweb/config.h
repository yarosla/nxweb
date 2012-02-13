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

#ifndef CONFIG_H
#define	CONFIG_H

#ifdef	__cplusplus
extern "C" {
#endif

#define NXWEB_MAX_LISTEN_SOCKETS 4
#define NXWEB_LISTEN_HOST_AND_PORT ":8055"
#define NXWEB_LISTEN_HOST_AND_PORT_SSL ":8056"

#define NXWEB_SSL_PRIORITIES "NORMAL:+VERS-TLS-ALL:+COMP-ALL:-CURVE-ALL:+CURVE-SECP256R1"
#define NXWEB_NUM_PROXY_POOLS 4
#define NXWEB_MAX_REQUEST_HEADERS_SIZE 4096
#define NXWEB_MAX_REQUEST_BODY_SIZE 512000
#define NXWEB_MAX_RESPONSE_HEADERS 32
#define NXWEB_RBUF_SIZE 16384
#define NXWEB_PROXY_RETRY_COUNT 4
#define NXWEB_CONN_NXB_SIZE (NXWEB_MAX_REQUEST_HEADERS_SIZE+1024)
#define NXWEB_MAX_FILTERS 16
#define NXWEB_DEFAULT_CACHED_TIME 30000000
#define NXWEB_MAX_CACHED_ITEMS 500
#define NXWEB_MAX_CACHED_ITEM_SIZE 32768
#define NXWEB_DEFAULT_CHARSET "utf-8"

// All paths are relative to working directory:
#define SSL_CERT_FILE "ssl/server_cert.pem"
#define SSL_KEY_FILE "ssl/server_key.pem"
#define SSL_DH_PARAMS_FILE "ssl/dh.pem"

#ifdef NX_DEBUG
#define N_NET_THREADS 1
#else
#define N_NET_THREADS 4
#endif

// timeouts are in micro-seconds:
#define NXWEB_KEEP_ALIVE_TIMEOUT 60000000
#define NXWEB_WRITE_TIMEOUT 30000000
#define NXWEB_READ_TIMEOUT 30000000
#define NXWEB_BACKEND_TIMEOUT 2000000
#define NXWEB_100CONTINUE_TIMEOUT 1500000


#ifdef	__cplusplus
}
#endif

#endif	/* CONFIG_H */

