#include "connection/h/media_conn_mgr.h"

#include "h/media_return_code.h"
#include "connection/h/conn_interface.h"
#include "connection/http_conn.h"
#include "handler/h/media_handler.h"
#include "utils/media_protocol_utility.h"
#include "media_server.h"
#include "utils/Worker.h"
#include "connection/media_listener.h"
#include "connection/media_rtmp_conn.h"

namespace ma {

srs_error_t MediaConnMgr::Init(const std::vector<std::string>& addrs) {
  if (addrs.empty()) {
    return srs_error_new(ERROR_INVALID_ARGS, "addrs is empty!");
  }
  
  listener_ = std::move(std::make_unique<MediaListenerMgr>());
  return listener_->Init(addrs);
}

void MediaConnMgr::Close() {
  listener_->Close();

  std::lock_guard<std::mutex> guard(source_lock_);
  for(auto& i : connections_) {
    i.second->Disconnect();
  }
  connections_.clear();
}

std::shared_ptr<IMediaConnection> MediaConnMgr::CreateConnection(
    ConnType type, std::unique_ptr<IMediaIOBaseFactory> factory) {

  std::shared_ptr<IMediaConnection> conn;
  if (e_http == type) {
    std::unique_ptr<IHttpProtocalFactory> pFactory( 
        dynamic_cast<IHttpProtocalFactory*>(factory.release()));
    conn = std::make_shared<MediaHttpConn>(
        std::move(pFactory), g_server_.mux_.get());
  } else if (e_flv == type) {
    std::unique_ptr<IHttpProtocalFactory> pFactory( 
        dynamic_cast<IHttpProtocalFactory*>(factory.release()));
    conn = std::make_shared<MediaResponseOnlyHttpConn>(
        std::move(pFactory), g_server_.mux_.get());
  } else if (e_rtmp == type) {
    std::unique_ptr<IMediaIOFactory> pFactory( 
        dynamic_cast<IMediaIOFactory*>(factory.release()));
    conn = std::make_shared<MediaRtmpConn>(
        std::move(pFactory), g_server_.mux_.get());
  } else {
    return nullptr;
  }

  std::lock_guard<std::mutex> guard(source_lock_);
  connections_.insert(
      std::make_pair(static_cast<IMediaConnection*>(conn.get()), conn));

  return std::move(conn);
}

void MediaConnMgr::RemoveConnection(std::shared_ptr<IMediaConnection> p) {
  signal_destroy_conn_(p);

  std::lock_guard<std::mutex> guard(source_lock_);
  connections_.erase(p.get());
}

MediaConnMgr g_conn_mgr_;

}

