import asyncio
import argparse
import logging
import os
import ssl

from aiohttp import web

logger = logging.getLogger("http")

def init_logger(logger, debug, console):
  if debug:
    logging.basicConfig(level=logging.DEBUG)
  else:
    logging.basicConfig(level=logging.INFO)

  formatter = logging.Formatter(
    '%(asctime)s - %(name)s - %(levelname)s - %(message)s')

  # add formatter to console handler
  if console:
    ch = logging.StreamHandler()  
    ch.setFormatter(formatter)
    logger.addHandler(ch)

  # create file handler
  fh = logging.FileHandler('http.log')
  fh.setFormatter(formatter)
  logger.addHandler(fh)

def log_debug(msg, *args):
  logger.debug(msg, *args)

def log_info(msg, *args):
  logger.info(msg, *args)

def log_warn(msg, *args):
  logger.warn(msg, *args)

def log_error(msg, *args):
  logger.error(msg, *args)

def GetContextType(path) :
  ctype = ""

  if path.rfind(".html") != -1:
    ctype = "text/html"
  elif path.rfind(".js") != -1:
    ctype = "application/javascript"
  elif path.rfind(".png") != -1:
    ctype = "image/png"
  elif path.rfind(".jpg") != -1 or path.rfind(".jpeg") != -1:
    ctype = "image/jpeg"
  elif path.rfind(".gif") != -1:
    ctype = "image/gif"
  elif path.rfind(".css") != -1:
    ctype = "text/css"
  elif path.rfind(".xml") != -1:
    ctype = "text/xml"

  return ctype
    

class HttpHandler:
  def __init__(self):
    routes = web.RouteTableDef()

    self.root = os.path.dirname(__file__)

    @routes.get(r'/{name:\S*}')
    async def OnRequest(request):
      log_debug(request.url)
      name = request.match_info['name']
    
      name_len = len(name)
      if name_len == 0 or name[name_len -1] == '/':
        name += "index.html"

      ctype = GetContextType(name)
    
      if ctype == "":
        return web.Response(status=404)

      try:
        content = open(os.path.join(self.root, name), "r").read()
      except IOError:
        log_error('{} not found'.format(name))
        return web.Response(status=404)

      return web.Response(content_type=ctype, text=content)

    self.app = web.Application()
    self.app.add_routes(routes)
    self.app.on_shutdown.append(self.OnShutdown)

  async def OnShutdown(app):
    pass

  def Run(self, args):
    ssl_context: Optional[SSLContext] = None
    if args.cert_file and args.key_file:
      ssl_context = ssl.SSLContext()
      ssl_context.load_cert_chain(args.cert_file, args.key_file)

    if ssl_context != None:
      if args.port == 80:
        args.port = 443

    if args.www_path:
      self.root = args.www_path

    log_info("port:{}, www:{}".format(args.port, self.root))

    web.run_app(
      self.app, access_log=None, host=args.host, port=args.port, 
      ssl_context = ssl_context)

if __name__ == "__main__":
  parser = argparse.ArgumentParser(
    description="mia players demo"
  )
  parser.add_argument("--host", default="0.0.0.0", 
    help="Host for HTTP server (default: 0.0.0.0)"
  )

  parser.add_argument("--port", type=int, default=80,
    help="Port for HTTP server (default: 80/443)"
  )

  parser.add_argument("--cert-file", help="SSL certificate file (for HTTPS)")
  parser.add_argument("--key-file", help="SSL key file (for HTTPS)")
  parser.add_argument("--www-path", help="resource files")

  parser.add_argument("--verbose", "-v", action="count")
  args = parser.parse_args()

  init_logger(logger, args.verbose, False)

  handler = HttpHandler();
  handler.Run(args)
