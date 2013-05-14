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

#include <fcntl.h>

static nxweb_handler* find_handler(const char* name) {
  nxweb_handler* h;
  for (h=nxweb_server_config.handlers_defined; h; h=h->next_defined) {
    if (!strcmp(name, h->name)) return h;
  }
  return 0;
}

static nxweb_filter* find_filter(const char* name) {
  nxweb_filter* f;
  for (f=nxweb_server_config.filters_defined; f; f=f->next_defined) {
    if (!strcmp(name, f->name)) return f;
  }
  return 0;
}

int nxweb_load_config(const char* filename) {
  struct stat st;
  if (stat(filename, &st)==-1) {
    nxweb_log_error("can't find config file %s", filename);
    return -1;
  }
  int fd=open(filename, O_RDONLY);
  if (fd==-1) {
    nxweb_log_error("can't open config file %s", filename);
    return -1;
  }
  char* text=malloc(st.st_size+1); // this is not going to be freed
  if (st.st_size!=read(fd, text, st.st_size)) {
    nxweb_log_error("can't read config file %s", filename);
    close(fd);
    return -1;
  }
  close(fd);
  text[st.st_size]='\0';
  const nx_json* json=nx_json_parse(text, 0);
  if (!json) {
    nxweb_log_error("can't parse config file %s", filename);
    return -1;
  }

  int i;

  const nx_json* listen=nx_json_get(json, "listen");
  if (listen->type!=NX_JSON_NULL) {
    for (i=0; i<listen->length; i++) {
      const nx_json* l=nx_json_item(listen, i);
      const char* itf=nx_json_get(l, "interface")->text_value;
      int backlog=(int)nx_json_get(l, "backlog")->int_value;
      int secure=(int)nx_json_get(l, "secure")->int_value;
      if (!backlog) backlog=1024;
      if (itf) {
        if (!secure) {
          if (nxweb_listen(itf, backlog)) return -1;
        }
#ifdef WITH_SSL
        else {
          if (nxweb_listen_ssl(itf, backlog, 1, nx_json_get(l, "cert")->text_value, nx_json_get(l, "key")->text_value, nx_json_get(l, "dh")->text_value, nx_json_get(l, "priorities")->text_value)) return -1;
        }
#endif // WITH_SSL
      }
    }
  }
  else { // fallback to command line arguments
    if (nxweb_main_args.http_listening_host_and_port) {
      if (nxweb_listen(nxweb_main_args.http_listening_host_and_port, 4096)) return -1;
    }
#ifdef WITH_SSL
    if (nxweb_main_args.https_listening_host_and_port) {
      if (nxweb_listen_ssl(nxweb_main_args.https_listening_host_and_port, 1024, 1,
                           "ssl/server_cert.pem", "ssl/server_key.pem", "ssl/dh.pem",
                           "NORMAL:+VERS-TLS-ALL:+COMP-ALL:-CURVE-ALL:+CURVE-SECP256R1")) return -1;
    }
#endif // WITH_SSL
  }

  const nx_json* drop_privileges=nx_json_get(json, "drop_privileges");
  if (drop_privileges->type!=NX_JSON_NULL) {
    if (nxweb_drop_privileges(nx_json_get(drop_privileges, "group")->text_value, nx_json_get(drop_privileges, "user")->text_value)==-1) return -1;
  }
  else { // fallback to command line arguments
    if (nxweb_drop_privileges(nxweb_main_args.group_name, nxweb_main_args.user_name)==-1) return -1;
  }

  nxweb_error_log_level=NXWEB_LOG_WARNING;

  const nx_json* logging=nx_json_get(json, "logging");
  if (logging->type!=NX_JSON_NULL) {
    // set error log verbosity: INFO=most verbose, WARN, ERROR, NONE
    const char* log_level=nx_json_get(logging, "log_level")->text_value;
    if (log_level) {
      if (!strcmp(log_level, "INFO")) nxweb_error_log_level=NXWEB_LOG_INFO;
      else if (!strcmp(log_level, "WARN")) nxweb_error_log_level=NXWEB_LOG_WARNING;
      else if (!strcmp(log_level, "ERROR")) nxweb_error_log_level=NXWEB_LOG_ERROR;
      else if (!strcmp(log_level, "NONE")) nxweb_error_log_level=NXWEB_LOG_NONE;
    }
    const char* access_log=nx_json_get(logging, "access_log")->text_value;
    if (access_log) {
      nxweb_server_config.access_log_fpath=access_log;
    }
  }

  const nx_json* backends=nx_json_get(json, "backends");
  if (backends->type!=NX_JSON_NULL) {
    for (i=0; i<backends->length; i++) {
      const nx_json* js=nx_json_item(backends, i);
      const char* itf=nx_json_get(js, "connect")->text_value;
      if (itf) {
        nxweb_setup_http_proxy_pool(i, itf);
      }
    }
  }

  const nx_json* routing=nx_json_get(json, "routing");
  if (routing->type!=NX_JSON_NULL) {
    for (i=0; i<routing->length; i++) {
      const nx_json* js=nx_json_item(routing, i);
      const char* targets=nx_json_get(js, "targets")->text_value;
      if (targets) {
        if (!nxweb_main_args.config_target || !strstr(nxweb_main_args.config_target, targets)) continue;
      }
      const char* handler_name=nx_json_get(js, "handler")->text_value;
      if (!handler_name || !*handler_name) {
        nxweb_log_error("no handler specified for routing record #%d", i);
        continue;
      }
      nxweb_handler* base_handler=find_handler(handler_name);
      if (!base_handler) {
        nxweb_log_error("can't find handler '%s' specified for routing record #%d", handler_name, i);
        continue;
      }

      nxweb_handler* new_handler=calloc(1, sizeof(nxweb_handler)); // this will never be freed
      new_handler->name=base_handler->name;
      new_handler->prefix=nx_json_get(js, "prefix")->text_value;

      const char* backend=nx_json_get(js, "backend")->text_value;
      if (backend) {
        if (!backends->length) {
          nxweb_log_error("no backend %s configured for routing record #%d", backend, i);
          continue;
        }
        int j, found=0;
        for (j=0; j<backends->length; j++) {
          const nx_json* bk=nx_json_item(backends, j);
          if (bk->key && !strcmp(bk->key, backend)) {
            new_handler->idx=j;
            found=1;
            break;
          }
        }
        if (!found) {
          nxweb_log_error("backend %s not found for routing record #%d", backend, i);
          continue;
        }
      }
      else {
        new_handler->idx=nx_json_get(js, "idx")->int_value;
      }

      const nx_json* filters=nx_json_get(js, "filters");
      if (filters->type!=NX_JSON_NULL) {
        int j, k=0;
        for (j=0; j<filters->length; j++) {
          const nx_json* js=nx_json_item(filters, j);
          const char* filter_name=nx_json_get(js, "type")->text_value;
          if (!filter_name || !*filter_name) {
            nxweb_log_error("no filter type specified for routing record #%d, filter record #%d", i, j);
            continue;
          }
          nxweb_filter* base_filter=find_filter(filter_name);
          if (!base_filter) {
            nxweb_log_error("can't find filter '%s' specified for routing record #%d, filter record #%d", filter_name, i, j);
            continue;
          }
          new_handler->filters[k++]=base_filter->config? base_filter->config(base_filter, js) : base_filter;
        }
      }

      new_handler->vhost=nx_json_get(js, "vhost")->text_value;
      new_handler->secure_only=!!nx_json_get(js, "secure_only")->int_value;
      new_handler->insecure_only=!!nx_json_get(js, "insecure_only")->int_value;
      new_handler->memcache=!!nx_json_get(js, "memcache")->int_value;
      new_handler->flags=nx_json_get(js, "flags")->int_value;
      new_handler->charset=nx_json_get(js, "charset")->text_value;
      new_handler->dir=nx_json_get(js, "dir")->text_value;
      new_handler->uri=nx_json_get(js, "uri")->text_value;
      new_handler->host=nx_json_get(js, "host")->text_value;
      new_handler->index_file=nx_json_get(js, "index_file")->text_value;
      new_handler->proxy_copy_host=!!nx_json_get(js, "proxy_copy_host")->int_value;
      new_handler->size=nx_json_get(js, "size")->int_value;
      new_handler->priority=nx_json_get(js, "priority")->int_value;
      if (!new_handler->priority) new_handler->priority=(i+1)*1000;
      _nxweb_register_handler(new_handler, base_handler);
    }
  }

  nx_json_free(json);
}
