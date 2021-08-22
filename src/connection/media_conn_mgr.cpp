#include "connection/h/media_conn_mgr.h"

#include "connection/h/conn_interface.h"
#include "connection/http_conn.h"
#include "connection/rtmp_conn.h"
#include "handler/h/media_handler.h"
#include "media_server.h"

namespace ma {

//static std::shared_ptr<wa::ThreadPool>  workers_;

void MediaConnMgr::Init(unsigned int) {
  //workers_ = std::make_shared<wa::ThreadPool>(num);
}

std::shared_ptr<IMediaConnection> MediaConnMgr::CreateConnection(
    ConnType type, std::unique_ptr<IHttpProtocalFactory> factory) {

  std::shared_ptr<IMediaConnection> conn;
  if (e_http == type) {
    conn = std::make_shared<GsHttpConn>(std::move(factory), g_server_.mux_.get());
  } else if (e_flv == type) {
    conn = std::make_shared<GsResponseOnlyHttpConn>(std::move(factory), g_server_.mux_.get());
  } else {
    conn = std::make_shared<GsDummyConnection>();
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

