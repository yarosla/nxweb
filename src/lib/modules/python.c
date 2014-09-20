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

#include "nxweb/nxweb.h"

#ifdef HAVE_SSIZE_T
#undef HAVE_SSIZE_T
#endif
#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#endif
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif

#include <Python.h>

#define MODULE_NAME "nxwebpy"
#define FUNC_NAME "_nxweb_on_request"

static const char* project_root="."; // defaults to workdir
static const char* project_app;
static const char* virtualenv_path="";

static PyThreadState* py_main_thread_state;
static PyObject* py_module;
static PyObject* py_nxweb_on_request_func;

static void on_config(const nx_json* js) {
  if (nxweb_main_args.python_root) project_root=nxweb_main_args.python_root;
  else if (js) {
    const char* v=nx_json_get(js, "project_path")->text_value;
    if (v) project_root=v;
  }
  if (nxweb_main_args.python_wsgi_app) project_app=nxweb_main_args.python_wsgi_app;
  else if (js) {
    const char* v=nx_json_get(js, "wsgi_application")->text_value;
    if (v) project_app=v;
  }
  if (nxweb_main_args.python_virtualenv_path) virtualenv_path=nxweb_main_args.python_virtualenv_path;
  else if (js) {
    const char* v=nx_json_get(js, "virtualenv_path")->text_value;
    if (v) virtualenv_path=v;
  }
  nxweb_log_error("python config: root=%s, app=%s, virtualenv=%s", project_root, project_app, virtualenv_path);
}

static int on_startup() {
  struct stat fi;
  if (!project_app || !*project_app) {
    nxweb_log_error("python wsgi app not specified; skipping python initialization");
    return 0;
  }
  static const char* prog_name="python/nxwebpy.py";
  if (stat(prog_name, &fi)==-1) {

#ifdef NXWEB_LIBDIR
    prog_name=NXWEB_LIBDIR "/nxwebpy.py";
    if (stat(prog_name, &fi)==-1) {
#endif

      nxweb_log_error("%s is missing; skipping python initialization", prog_name);
      return 0;

#ifdef NXWEB_LIBDIR
    }
#endif

  }

  Py_SetProgramName((char*)prog_name);
  // initialize thread support
  PyEval_InitThreads();
  Py_Initialize();
  char *a[]={(char*)prog_name, (char*)project_root, (char*)project_app, (char*)virtualenv_path};
  PySys_SetArgv(4, a);
  PyObject* py_module_name=PyString_FromString(MODULE_NAME);
  assert(py_module_name);
  // save a pointer to the main PyThreadState object
  py_main_thread_state=PyThreadState_Get();
  py_module=PyImport_Import(py_module_name);
  if (!py_module || !PyModule_Check(py_module)) {
    fprintf(stderr, "can't load python module %s; check parse errors:\n", MODULE_NAME);
    PyErr_Print();
    exit(0);
  }
  Py_DECREF(py_module_name);
  py_nxweb_on_request_func=PyObject_GetAttrString(py_module, FUNC_NAME);
  assert(py_nxweb_on_request_func && PyCallable_Check(py_nxweb_on_request_func));
  // release the lock
  PyEval_ReleaseLock();
  return 0;
}

static void on_shutdown() {
  if (!py_module) return; // not initialized
  // shut down the interpreter
  nxweb_log_error("shutting down python");
  PyEval_AcquireLock();
  PyThreadState_Swap(py_main_thread_state);
  Py_XDECREF(py_nxweb_on_request_func);
  Py_XDECREF(py_module);
  Py_Finalize();
  nxweb_log_error("python finalized");
}

NXWEB_MODULE(python, .on_server_startup=on_startup, .on_server_shutdown=on_shutdown, .on_config=on_config);

#define NXWEB_MAX_PYTHON_UPLOAD_SIZE 50000000

static const char python_handler_key; // variable's address only matters
#define PYTHON_HANDLER_KEY ((nxe_data)&python_handler_key)

static void python_request_data_finalize(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp, nxe_data data) {
  nxd_fwbuffer* fwb=data.ptr;
  if (fwb && fwb->fd) {
    // close temp upload file
    close(fwb->fd);
    fwb->fd=0;
  }
}

