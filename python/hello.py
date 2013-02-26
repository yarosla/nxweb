from cgi import parse_qs, escape
import time

def ping_app(environ, start_response):
    status = '200 OK'
    output = 'Pong!'

    response_headers = [('Content-type', 'text/plain'),
                        ('Content-Length', str(len(output)))]
    start_response(status, response_headers)
    return [output]

def hello_world_app(environ, start_response):
  parameters=parse_qs(environ.get('QUERY_STRING', ''))
  if 'sleep' in parameters:
    time.sleep(5)
  if 'subject' in parameters:
    subject=escape(parameters['subject'][0])
  else:
    subject='World'
  start_response('200 OK', [('Content-Type', 'text/plain')])
  result='Hello, %(subject)s!\n' % {'subject': subject}
  for key, value in iter(sorted(environ.iteritems())):
    result+=key+'='+str(value)+'\n'
  if environ.get('CONTENT_LENGTH', 0):
    result+='bytes read='+environ['wsgi.input'].read()
  # environ['wsgi.errors'].write(result+'\n')
  return [result]
