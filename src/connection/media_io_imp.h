#ifndef __MEDIA_IO_IMP_H__
#define __MEDIA_IO_IMP_H__

#include "common/media_define.h"
#include "connection/h/media_io.h"
#include "rtc_base/async_packet_socket.h"

namespace ma {

class MediaTcpIO : public IMediaIO, public sigslot::has_slots<> {
 public:
  MediaTcpIO(std::unique_ptr<rtc::AsyncPacketSocket> s);
  ~MediaTcpIO() override;

  void Open(IMediaIOSink*) override;
  void Close() override;

  std::string GetLocalAddress() const override;
  std::string GetRemoteAddress() const override;

  srs_error_t Write(MessageChain* data, int* sent) override;

  void SetRecvTimeout(uint32_t);
  void SetSendTimeout(uint32_t);

  void OnReadEvent(rtc::AsyncPacketSocket*,
                  const char*,
                  size_t,
                  const rtc::SocketAddress&,
                  const int64_t&);
  void OnCloseEvent(rtc::AsyncPacketSocket* socket, int err);
  void OnWriteEvent(rtc::AsyncPacketSocket* socket);
 private:
  srs_error_t Write_i(const char* c_data, int c_size, int* sent);

  std::unique_ptr<rtc::AsyncPacketSocket> sock_;
  IMediaIOSink* sink_ = nullptr;

  static constexpr int kMaxPacketSize = MA_MAX_PACKET_SIZE;
};

} //namespace ma

#endif //!__MEDIA_TRANSPORT_IMP_H__
