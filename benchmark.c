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

#include "nxweb/nxweb.h"

static nxweb_result benchmark(nxweb_uri_handler_phase phase, nxweb_request *req) {
  nxweb_response_append(req, "<p>Hello, world!</p>");
  return NXWEB_OK;
}

static nxweb_result benchmark_empty(nxweb_uri_handler_phase phase, nxweb_request *req) {
  return NXWEB_OK;
}

static const nxweb_uri_handler uri_handlers[] = {
  {"/benchmark-inprocess", benchmark, NXWEB_INPROCESS|NXWEB_HANDLE_GET},
  {"/benchmark-inworker", benchmark, NXWEB_INWORKER|NXWEB_HANDLE_GET},
  {"/benchmark-empty", benchmark_empty, NXWEB_INPROCESS|NXWEB_HANDLE_GET},
  {0, 0, 0}
};

/// Module definition
// List of active modules is maintained in modules.c file.

const nxweb_module benchmark_module = {
  .uri_handlers=uri_handlers
};
