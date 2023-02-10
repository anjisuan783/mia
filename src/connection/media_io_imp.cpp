#include "connection/media_io_imp.h"

#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include  "utils/media_msg_chain.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.rtmp");

//MediaTcpIO
MediaTcpIO::MediaTcpIO(std::shared_ptr<Transport> s)
  : sock_(std::move(s)) {
  srs_error_t err = sock_->Open(this);
  if (srs_success != err) {
    MLOG_ERROR("transport open failed, desc" << srs_error_desc(err));
    delete err;
  }
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
  sock_ = nullptr;
}

std::string MediaTcpIO::GetLocalAddress() const {
  std::string ip("");
  if(sock_) {
    ip = sock_->GetLocalAddr().ToString();
  }

  return ip;
}

std::string MediaTcpIO::GetRemoteAddress() const {
  std::string ip("");
  if(sock_) {
    ip = sock_->GetPeerAddr().ToString();
  }
  return ip;
}

srs_error_t MediaTcpIO::Write(MessageChain* msg, int* sent) {
  srs_error_t err = srs_success;
  if (!sock_) {
    return srs_error_new(ERROR_SOCKET_CLOSED, "socket closed");
  }

  if (!msg)
    return err;

  int msg_sent = 0;

  int ret = sock_->Write(*msg, msg_sent, false);
  if (ERROR_SUCCESS != ret && ERROR_SOCKET_WOULD_BLOCK != ret) {
      return srs_error_new(ret, "transport write");
  }

  if (sent) {
    *sent = msg_sent;
  }
  return err;
}

int MediaTcpIO::OnRead(MessageChain& msg) {
  srs_error_t err = srs_success;
  if (!sink_) {
    return 0;
  }
  uint64_t total = 0;
  if (srs_success != (err = sink_->OnRead(&msg))) {
    if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
      sock_->GetOpt(TP_RECEIVED_BYTES, &total);
      MLOG_TRACE_THIS("total recv " << total);
      sink_->OnClose(srs_error_wrap(err, "onread failure"));
      return -1;
    }
    srs_freep(err);
  }
  return 0;
}

int MediaTcpIO::OnWrite() {
  srs_error_t err = srs_success;
  if (sink_) {
    return 0;
  }

  if (srs_success != (err = sink_->OnWrite())) {
    if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
      sink_->OnClose(srs_error_wrap(err, "onwrite failure"));
      return -1;
    }
    srs_freep(err);
  }
  return 0;
}

int MediaTcpIO::OnClose(srs_error_t reason) {
  if (sink_) {
    sink_->OnClose(srs_error_wrap(reason, "OnClose"));
  }
  return 0;
}

void MediaTcpIO::SetRecvTimeout(uint32_t) {

}

void MediaTcpIO::SetSendTimeout(uint32_t) {

}

//MediaTcpIOFactory
class MediaTcpIOFactory : public IMediaIOFactory {
 public:
  MediaTcpIOFactory(std::shared_ptr<Transport> t) : sock_(std::move(t)) {}
  std::shared_ptr<IMediaIO> CreateIO() override {
    return std::dynamic_pointer_cast<IMediaIO>(
        std::make_shared<MediaTcpIO>(std::move(sock_)));
  }
 private:
  std::shared_ptr<Transport> sock_;
};

std::unique_ptr<IMediaIOBaseFactory> CreateTcpIOFactory(
    std::shared_ptr<Transport> t) {
  return std::make_unique<MediaTcpIOFactory>(t);
}

} //namespace ma
