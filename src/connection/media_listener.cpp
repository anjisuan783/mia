#include "connection/media_listener.h"

#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_adapter.h"
#include "network/basic_packet_socket_factory.h"

#include "h/media_return_code.h"
#include "utils/sigslot.h"
#include "utils/protocol_utility.h"
#include "common/media_log.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"
#include "connection/h/media_conn_mgr.h"
#include "media_server.h"

namespace ma {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("listener");

void MediaListenerMgr::IMediaListener::OnNewConnectionEvent(
  rtc::AsyncPacketSocket* s, rtc::AsyncPacketSocket* c) {
  auto factory = CreateDefaultHttpProtocalFactory(s, c);
  
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_http, std::move(factory));

  conn->Start();
}

rtc::PacketSocketServerOptions
MediaListenerMgr::IMediaListener::GetSocketType() {
  rtc::PacketSocketServerOptions op;
  op.opts = rtc::PacketSocketFactory::OPT_RAW |
            rtc::PacketSocketFactory::OPT_ADDRESS_REUSE;

  return op;
}

//MediaRtmpListener
class MediaRtmpListener : public MediaListenerMgr::IMediaListener {
 public:
  int Listen(const rtc::SocketAddress&, 
             rtc::PacketSocketFactory*) override;
  void OnNewConnectionEvent(rtc::AsyncPacketSocket*, 
                            rtc::AsyncPacketSocket*) override;
};

void MediaRtmpListener::OnNewConnectionEvent(rtc::AsyncPacketSocket* s, 
                                             rtc::AsyncPacketSocket* c) {
  MLOG_TRACE("new peer:" << c->GetRemoteAddress().ToString() << 
             ", from:" << s->GetLocalAddress().ToString());
}

int MediaRtmpListener::Listen(const rtc::SocketAddress& address, 
                              rtc::PacketSocketFactory* factory) {
  
  rtc::AsyncPacketSocket* s = factory->CreateServerTcpSocket(
      address, 0, 0, GetSocketType());

  if (!s) {
    return kma_listen_failed;
  }
  listen_socket_.reset(s);
  
  listen_socket_->SignalNewConnection.connect(
      this, &MediaRtmpListener::OnNewConnectionEvent);

  return kma_ok;
}

//MediaHttpListener
class MediaHttpListener : public MediaListenerMgr::IMediaListener {
 public:
  int Listen(const rtc::SocketAddress&, 
             rtc::PacketSocketFactory*) override;
  void OnNewConnectionEvent(rtc::AsyncPacketSocket*, 
                            rtc::AsyncPacketSocket*) override;
};

int MediaHttpListener::Listen(const rtc::SocketAddress& address, 
                              rtc::PacketSocketFactory* factory) {
  
  rtc::AsyncPacketSocket* s = factory->CreateServerTcpSocket(
      address, 0, 0, this->GetSocketType());

  if (!s) {
    return kma_listen_failed;
  }
  listen_socket_.reset(s);

  listen_socket_->SignalNewConnection.connect(
      this, &MediaHttpListener::OnNewConnectionEvent);

  return kma_ok;
}

void MediaHttpListener::OnNewConnectionEvent(rtc::AsyncPacketSocket* s, 
                                             rtc::AsyncPacketSocket* c) {
  MLOG_TRACE("new peer:" << c->GetRemoteAddress().ToString() << 
             ", from:" << s->GetLocalAddress().ToString());
  IMediaListener::OnNewConnectionEvent(s, c);
}

//MediaHttpsListener
class MediaHttpsListener : public MediaListenerMgr::IMediaListener {
 public:
  MediaHttpsListener();
  ~MediaHttpsListener() override;
  
  int Listen(const rtc::SocketAddress&, 
             rtc::PacketSocketFactory*) override;
  void OnNewConnectionEvent(rtc::AsyncPacketSocket*, 
                            rtc::AsyncPacketSocket*) override;
 private:
  void CheckInit();
  void CheckClean();
 private:
  rtc::PacketSocketServerOptions GetSocketType() override;
  static bool inited_;
};

