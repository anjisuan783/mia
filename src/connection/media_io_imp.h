#ifndef __MEDIA_IO_IMP_H__
#define __MEDIA_IO_IMP_H__

#include "common/media_define.h"
#include "connection/h/media_io.h"
#include "utils/media_transport.h"

namespace ma {

class MediaTcpIO : public IMediaIO, public TransportSink {
 public:
  MediaTcpIO(std::shared_ptr<Transport> s);
  ~MediaTcpIO() override;

  void Open(IMediaIOSink*) override;
  void Close() override;

  std::string GetLocalAddress() const override;
  std::string GetRemoteAddress() const override;

  srs_error_t Write(MessageChain* data, int* sent) override;

  void SetRecvTimeout(uint32_t);
  void SetSendTimeout(uint32_t);

  // TransporSink implement
  void OnRead(MessageChain &aData) override;
  void OnWrite() override;
  void OnClose(srs_error_t reason) override;
 private:
  std::shared_ptr<Transport> sock_;
  IMediaIOSink* sink_ = nullptr;
};

} //namespace ma

#endif //!__MEDIA_TRANSPORT_IMP_H__
