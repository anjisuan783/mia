#ifdef __GS__

#include "media_gs_server.h"

#include <memory>
#include "common/media_log.h"
#include "connection/h/conn_interface.h"
#include "handler/h/media_handler.h"
#include "http/h/http_protocal.h"
#include "connection/h/media_conn_mgr.h"

namespace ma {

bool GsServer::OnHttpConnect(IHttpServer* http_conn, CDataPackage* data) {
  std::string path;
  http_conn->GetRequestPath(path);

  MediaConnMgr::ConnType cType{MediaConnMgr::e_unknow};
  size_t pos = path.find(".flv");
 
  if (/*path == RTC_PALY_PREFIX || */path == RTC_PUBLISH_PREFIX) {
    cType = MediaConnMgr::e_http;
  } else if(pos != path.npos) {
    cType = MediaConnMgr::e_flv;
  }

  if (MediaConnMgr::e_unknow == cType) {
    return false;
  }

  MLOG_TRACE("connection:" << http_conn << ", path:" << path);

  auto factory = CreateDefaultHttpProtocalFactory((void*)http_conn, (void*)data);

  auto conn = g_conn_mgr_.CreateConnection(cType, std::move(factory));

  conn->Start();
  
  return true;
}

GsServer g_gs_;

IGsServer* GsServerFactory::Create() {
  return &g_gs_;
}

}

#endif

