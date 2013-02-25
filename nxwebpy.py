
def on_request(uri, if_modified_since):
  return 'Hello, python! (%s, %d)' % (uri, if_modified_since)