bool MediaHttpsListener::inited_{false};

MediaHttpsListener::MediaHttpsListener() {
  CheckInit();
}

MediaHttpsListener::~MediaHttpsListener() {
  CheckClean();
}

int MediaHttpsListener::Listen(const rtc::SocketAddress& address, 
                               rtc::PacketSocketFactory* factory) {
  rtc::AsyncPacketSocket* s = factory->CreateServerTcpSocket(
        address, 0, 0, this->GetSocketType());
  
  if (!s) {
    return kma_listen_failed;
  }
  listen_socket_.reset(s);

  listen_socket_->SignalNewConnection.connect(
      this, &MediaHttpsListener::OnNewConnectionEvent);

  return kma_ok;
}

void MediaHttpsListener::OnNewConnectionEvent(rtc::AsyncPacketSocket* s, 
                                              rtc::AsyncPacketSocket* c) {
  MLOG_TRACE("new peer:" << c->GetRemoteAddress().ToString() << 
             ", from:" << s->GetLocalAddress().ToString());
  IMediaListener::OnNewConnectionEvent(s, c);
}

rtc::PacketSocketServerOptions MediaHttpsListener::GetSocketType() {
  rtc::PacketSocketServerOptions op;
  op.https_certificate = g_server_.config_.https_crt;
  op.https_private_key = g_server_.config_.https_key;
  
  op.opts = rtc::PacketSocketFactory::OPT_RAW |
            rtc::PacketSocketFactory::OPT_TLS_INSECURE |
            rtc::PacketSocketFactory::OPT_ADDRESS_REUSE;
  return op;
}

void MediaHttpsListener::CheckInit() {
  if (!inited_) {
    rtc::InitializeSSL();
    inited_ = true;
  }
}

void MediaHttpsListener::CheckClean() {
  if (inited_) {
    rtc::CleanupSSL();
    inited_ = false;
  }
}

//MediaListenerMgr
MediaListenerMgr::MediaListenerMgr() = default;

int MediaListenerMgr::Init(const std::vector<std::string>& addr) {

  worker_ = std::move(rtc::Thread::CreateWithSocketServer());

  worker_->SetName("media_listener", nullptr);
  bool ret = worker_->Start();
  MA_ASSERT(ret);

  socket_factory_ = std::move(
      std::make_unique<rtc::BasicPacketSocketFactory>(worker_.get()));

  int result = kma_ok;

  for(auto& i : addr) {
    result = worker_->Invoke<int>(RTC_FROM_HERE, [this, i]() {
    std::string_view schema, host;
    int port;
    split_schema_host_port(i, schema, host, port);
    MLOG_DEBUG(i << "[schema:" << schema << ", host:" << host 
                 << ", port:" << port << "]");

    rtc::SocketAddress host_port(std::string{host.data(), host.length()}, port);
      std::unique_ptr<IMediaListener> listener = CreateListener(schema);
      int ret = kma_ok;
      if ((ret = listener->Listen(host_port, socket_factory_.get())) == kma_ok){
        listeners_.emplace_back(std::move(listener));
      }
      return ret;
    });

    if (result != 0) {
      MLOG_CERROR("listen failed, code:%d, address:%s", result, i.c_str());
      return kma_listen_failed;
    }
  }

  return kma_ok;
}

std::unique_ptr<MediaListenerMgr::IMediaListener> 
MediaListenerMgr::CreateListener(std::string_view schema) {
  std::unique_ptr<IMediaListener> p;
  if (schema == "rtmp") {
    p = std::move(std::make_unique<MediaRtmpListener>());
  }

  if (schema == "http") {
    p = std::move(std::make_unique<MediaHttpListener>());
  }

  if (schema == "https") {
    p = std::move(std::make_unique<MediaHttpsListener>());
  }

  return std::move(p);
}

} //namespace ma

