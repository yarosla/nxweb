import sys
import io
import hello

WSGI_APP=hello.ping_app

def _nxweb_on_request(environ):
  try:
    environ['wsgi.version']=(1, 0)
    if 'nxweb.req.content' in environ:
      environ['wsgi.input']=io.BytesIO(environ.get('nxweb.req.content', ''))
    environ['wsgi.errors']=sys.stderr
    environ['wsgi.multithread']=True
    environ['wsgi.multiprocess']=False
    environ['wsgi.run_once']=False
    return _call_wsgi_application(WSGI_APP, environ)
  except Exception as e:
    return 'Error: '+repr(e)

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
