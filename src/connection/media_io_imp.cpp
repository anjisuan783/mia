#include "connection/media_io_imp.h"

#include "common/media_kernel_error.h"
#include "common/media_log.h"


namespace ma {

//MediaTcpIO
void MediaTcpIO::Open() {

}

void MediaTcpIO::Close() {

}

srs_error_t MediaTcpIO::Write(MessageChain* data, int* sent) {
  srs_error_t err = nullptr;
  return err;
}

void MediaTcpIO::SetRecvTimeout(uint32_t) {

}

void MediaTcpIO::SetSendTimeout(uint32_t) {

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
