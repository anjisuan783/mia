#include "media_server.h"

#include "h/media_return_code.h"
#include "media_source_mgr.h"
#include "handler/h/media_handler.h"
#include "connection/h/media_conn_mgr.h"
#include "media_source.h"
#include "rtmp/media_req.h"

namespace ma {

MDEFINE_LOGGER(MediaServerImp, "MediaServer");

int MediaServerImp::Init(const Config& _config) {
  if (inited_) {
    return kma_already_initilized;
  }
  
  inited_ = true;

  config_ = _config;

  rtc::LogMessage::AddLogToStream(this, rtc::LS_INFO);

  ServerHandlerFactor factory;
  
  mux_ = std::move(factory.Create());

  mux_->init();

  g_source_mgr_.Init(config_.workers_);

  return g_conn_mgr_.Init(config_.ioworkers_, config_.listen_addr_);
}

srs_error_t MediaServerImp::on_publish(std::shared_ptr<MediaSource> s, 
                                       std::shared_ptr<MediaRequest> r) {
  srs_error_t err = srs_success;
  if ((err = mux_->mount_service(std::move(s), std::move(r))) != srs_success) {
    return srs_error_wrap(err, "mount service");
  }
    
  return err;
}

void MediaServerImp::on_unpublish(std::shared_ptr<MediaSource> s, 
                                  std::shared_ptr<MediaRequest> r) {
  mux_->unmount_service(std::move(s), std::move(r));
}

void MediaServerImp::OnLogMessage(const std::string& message) {
  MLOG_INFO(message);
}

MediaServerImp g_server_;
MediaServerApi* MediaServerFactory::Create() {
  return &g_server_;
}

}

