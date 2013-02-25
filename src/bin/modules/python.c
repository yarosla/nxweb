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

static PyThreadState* mainThreadState;
static PyObject* pModule;
static PyObject* pFunc;

static int on_startup() {
  //Py_SetProgramName(argv[0]);
  // initialize thread support
  PyEval_InitThreads();
  Py_Initialize();
  char *a[]={};
  PySys_SetArgv(0, a);
  PyObject* pName=PyString_FromString("nxwebpy");
  // save a pointer to the main PyThreadState object
  mainThreadState=PyThreadState_Get();
  pModule=PyImport_Import(pName);
  Py_DECREF(pName);
  pFunc=PyObject_GetAttrString(pModule, "on_request");
  assert(pFunc && PyCallable_Check(pFunc));
  // release the lock
  PyEval_ReleaseLock();
  return 0;
}

static void on_shutdown() {
  // shut down the interpreter
  PyEval_AcquireLock();
  PyThreadState_Swap(mainThreadState);
  Py_XDECREF(pFunc);
  Py_DECREF(pModule);
  Py_Finalize();
  nxweb_log_error("python finalized");
}

NXWEB_MODULE(python, .on_server_startup=on_startup, .on_server_shutdown=on_shutdown);


static nxweb_result python_on_request(nxweb_http_server_connection* conn, nxweb_http_request* req, nxweb_http_response* resp) {
  resp->content_type="text/html";

  //// setup python thread
  // get the global lock
  PyEval_AcquireLock();
  // create a thread state object for this thread
  PyThreadState* myThreadState=PyThreadState_New(mainThreadState->interp);
  // free the lock
//  PyEval_ReleaseLock();


  //// execute on python thread
  // grab the global interpreter lock
//  PyEval_AcquireLock();
  // swap in my thread state
  PyThreadState_Swap(myThreadState);
  // execute some python code

  PyObject* pArgs=PyTuple_New(2);
  PyObject* py_uri=PyString_FromString(req->uri);
  PyObject* py_ims=PyInt_FromLong(req->if_modified_since);
  PyObject* py_result=0;
  if (py_uri && py_ims) {
    /* py_* reference stolen here */
    PyTuple_SetItem(pArgs, 0, py_uri);
    PyTuple_SetItem(pArgs, 1, py_ims);
    py_result=PyObject_CallObject(pFunc, pArgs);
  }
  else {
    nxweb_log_error("invalid py_uri or py_ims");
    nxweb_response_printf(resp, "invalid py_uri or py_ims");
  }
  Py_DECREF(pArgs);
  if (py_result) {
    nxweb_response_append_str(resp, PyString_AsString(py_result));
    Py_DECREF(py_result);
  }
  else {
    PyErr_Print();
    nxweb_log_error("python call failed");
    nxweb_response_printf(resp, "python call failed");
  }

  // clear the thread state
  PyThreadState_Swap(0);
  // release our hold on the global interpreter
//  PyEval_ReleaseLock();

  //// finalize python thread
  // grab the lock
//  PyEval_AcquireLock();
  // swap my thread state out of the interpreter
//  PyThreadState_Swap(0);
  // clear out any cruft from thread state object
  PyThreadState_Clear(myThreadState);
  // delete my thread state object
  PyThreadState_Delete(myThreadState);
  // release the lock
  PyEval_ReleaseLock();


  return NXWEB_OK;
}

nxweb_handler python_handler={.on_request=python_on_request, .flags=NXWEB_HANDLE_ANY|NXWEB_INWORKER};

NXWEB_SET_HANDLER(python, "/py", &python_handler, .priority=1000);