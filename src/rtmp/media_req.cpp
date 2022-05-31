#include "rtmp/media_req.h"

#include "common/media_log.h"
#include "utils/media_protocol_utility.h"
#include "common/media_consts.h"
#include "rtmp/media_amf0.h"

namespace ma {

#define RTMP_SIG_AMF0_VER   0

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.rtmp");

MediaRequest::MediaRequest() {
  objectEncoding = RTMP_SIG_AMF0_VER;
  duration = -1;
  port = SRS_CONSTS_RTMP_DEFAULT_PORT;
}

MediaRequest::~MediaRequest() {
  srs_freep(args);
}

std::shared_ptr<MediaRequest> MediaRequest::copy() {
  auto cp = std::make_shared<MediaRequest>();
  
  cp->ip = ip;
  cp->vhost = vhost;
  cp->app = app;
  cp->objectEncoding = objectEncoding;
  cp->pageUrl = pageUrl;
  cp->host = host;
  cp->port = port;
  cp->param = param;
  cp->schema = schema;
  cp->stream = stream;
  cp->swfUrl = swfUrl;
  cp->tcUrl = tcUrl;
  cp->duration = duration;
  if (args) {
    cp->args = args->copy()->to_object();
  }
  
  return std::move(cp);
}

void MediaRequest::update_auth(MediaRequest* req) {
  pageUrl = req->pageUrl;
  swfUrl = req->swfUrl;
  tcUrl = req->tcUrl;
  param = req->param;
  
  ip = req->ip;
  vhost = req->vhost;
  app = req->app;
  objectEncoding = req->objectEncoding;
  host = req->host;
  port = req->port;
  param = req->param;
  schema = req->schema;
  duration = req->duration;
  
  if (args) {
      srs_freep(args);
  }
  if (req->args) {
      args = req->args->copy()->to_object();
  }
  
  MLOG_INFO("update req of soruce for auth ok");
}

std::string MediaRequest::get_stream_url() {
  return srs_generate_stream_url(vhost, app, stream);
}

void MediaRequest::strip() {
  // remove the unsupported chars in names.
  host = srs_string_remove(host, "/ \n\r\t");
  vhost = srs_string_remove(vhost, "/ \n\r\t");
  app = srs_string_remove(app, " \n\r\t");
  stream = srs_string_remove(stream, " \n\r\t");
  
  // remove end slash of app/stream
  app = srs_string_trim_end(app, "/");
  stream = srs_string_trim_end(stream, "/");
  
  // remove start slash of app/stream
  app = srs_string_trim_start(app, "/");
  stream = srs_string_trim_start(stream, "/");
}

MediaRequest* MediaRequest::as_http() {
  schema = "http";
  return this;
}

}

