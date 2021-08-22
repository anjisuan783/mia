#include "media_server.h"

#include "httpapi.h"
#include "common/media_log.h"
#include "connection/h/conn_interface.h"
#include "connection/h/media_conn_mgr.h"
#include "handler/h/media_handler.h"
#include "http/h/http_protocal.h"
#include "media_source_mgr.h"

namespace ma {

void MediaServerImp::Init(unsigned int num1, unsigned int num2) {
  if (inited_) {
    return;
  }
  inited_ = true;

  ServerHandlerFactor factory;
  
  mux_ = std::move(factory.Create());

  g_source_mgr_.Init(num1);

  g_conn_mgr_.Init(num2);
}

srs_error_t MediaServerImp::on_publish(std::shared_ptr<MediaSource> s, 
                                       std::shared_ptr<MediaRequest> r) {
  srs_error_t err = srs_success;
  if ((err = mux_->mount_service(s, r)) != srs_success) {
    return srs_error_wrap(err, "mount service");
  }
    
  return err;
}

void MediaServerImp::on_unpublish(std::shared_ptr<MediaSource> s, 
                                  std::shared_ptr<MediaRequest> r) {
  mux_->unmount_service(s, r);
}

bool MediaServerImp::OnHttpConnect(
    IHttpServer* http_conn, CDataPackage* data) {
  std::string path;
  http_conn->GetRequestPath(path);

  MediaConnMgr::ConnType cType{MediaConnMgr::e_unknow};
  if (path == RTC_PALY_PREFIX || path == RTC_PUBLISH_PREFIX) {
    cType = MediaConnMgr::e_http;
  } else {
    size_t pos = path.find(".flv");
    if(pos != path.npos) {
      cType = MediaConnMgr::e_flv;
    }
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

MediaServerImp g_server_;
MediaServerApi* MediaServerFactory::Create() {
  return &g_server_;
}

}

