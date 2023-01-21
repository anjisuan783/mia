#include "connection/media_listener.h"

#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_adapter.h"
#include "network/basic_packet_socket_factory.h"

#include "h/media_return_code.h"
#include "utils/sigslot.h"
#include "utils/media_protocol_utility.h"
#include "common/media_log.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"
#include "connection/h/media_conn_mgr.h"
#include "media_server.h"
#include "utils/media_acceptor.h"
#include "utils/media_thread.h"
#include "utils/media_msg_queue.h"

namespace ma {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.connection");

//MediaRtmpListener
class MediaRtmpListener final : public MediaListenerMgr::IMediaListener,
    public AcceptorSink  {
 public:
  // AcceptorSink implement
  void OnAccept(std::shared_ptr<Transport>) override;

  int Listen(const MediaAddress&, 
             rtc::PacketSocketFactory*) override;

  void Stop() override;
 private:
  std::shared_ptr<Acceptor> acceptor_;
};

int MediaRtmpListener::Listen(const MediaAddress& address, 
                              rtc::PacketSocketFactory* factory) {
  if (acceptor_) {
    return kma_already_initilized;
  }

  acceptor_ = AcceptorFactory::CreateAcceptor(true);

  MediaAddress lsn_addr;
  srs_error_t err = acceptor_->Listen(this, address);
  if (srs_success != err) {
    MLOG_ERROR_THIS("Listen faild, addr:" << address.ToString()
        << ", desc:" << srs_error_desc(err));
    delete err;
    return kma_listen_failed;
  }

  return kma_ok;
}

void MediaRtmpListener::Stop() {
  if (!acceptor_) {
    return ;
  }
  srs_error_t err = acceptor_->Stop();
  if (srs_success != err) {
    MLOG_ERROR_THIS("Stop listen faild, desc:" << srs_error_desc(err));
    delete err;
  }
}

void MediaRtmpListener::OnAccept(std::shared_ptr<Transport> t) {
  MLOG_TRACE("new peer:" << t->GetLocalAddr().ToString() << 
             ", from:" << t->GetLocalAddr().ToString());
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_rtmp, std::move(CreateTcpIOFactory(std::move(t))));

  conn->Start();
}

//MediaHttpListener
class MediaHttpListener final: public MediaListenerMgr::IMediaListener,
    public sigslot::has_slots<> {
 public:
  int Listen(const MediaAddress&, 
             rtc::PacketSocketFactory*) override;
  void Stop() override;
  void OnNewConnectionEvent(rtc::AsyncPacketSocket*, 
                            rtc::AsyncPacketSocket*);
  rtc::PacketSocketServerOptions GetSocketType();  
 private:
  std::unique_ptr<rtc::AsyncPacketSocket> listen_socket_;
};

int MediaHttpListener::Listen(const MediaAddress& addr, 
                              rtc::PacketSocketFactory* factory) {
  
  rtc::SocketAddress address(addr.GetHostName(), addr.GetPort());
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

void MediaHttpListener::Stop() {
  listen_socket_->SignalNewConnection.disconnect(this);
  listen_socket_->Close();
}

void MediaHttpListener::OnNewConnectionEvent(
  rtc::AsyncPacketSocket* s, rtc::AsyncPacketSocket* c) {
  MLOG_TRACE("new peer:" << c->GetRemoteAddress().ToString() << 
             ", from:" << s->GetLocalAddress().ToString());    
  auto factory = CreateDefaultHttpProtocalFactory(s, c);
  
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_http, std::move(factory));

  conn->Start();
}

rtc::PacketSocketServerOptions MediaHttpListener::GetSocketType() {
  rtc::PacketSocketServerOptions op;
  op.opts = rtc::PacketSocketFactory::OPT_RAW |
            rtc::PacketSocketFactory::OPT_ADDRESS_REUSE;

  return op;
}

///////////////////////////////////////////////////////////////////////////////
//MediaHttpsListener
///////////////////////////////////////////////////////////////////////////////
class MediaHttpsListener : public MediaListenerMgr::IMediaListener,
    public sigslot::has_slots<> {
 public:
  MediaHttpsListener();
  ~MediaHttpsListener() override;
  
  int Listen(const MediaAddress&, 
             rtc::PacketSocketFactory*) override;
  void Stop() override;
  void OnNewConnectionEvent(rtc::AsyncPacketSocket*, 
                            rtc::AsyncPacketSocket*);
 private:
  void CheckInit();
  void CheckClean();
  rtc::PacketSocketServerOptions GetSocketType();
 private:
  static bool inited_;
  std::unique_ptr<rtc::AsyncPacketSocket> listen_socket_;
};

