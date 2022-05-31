#ifndef __MEDIA_IO_IMP_H__
#define __MEDIA_IO_IMP_H__

#include "connection/h/media_io.h"

namespace ma {

class MediaTcpIO : public IMediaIO {
 public:
  MediaTcpIO(std::unique_ptr<rtc::AsyncPacketSocket> s) 
    : sock_(std::move(s)) { }
  ~MediaTcpIO() override = default;

  void Open() override;
  void Close() override;

  srs_error_t Write(MessageChain* data, int* sent) override;

  void SetRecvTimeout(uint32_t);
  void SetSendTimeout(uint32_t);
 private:
  std::unique_ptr<rtc::AsyncPacketSocket> sock_;
};

} //namespace ma

#endif //!__MEDIA_TRANSPORT_IMP_H__
