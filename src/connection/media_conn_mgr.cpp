#include "connection/h/media_conn_mgr.h"

#include "h/media_return_code.h"
#include "connection/h/conn_interface.h"
#include "connection/http_conn.h"
#include "connection/rtmp_conn.h"
#include "handler/h/media_handler.h"
#include "utils/protocol_utility.h"
#include "media_server.h"
#include "utils/Worker.h"
#include "connection/media_listener.h"

namespace ma {

int MediaConnMgr::Init(uint32_t ioworkers, const std::vector<std::string>& addrs) {
  if (addrs.empty()) {
    return kma_ok;
  }
  
  listener_ = std::move(std::make_unique<MediaListenerMgr>());
  return listener_->Init(addrs);
}

std::shared_ptr<IMediaConnection> MediaConnMgr::CreateConnection(
    ConnType type, std::unique_ptr<IHttpProtocalFactory> factory) {

  std::shared_ptr<IMediaConnection> conn;
  if (e_http == type) {
    conn = std::make_shared<MediaHttpConn>(std::move(factory), g_server_.mux_.get());
  } else if (e_flv == type) {
    conn = std::make_shared<MediaResponseOnlyHttpConn>(std::move(factory), g_server_.mux_.get());
  } else {
    conn = std::make_shared<MediaDummyConnection>();
  }

  std::lock_guard<std::mutex> guard(source_lock_);
  connections_.insert(
      std::make_pair(static_cast<IMediaConnection*>(conn.get()), conn));

  return conn;
}

void MediaConnMgr::RemoveConnection(std::shared_ptr<IMediaConnection> p) {
  signal_destroy_conn_(p);

  std::lock_guard<std::mutex> guard(source_lock_);
  connections_.erase(p.get());
}

MediaConnMgr g_conn_mgr_;

}