bool MediaHttpsListener::inited_{false};

MediaHttpsListener::MediaHttpsListener() {
  CheckInit();
}

MediaHttpsListener::~MediaHttpsListener() {
  CheckClean();
}

int MediaHttpsListener::Listen(const MediaAddress& addr, 
                               rtc::PacketSocketFactory* factory) {
  rtc::SocketAddress address(addr.GetHostName(), addr.GetPort());        
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

void MediaHttpsListener::Stop() {
  listen_socket_->SignalNewConnection.disconnect(this);
  listen_socket_->Close();
  CheckClean();
}

void MediaHttpsListener::OnNewConnectionEvent(
  rtc::AsyncPacketSocket* s, rtc::AsyncPacketSocket* c) {
  MLOG_TRACE("new peer:" << c->GetRemoteAddress().ToString() << 
          ", from:" << s->GetLocalAddress().ToString());
  auto factory = CreateDefaultHttpProtocalFactory(s, c);
  
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_http, std::move(factory));

  conn->Start();
}

rtc::PacketSocketServerOptions MediaHttpsListener::GetSocketType() {
  rtc::PacketSocketServerOptions op;
  op.https_certificate = g_server_.config_.https_crt;
  op.https_private_key = g_server_.config_.https_key;
  op.https_hostname = g_server_.config_.https_hostname;
  
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

///////////////////////////////////////////////////////////////////////////////
//MediaHttpsListener
///////////////////////////////////////////////////////////////////////////////
MediaListenerMgr::MediaListenerMgr() = default;

srs_error_t MediaListenerMgr::Init(const std::vector<std::string>& addr) {

  worker_ = std::move(rtc::Thread::CreateWithSocketServer());

  worker_->SetName("media_listener", nullptr);
  bool ret = worker_->Start();
  MA_ASSERT(ret);

  worker1_ = MediaThreadManager::Instance()->CreateNetThread("reactor");

  socket_factory_ = std::move(
      std::make_unique<rtc::BasicPacketSocketFactory>(worker_.get()));

  int result = kma_ok;

  for(auto& i : addr) {
    result = worker_->Invoke<int>(RTC_FROM_HERE, [this, i]() {
      std::string_view schema, host;
      int port;
      split_schema_host_port(i, schema, host, port);

      MediaAddress host_port(std::string{host.data(), host.length()}.c_str(), port);

      int ret = kma_ok;
      if (schema == "rtmp") {
        return ret;
      }
      MLOG_DEBUG(i << "[schema:" << schema << ", host:" << host 
            << ", port:" << port << "]");
      std::unique_ptr<IMediaListener> listener = CreateListener(schema);
      
      if ((ret = listener->Listen(host_port, socket_factory_.get())) == kma_ok){
        listeners_.emplace_back(std::move(listener));
      }
      return ret;
    });

    if (result != kma_ok) {
      return srs_error_new(ERROR_SOCKET_LISTEN, "listen failed, code:%d, address:%s", result, i.c_str());
    }
  }

  srs_error_t err = srs_success;
  for (auto& i : addr) {
    std::string_view schema, host;
    int port;
    split_schema_host_port(i, schema, host, port);
    MediaAddress host_port(std::string{host.data(), host.length()}.c_str(), port);

    if (schema == "rtmp") {
      MLOG_DEBUG(i << "[schema:" << schema << ", host:" << host 
          << ", port:" << port << "]");
#if 1          
      err = worker1_->MsgQueue()->Send([this, i, schema, host_port]()->srs_error_t {
        std::unique_ptr<IMediaListener> listener = CreateListener(schema);
        int ret = kma_ok;
        if ((ret = listener->Listen(host_port, socket_factory_.get())) == kma_ok) {
          listeners_.emplace_back(std::move(listener));
          return srs_success;
        }
        return srs_error_new(ERROR_SOCKET_LISTEN, 
            "Listen failed, %s", host_port.ToString().c_str());
      });
#endif
      if (err) {
        return srs_error_wrap(err, "rtmp listen failed!");
      }
    }
  }

  return err;
}

void MediaListenerMgr::Close() {
  for(auto& i : listeners_) {
    i->Stop();
  }
  listeners_.clear();

  worker_->Stop();
  if (worker1_) {
    worker1_->Stop();
    worker1_->Join();
    worker1_->Destroy();
  }
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