static nxweb_result python_on_post_data(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (req->content_length>0 && req->content_length<NXWEB_MAX_REQUEST_BODY_SIZE) {
    // fallback to default in-memory buffering
    return NXWEB_NEXT;
  }
  if (!conn->handler->dir) {
    nxweb_log_warning("python handler temp upload file dir not set => skipping file buffering for %s", req->uri);
    return NXWEB_NEXT;
  }
  nxe_ssize_t upload_size_limit=conn->handler->size? conn->handler->size : NXWEB_MAX_PYTHON_UPLOAD_SIZE;
  if (req->content_length > upload_size_limit) {
    nxweb_send_http_error(resp, 413, "Request Entity Too Large");
    resp->keep_alive=0; // close connection
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
  char* fname_template=nxb_alloc_obj(req->nxb, strlen(conn->handler->dir)+sizeof("/py_upload_tmp_XXXXXX")+1);
  strcat(strcpy(fname_template, conn->handler->dir), "/py_upload_tmp_XXXXXX");
  if (nxweb_mkpath(fname_template, 0755)==-1) {
    nxweb_log_error("can't create path to temp upload file %s; check permissions", fname_template);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    resp->keep_alive=0; // close connection
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
  int fd=mkstemp(fname_template);
  if (fd==-1) {
    nxweb_log_error("can't open (mkstemp()) temp upload file for %s", req->uri);
    nxweb_send_http_error(resp, 500, "Internal Server Error");
    resp->keep_alive=0; // close connection
    nxweb_start_sending_response(conn, resp);
    return NXWEB_OK;
  }
  unlink(fname_template); // auto-delete on close()
  nxd_fwbuffer* fwb=nxb_alloc_obj(req->nxb, sizeof(nxd_fwbuffer));
  nxweb_set_request_data(req, PYTHON_HANDLER_KEY, (nxe_data)(void*)fwb, python_request_data_finalize);
  nxd_fwbuffer_init(fwb, fd, upload_size_limit);
  conn->hsp.cls->connect_request_body_out(&conn->hsp, &fwb->data_in);
  conn->hsp.cls->start_receiving_request_body(&conn->hsp);
  return NXWEB_OK;
}

static nxweb_result python_on_post_data_complete(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  // nothing to do here
  // keep temp file open
  return NXWEB_OK;
}


static inline void dict_set(PyObject* dict, const char* key, PyObject* val) {
  PyDict_SetItemString(dict, key, val);
  Py_XDECREF(val);
}

static nxweb_result python_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  nxb_buffer* nxb=req->nxb;
  nxweb_handler* handler=conn->handler;
  const char* request_uri=req->uri;
  char* query_string=strchr(request_uri, '?');
  int ulen=query_string? (query_string-request_uri) : strlen(request_uri);
  if (query_string) query_string++;
  int pfxlen=req->path_info? (req->path_info - req->uri) : 0;
  int plen=ulen-pfxlen;
  const char* path_info=request_uri+pfxlen;
  if (handler->uri && *handler->uri) {
    pfxlen=strlen(handler->uri);
    ulen=pfxlen+plen;
    char* u=nxb_alloc_obj(nxb, ulen+1);
    memcpy(u, handler->uri, pfxlen);
    memcpy(u+pfxlen, path_info, plen);
    u[ulen]='\0';
    request_uri=u;
    path_info=request_uri+pfxlen;
  }
  const char* host_port=req->host? strchr(req->host, ':') : 0;

  int content_fd=0;

  if (req->content_length) {
    nxd_fwbuffer* fwb=nxweb_get_request_data(req, PYTHON_HANDLER_KEY).ptr;

    if (fwb) {
      if (fwb->error || fwb->size > fwb->max_size) {
        nxweb_send_http_error(resp, 413, "Request Entity Too Large"); // most likely cause
        return NXWEB_ERROR;
      }
      else if (req->content_received!=fwb->size) {
        nxweb_log_error("content_received does not match upload stored size for %s", req->uri);
        nxweb_send_http_error(resp, 500, "Internal Server Error");
        return NXWEB_ERROR;
      }
      else {
        content_fd=fwb->fd;
        if (lseek(content_fd, 0, SEEK_SET)==-1) {
          nxweb_log_error("can't lseek() temp upload file for %s", req->uri);
          nxweb_send_http_error(resp, 500, "Internal Server Error");
          return NXWEB_ERROR;
        }
      }
    }
  }


  nxweb_log_debug("invoke python");

  PyGILState_STATE gstate=PyGILState_Ensure();

  PyObject* py_func_args=PyTuple_New(1);
  PyObject* py_environ=PyDict_New();
  assert(PyDict_Check(py_environ));

  dict_set(py_environ, "SERVER_NAME", PyString_FromStringAndSize(req->host, host_port? (host_port-req->host) : strlen(req->host)));
  dict_set(py_environ, "SERVER_PORT", PyString_FromString(host_port? host_port+1 : ""));
  dict_set(py_environ, "SERVER_PROTOCOL", PyString_FromString(req->http11? "HTTP/1.1" : "HTTP/1.0"));
  dict_set(py_environ, "SERVER_SOFTWARE", PyString_FromString(PACKAGE_STRING));
  dict_set(py_environ, "GATEWAY_INTERFACE", PyString_FromString("CGI/1.1"));
  dict_set(py_environ, "REQUEST_METHOD", PyString_FromString(req->method));
  dict_set(py_environ, "REQUEST_URI", PyString_FromStringAndSize(request_uri, ulen));
  dict_set(py_environ, "SCRIPT_NAME", PyString_FromStringAndSize(request_uri, pfxlen));
  dict_set(py_environ, "PATH_INFO", PyString_FromStringAndSize(path_info, plen));
  dict_set(py_environ, "QUERY_STRING", PyString_FromString(query_string? query_string : ""));
  dict_set(py_environ, "REMOTE_ADDR", PyString_FromString(conn->remote_addr));
  dict_set(py_environ, "CONTENT_TYPE", PyString_FromString(req->content_type? req->content_type : ""));
  dict_set(py_environ, "CONTENT_LENGTH", PyInt_FromLong(req->content_received));
  if (req->cookie) dict_set(py_environ, "HTTP_COOKIE", PyString_FromString(req->cookie));
  if (req->host) dict_set(py_environ, "HTTP_HOST", PyString_FromString(req->host));
  if (req->user_agent) dict_set(py_environ, "HTTP_USER_AGENT", PyString_FromString(req->user_agent));
  if (req->if_modified_since) {
    struct tm tm;
    gmtime_r(&req->if_modified_since, &tm);
    char ims[32];
    nxweb_format_http_time(ims, &tm);
    dict_set(py_environ, "HTTP_IF_MODIFIED_SINCE", PyString_FromString(ims));
  }

  if (req->headers) {
    // write added headers
    // encode http headers into CGI variables; see 4.1.18 in https://tools.ietf.org/html/rfc3875
    char hname[256];
    memcpy(hname, "HTTP_", 5);
    char* h=hname+5;
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->headers); itr; itr=nx_simple_map_itr_next(itr)) {
      nx_strtoupper(h, itr->name);
      char* p;
      for (p=h; *p; p++) {
        if (*p=='-') *p='_';
      }
      dict_set(py_environ, hname, PyString_FromString(itr->value));
    }
  }

  dict_set(py_environ, "wsgi.url_scheme", PyString_FromString(conn->secure? "https" : "http"));

  if (req->content_length) {
    if (content_fd) {
      dict_set(py_environ, "nxweb.req.content_fd", PyInt_FromLong(content_fd));
    }
    else {
      dict_set(py_environ, "nxweb.req.content", PyByteArray_FromStringAndSize(req->content? req->content : "", req->content_received));
    }
  }
  if (req->if_modified_since) dict_set(py_environ, "nxweb.req.if_modified_since", PyLong_FromLong(req->if_modified_since));
  dict_set(py_environ, "nxweb.req.uid", PyLong_FromLongLong(req->uid));
  if (req->parent_req) {
    nxweb_http_request* preq=req->parent_req;
    while (preq->parent_req) preq=preq->parent_req; // find root request
    if (preq->uid) {
      dict_set(py_environ, "nxweb.req.root_uid", PyLong_FromLongLong(preq->uid));
    }
  }

  // call python
  PyTuple_SetItem(py_func_args, 0, py_environ);
  PyObject* py_result=PyObject_CallObject(py_nxweb_on_request_func, py_func_args);
  Py_DECREF(py_func_args);
  if (py_result && PyTuple_Check(py_result) && PyTuple_Size(py_result)==3) {
    PyObject* py_status=PyTuple_GET_ITEM(py_result, 0);
    PyObject* py_headers=PyTuple_GET_ITEM(py_result, 1);
    PyObject* py_body=PyTuple_GET_ITEM(py_result, 2);

    if (py_status && PyString_Check(py_status)) {
      const char* status_string=PyString_AS_STRING(py_status);
      int status_code=0;
      const char* p=status_string;
      while (*p && *p>='0' && *p<='9') {
        status_code=status_code*10+(*p-'0');
        p++;
      }
      while (*p && *p==' ') p++;
      if (status_code>=200 && status_code<600 && *p) {
        resp->status_code=status_code;
        resp->status=nxb_copy_str(nxb, p);
      }
    }

    if (py_headers && PyList_Check(py_headers)) {
      const int size=PyList_Size(py_headers);
      int i;
      for (i=0; i<size; i++) {
        PyObject* py_header_tuple=PyList_GET_ITEM(py_headers, i);
        if (py_header_tuple && PyTuple_Check(py_header_tuple) && PyTuple_Size(py_header_tuple)==2) {
          PyObject* py_name=PyTuple_GET_ITEM(py_header_tuple, 0);
          PyObject* py_value=PyTuple_GET_ITEM(py_header_tuple, 1);
          if (py_name && PyString_Check(py_name) && py_value && PyString_Check(py_value)) {
            nxweb_add_response_header_safe(resp, PyString_AS_STRING(py_name), PyString_AS_STRING(py_value));
          }
        }
      }
    }

    if ((!resp->status_code || resp->status_code==200) && !resp->content_type) resp->content_type="text/html";

    char* rcontent=0;
    nxe_ssize_t rsize=0;
    if (PyByteArray_Check(py_body)) {
      rcontent=PyByteArray_AS_STRING(py_body);
      rsize=PyByteArray_Size(py_body);
    }
    else if (PyString_Check(py_body)) {
      rcontent=PyString_AS_STRING(py_body);
      rsize=PyString_Size(py_body);
    }
    if (rcontent && rsize>0) nxweb_response_append_data(resp, rcontent, rsize);
  }
  else if (py_result && PyString_Check(py_result)) {
    resp->status_code=500;
    resp->status="Internal Server Error";
    resp->content_type="text/html";
    nxweb_log_error("python call failed: %s", PyString_AS_STRING(py_result));
    nxweb_response_printf(resp, "python call failed: %H", PyString_AS_STRING(py_result));
  }
  else {
    PyErr_Print();
    nxweb_log_error("python call failed");
    nxweb_response_printf(resp, "python call failed");
  }
  Py_XDECREF(py_result);

  // Release the thread. No Python API allowed beyond this point.
  PyGILState_Release(gstate);

  nxweb_log_debug("invoke python complete");

  return NXWEB_OK;
}

static nxweb_result python_on_select(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!py_module) return NXWEB_NEXT; // skip if python not initialized
  return NXWEB_OK;
}

static nxweb_result python_generate_cache_key(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  if (!py_module) return NXWEB_NEXT; // skip if python not initialized
  if (!req->get_method || req->content_length) return NXWEB_OK; // do not cache POST requests, etc.
  nxb_start_stream(req->nxb);
  _nxb_append_encode_file_path(req->nxb, req->host);
  if (conn->secure) nxb_append_str(req->nxb, "_s");
  _nxb_append_encode_file_path(req->nxb, req->uri);
  nxb_append_char(req->nxb, '\0');
  resp->cache_key=nxb_finish_stream(req->nxb, 0);
  return NXWEB_OK;
}

NXWEB_DEFINE_HANDLER(python, .on_select=python_on_select, .on_request=python_on_request,
        .on_generate_cache_key=python_generate_cache_key,
        .on_post_data=python_on_post_data,
        //.on_post_data_complete=python_on_post_data_complete,
        .flags=NXWEB_HANDLE_ANY|NXWEB_INWORKER);
