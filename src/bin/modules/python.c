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

static const char* PROG_NAME="python/nxwebpy.py";
static const char* MODULE_NAME="nxwebpy";
static const char* FUNC_NAME="_nxweb_on_request";

static PyThreadState* py_main_thread_state;
static PyObject* py_module;
static PyObject* py_nxweb_on_request_func;

static int on_startup() {
  Py_SetProgramName((char*)PROG_NAME);
  // initialize thread support
  PyEval_InitThreads();
  Py_Initialize();
  char *a[]={(char*)PROG_NAME};
  PySys_SetArgv(1, a);
  PyObject* py_module_name=PyString_FromString(MODULE_NAME);
  assert(py_module_name);
  // save a pointer to the main PyThreadState object
  py_main_thread_state=PyThreadState_Get();
  py_module=PyImport_Import(py_module_name);
  assert(py_module && PyModule_Check(py_module));
  Py_DECREF(py_module_name);
  py_nxweb_on_request_func=PyObject_GetAttrString(py_module, FUNC_NAME);
  assert(py_nxweb_on_request_func && PyCallable_Check(py_nxweb_on_request_func));
  // release the lock
  PyEval_ReleaseLock();
  return 0;
}

static void on_shutdown() {
  // shut down the interpreter
  PyEval_AcquireLock();
  PyThreadState_Swap(py_main_thread_state);
  Py_XDECREF(py_nxweb_on_request_func);
  Py_XDECREF(py_module);
  Py_Finalize();
  nxweb_log_error("python finalized");
}

NXWEB_MODULE(python, .on_server_startup=on_startup, .on_server_shutdown=on_shutdown);


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
  dict_set(py_environ, "CONTENT_LENGTH", PyInt_FromLong(req->content_length>0? req->content_length : req->content_received));
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
    char hname[256];
    memcpy(hname, "HTTP_", 5);
    char* h=hname+5;
    nx_simple_map_entry* itr;
    for (itr=nx_simple_map_itr_begin(req->headers); itr; itr=nx_simple_map_itr_next(itr)) {
      nx_strtoupper(h, itr->name);
      dict_set(py_environ, hname, PyString_FromString(itr->value));
    }
  }

  dict_set(py_environ, "wsgi.url_scheme", PyString_FromString(conn->secure? "https" : "http"));

  if (req->content_length) dict_set(py_environ, "nxweb.req.content", PyByteArray_FromStringAndSize(req->content? req->content : "", req->content_received));
  if (req->if_modified_since) dict_set(py_environ, "nxweb.req.if_modified_since", PyLong_FromLong(req->if_modified_since));
  dict_set(py_environ, "nxweb.req.uid", PyLong_FromLong(req->uid));
  if (req->parent_req) {
    nxweb_http_request* preq=req->parent_req;
    while (preq->parent_req) preq=preq->parent_req; // find root request
    if (preq->uid) {
      dict_set(py_environ, "nxweb.req.root_uid", PyLong_FromLong(preq->uid));
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
        resp->status=nxb_copy_obj(nxb, p, strlen(p)+1);
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

  return NXWEB_OK;
}

nxweb_handler python_handler={.on_request=python_on_request, .flags=NXWEB_HANDLE_ANY|NXWEB_INWORKER};

NXWEB_SET_HANDLER(python, "/py", &python_handler, .priority=1000, /*.uri="/pyapp"*/);