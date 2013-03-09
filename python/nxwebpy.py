import sys
import io
import os

import hello
# WSGI_APP=hello.ping_app
# WSGI_APP=hello.hello_world_app
WSGI_APP=hello.file_upload_app

# Django sample setup:
# import os
# PROJECT_PATH=os.path.abspath(os.path.dirname(__file__))
# sys.path.append(os.path.join(PROJECT_PATH, 'mysite'))
# import mysite.wsgi
# WSGI_APP=mysite.wsgi.application

def _nxweb_on_request(environ):
  try:
    environ['wsgi.version']=(1, 0)
    if 'nxweb.req.content' in environ:
      environ['wsgi.input']=io.BytesIO(environ['nxweb.req.content'])
    elif 'nxweb.req.content_fd' in environ:
      environ['wsgi.input']=os.fdopen(environ['nxweb.req.content_fd'], 'r')
    else:
      environ['wsgi.input']=None
    environ['wsgi.errors']=sys.stderr
    environ['wsgi.multithread']=True
    environ['wsgi.multiprocess']=False
    environ['wsgi.run_once']=False
    return _call_wsgi_application(WSGI_APP, environ)
  except:
    ei=sys.exc_info()
    traceback.print_exception(*ei)
    return repr(ei[1])+' see log for details'

def _call_wsgi_application(app, environ):
  body_writer=io.BytesIO()
  status_headers=[None, None]
  def start_response(status, headers, exc_info=None):
    status_headers[:]=[status, headers]
    return body_writer
  app_iter=app(environ, start_response)
  try:
    for item in app_iter:
      body_writer.write(item)
  finally:
    if hasattr(app_iter, 'close'):
      app_iter.close()
  return status_headers[0], status_headers[1], body_writer.getvalue()
