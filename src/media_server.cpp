#include "media_server.h"

#include <iostream>

#include "h/rtc_return_value.h"
#include "h/media_return_code.h"
#include "media_source_mgr.h"
#include "handler/h/media_handler.h"
#include "connection/h/media_conn_mgr.h"
#include "media_source.h"
#include "rtmp/media_req.h"
#include "http/http_consts.h"
#include "media_statistics.h"

namespace ma {

MDEFINE_LOGGER(MediaServerImp, "ma.server");

int MediaServerImp::Init(const Config& _config) {
  if (inited_) {
    return kma_already_initilized;
  }
  
  inited_ = true;

  config_ = _config;
  if (config_.vhost.empty()) {
    config_.vhost = SRS_CONSTS_RTMP_DEFAULT_VHOST;
  }

  rtc::LogMessage::AddLogToStream(this, rtc::LS_INFO);

  ServerHandlerFactor factory;
  
  mux_ = std::move(factory.Create());

  mux_->init();

  int rv = g_source_mgr_.Init(config_.workers_);

  if (rv != wa::wa_ok) {
    MLOG_ERROR("wa init failed. code:" << rv);
    return kma_invalid_argument;
  }

  return g_conn_mgr_.Init(config_.ioworkers_, config_.listen_addr_);
}

void MediaServerImp::Close() {
  g_conn_mgr_.Close();
  g_source_mgr_.Close();
  rtc::LogMessage::RemoveLogToStream(this);
}

srs_error_t MediaServerImp::OnPublish(std::shared_ptr<MediaSource> s, 
                                      std::shared_ptr<MediaRequest> r) {
  srs_error_t err = srs_success;
  if ((err = mux_->mount_service(std::move(s), std::move(r))) != srs_success) {
    return srs_error_wrap(err, "mount service");
  }
    
  return err;
}

void MediaServerImp::OnUnpublish(std::shared_ptr<MediaSource> s, 
                                  std::shared_ptr<MediaRequest> r) {
  mux_->unmount_service(std::move(s), std::move(r));
}

void MediaServerImp::OnLogMessage(const std::string& message) {
  MLOG_TRACE(message);
}

void MediaServerImp::Dump() {
  std::cout << "client:" << Stat().Clients() << std::endl;
  std::cout << "streams:" << Stat().Streams() << std::endl;
}

MediaServerImp g_server_;
MediaServerApi* MediaServerFactory::Create() {
  return &g_server_;
}

}
