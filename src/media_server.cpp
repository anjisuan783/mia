#include "media_server.h"

#include "h/media_return_code.h"
#include "media_source_mgr.h"
#include "handler/h/media_handler.h"
#include "connection/h/media_conn_mgr.h"

namespace ma {

int MediaServerImp::Init(const config& _config) {
  if (inited_) {
    return kma_already_initilized;
  }
  
  inited_ = true;

  config_ = _config;

  ServerHandlerFactor factory;
  
  mux_ = std::move(factory.Create());

  g_source_mgr_.Init(config_.workers_);

  return g_conn_mgr_.Init(config_.ioworkers_, config_.listen_addr_);
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

MediaServerImp g_server_;
MediaServerApi* MediaServerFactory::Create() {
  return &g_server_;
}

}

