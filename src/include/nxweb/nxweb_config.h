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
#define NXWEB_DEFAULT_ACCEPT_RETRY_TIMEOUT 500000


#ifdef	__cplusplus
}
#endif

#endif	/* NXWEB_CONFIG_H */

