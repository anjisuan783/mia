#include "connection/h/media_conn_mgr.h"

#ifdef __GS__
#include <byteswap.h>
#define htobe16(x) __bswap_16 (x)
#define htole16(x) (x)
#define be16toh(x) __bswap_16 (x)
#define le16toh(x) (x)

#define htobe32(x) __bswap_32 (x)
#define htole32(x) (x)
#define be32toh(x) __bswap_32 (x)
#define le32toh(x) (x)

#define htobe64(x) __bswap_64 (x)
#define htole64(x) (x)
#define be64toh(x) __bswap_64 (x)
#define le64toh(x) (x)
#endif

#include "h/media_return_code.h"
#include "connection/h/conn_interface.h"
#include "connection/http_conn.h"
#include "connection/rtmp_conn.h"
#include "handler/h/media_handler.h"
#include "utils/protocol_utility.h"
#include "media_server.h"
#include "utils/Worker.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/async_packet_socket.h"
#include "network/basic_packet_socket_factory.h"
#include "rtc_base/thread.h"
#include "utils/sigslot.h"

namespace ma {

class MediaRtmpListener : public sigslot::has_slots<> {
  MDECLARE_LOGGER();
  
 public:
  int Listen(const rtc::SocketAddress&, rtc::PacketSocketFactory*);
 protected:
  void OnConnectEvent(rtc::AsyncPacketSocket*);
  void OnNewConnectionEvent(rtc::AsyncPacketSocket*, rtc::AsyncPacketSocket*);
  void OnAddressReadyEvent(rtc::AsyncPacketSocket*, const rtc::SocketAddress&);
  void OnReadEvent(rtc::AsyncPacketSocket*,
                   const char*,
                   size_t,
                   const rtc::SocketAddress&,
                   const int64_t&);
  void OnSentEvent(rtc::AsyncPacketSocket*, const rtc::SentPacket&);
  void OnWriteEvent(rtc::AsyncPacketSocket* socket);
  void OnCloseEvent(rtc::AsyncPacketSocket* socket, int err);

 private:
  std::unique_ptr<rtc::AsyncPacketSocket> listen_socket_;
};

MDEFINE_LOGGER(MediaRtmpListener, "MediaRtmpListener");

void MediaRtmpListener::OnConnectEvent(rtc::AsyncPacketSocket*) {
  MLOG_INFO("");
}

void MediaRtmpListener::OnNewConnectionEvent(rtc::AsyncPacketSocket*, rtc::AsyncPacketSocket*) {
  MLOG_INFO("");
}

void MediaRtmpListener::OnAddressReadyEvent(rtc::AsyncPacketSocket*, const rtc::SocketAddress&) {
  MLOG_INFO("");
}

void MediaRtmpListener::OnReadEvent(rtc::AsyncPacketSocket*,
                                    const char*,
                                    size_t,
                                    const rtc::SocketAddress&,
                                    const int64_t&) {
  MLOG_INFO("");
}

void MediaRtmpListener::OnSentEvent(rtc::AsyncPacketSocket*, const rtc::SentPacket&) {
  MLOG_INFO("");
}

void MediaRtmpListener::OnWriteEvent(rtc::AsyncPacketSocket* socket) {
  MLOG_INFO("");
}

void MediaRtmpListener::OnCloseEvent(rtc::AsyncPacketSocket* socket, int err) {
  MLOG_INFO("");
}


int MediaRtmpListener::Listen(const rtc::SocketAddress& address, 
                              rtc::PacketSocketFactory* factory) {
  
  rtc::AsyncPacketSocket* s = factory->CreateServerTcpSocket(address, 0, 0, 0);

  if (!s) {
    MLOG_ERROR("listen failed, address:" << address.ToString());
    return kma_listen_failed;
  }
  listen_socket_.reset(s);
  
  listen_socket_->SignalReadPacket.connect(this, &MediaRtmpListener::OnReadEvent);
  listen_socket_->SignalSentPacket.connect(this, &MediaRtmpListener::OnSentEvent);
  listen_socket_->SignalReadyToSend.connect(this, &MediaRtmpListener::OnWriteEvent);
  listen_socket_->SignalAddressReady.connect(this, &MediaRtmpListener::OnAddressReadyEvent);
  listen_socket_->SignalConnect.connect(this, &MediaRtmpListener::OnConnectEvent);
  listen_socket_->SignalClose.connect(this, &MediaRtmpListener::OnCloseEvent);
  listen_socket_->SignalNewConnection.connect(this, &MediaRtmpListener::OnNewConnectionEvent);

  return kma_ok;
}

class MediaListener {
  MDECLARE_LOGGER();
  
 public:
  MediaListener();
  int Init(const std::vector<std::string>& addr);

 private:
  int listen_rtmp(const rtc::SocketAddress& address);
 
  std::unique_ptr<rtc::Thread> worker_;
  std::unique_ptr<rtc::PacketSocketFactory> socket_factory_;
  std::vector<std::unique_ptr<MediaRtmpListener>> rtmp_listeners_;
};

MDEFINE_LOGGER(MediaListener, "MediaListener");

MediaListener::MediaListener() = default;

int MediaListener::Init(const std::vector<std::string>& addr) {

  worker_ = std::move(rtc::Thread::CreateWithSocketServer());

  worker_->SetName("media_listener", nullptr);
  bool ret = worker_->Start();
  MA_ASSERT(ret);

  socket_factory_.reset(new rtc::BasicPacketSocketFactory);

  int result = kma_ok;

  for(auto& i : addr) {
    std::string_view schema, host;
    int port;
    
    split_schema_host_port(i, schema, host, port);

    if (schema == "rtmp") {
      rtc::SocketAddress host_port(host.data(), port);
      result = worker_->Invoke<int>(RTC_FROM_HERE, [this, host_port]() {
        return listen_rtmp(host_port);
      });

      if (result != 0) {
        MLOG_CERROR("listen rtmp failed, code:%d, address:%s", result, i.c_str());
        return kma_listen_failed;
      }
    } else if (schema == "http") {
    } else if (schema == "https") {
    }
  }

  return kma_ok;
}

int MediaListener::listen_rtmp(const rtc::SocketAddress& address) {
  auto listener = std::make_unique<MediaRtmpListener>();
  int ret = listener->Listen(address, socket_factory_.get());
  
  rtmp_listeners_.emplace_back(std::move(listener));
  return ret;
}

MDEFINE_LOGGER(MediaConnMgr, "MediaConnMgr");

int MediaConnMgr::Init(uint32_t ioworkers, const std::vector<std::string>& addr) {
  if (addr.empty()) {
    return kma_ok;
  }
  
  listener_ = std::move(std::make_unique<MediaListener>());
  return listener_->Init(addr);
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

