#include "connection/media_io_imp.h"

#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include  "utils/media_msg_chain.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.rtmp");

//MediaTcpIO
MediaTcpIO::MediaTcpIO(std::unique_ptr<rtc::AsyncPacketSocket> s)
  : sock_(std::move(s)) {
  sock_->SignalReadyToSend.connect(this, &MediaTcpIO::OnWriteEvent);
  sock_->SignalReadPacket.connect(this, &MediaTcpIO::OnReadEvent);
  sock_->SignalClose.connect(this, &MediaTcpIO::OnCloseEvent);
}

MediaTcpIO::~MediaTcpIO() {
  sock_->Close();
}

void MediaTcpIO::Open(IMediaIOSink* sink) {
  sink_ = sink;
}

void MediaTcpIO::Close() {
  sink_ = nullptr;

  if (!sock_) return ;

  sock_->Close();
  sock_->SignalReadyToSend.disconnect(this);
  sock_->SignalReadPacket.disconnect(this);
  sock_->SignalClose.disconnect(this);
  sock_.reset(nullptr);
}

std::string MediaTcpIO::GetLocalAddress() const {
  std::string ip("");
  if(sock_) {
    ip = sock_->GetLocalAddress().ipaddr().ToString();
  }

  return ip;
}

std::string MediaTcpIO::GetRemoteAddress() const {
  std::string ip("");
  if(sock_) {
    ip = sock_->GetRemoteAddress().ipaddr().ToString();
  }
  return ip;
}

srs_error_t MediaTcpIO::Write(MessageChain* msg, int* sent) {
  if (!sock_) {
    return srs_error_new(ERROR_SOCKET_CLOSED, "socket closed");
  }

  srs_error_t err = srs_success;
  int isent = 0;
  MessageChain* pnext = msg;
  while (pnext) {
    int msg_sent = 0;
    uint32_t len = pnext->GetFirstMsgLength();
    if (len != 0) {
      const char* c_data = pnext->GetFirstMsgReadPtr();
      MA_ASSERT(c_data + len == pnext->GetFirstMsgWritePtr());
      err = Write_i(c_data, len, &msg_sent);
      isent += msg_sent;
      if (err != srs_success) {
        //error occur, mybe would block
        MA_ASSERT(srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK);
        if (srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK) {
        }
        break;
      }
    }
    pnext = pnext->GetNext();
  }
  if (sent) {
    *sent = isent;
  }
  
  return err;
}

void MediaTcpIO::OnReadEvent(rtc::AsyncPacketSocket*,
                  const char* data,
                  size_t cb,
                  const rtc::SocketAddress&,
                  const int64_t&) {
  srs_error_t err = srs_success;
  MessageChain msg(cb, data, MessageChain::DONT_DELETE, cb);
  if (sink_) {
    err = sink_->OnRead(&msg);
  }

  if (srs_success != err) {
    if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
      sink_->OnDisconnect(err);
      this->Close();
    } else {
      delete err;
    }
  }
}

void MediaTcpIO::OnCloseEvent(rtc::AsyncPacketSocket* socket, int err) {
  if (sink_) {
    sink_->OnDisconnect(srs_error_new(err, "OnCloseEvent"));
  }
}

void MediaTcpIO::OnWriteEvent(rtc::AsyncPacketSocket*) {
  srs_error_t err = srs_success;
  if (sink_) {
    err = sink_->OnWrite();
  }

  if (srs_success != err) {
    MLOG_ERROR("rtmp read failure, desc:" << srs_error_desc(err));
    delete err;
  }
}

void MediaTcpIO::SetRecvTimeout(uint32_t) {

}

void MediaTcpIO::SetSendTimeout(uint32_t) {

}


srs_error_t MediaTcpIO::Write_i(const char* c_data, int c_size, int* sent) {
  srs_error_t err = srs_success;
  
  int left_size = c_size;
  int size;
  const char* data;
  
  do {
    data = c_data + c_size - left_size;
    size = std::min(left_size, kMaxPacketSize);
    
    if (size <= 0) {
      break;
    }
    
    rtc::PacketOptions option;
    int ret = sock_->Send(data, size, option);
    if (UNLIKELY(ret <= 0)) {
      MA_ASSERT(EMSGSIZE != sock_->GetError());
      if (sock_->GetError() == EWOULDBLOCK) {
        err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, 
            "need on send, sent:%d", c_size - left_size);
      } else {
        err = srs_error_new(ERROR_SOCKET_ERROR, 
            "unexpect low level error code:%d", sock_->GetError());
      }
      break;
    }

    // must be total sent
    MA_ASSERT(ret == size);
    left_size -= ret;
  } while(true);

  if (sent) {
    *sent = c_size - left_size;
  }

  return err;
}

//MediaTcpIOFactory
class MediaTcpIOFactory : public IMediaIOFactory {
 public:
  MediaTcpIOFactory(rtc::AsyncPacketSocket* s) : sock_(s) {}
  std::shared_ptr<IMediaIO> CreateIO() override {
    return std::dynamic_pointer_cast<IMediaIO>(
        std::make_shared<MediaTcpIO>(std::move(sock_)));
  }
 private:
  std::unique_ptr<rtc::AsyncPacketSocket> sock_;
};

std::unique_ptr<IMediaIOBaseFactory> CreateTcpIOFactory(
    rtc::AsyncPacketSocket*s , rtc::AsyncPacketSocket* c) {
  return std::make_unique<MediaTcpIOFactory>(c);
}

} //namespace ma
