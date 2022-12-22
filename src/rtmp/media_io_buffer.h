#ifndef __MEDIA_RTMP_IO_BUFFER_H__
#define __MEDIA_RTMP_IO_BUFFER_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "connection/h/media_io.h"

namespace ma {
class MessageChain;
class IMediaIO;

using MediaIOPtr = std::shared_ptr<IMediaIO>;

class RtmpBufferIOSink {
 public:
  virtual ~RtmpBufferIOSink() = default;
  virtual srs_error_t OnRead(MessageChain*) = 0;
  virtual srs_error_t OnWrite() = 0;
  virtual void OnDisc(srs_error_t) = 0;
};

class RtmpBufferIO final : public IMediaIOSink {
 public:
  RtmpBufferIO(MediaIOPtr io, RtmpBufferIOSink* sink);
  ~RtmpBufferIO();
  
  void SetSink(RtmpBufferIOSink*);

  // MediaIO callback
  srs_error_t OnRead(MessageChain*) override;
  srs_error_t OnWrite() override;
  void OnDisconnect(srs_error_t) override;

  srs_error_t Write(MessageChain*);
  int64_t GetRecvBytes() {
    return nrecv_;
  }
 private:
  srs_error_t TrySend();

 private:
  RtmpBufferIOSink* sink_ = nullptr;
  MediaIOPtr io_;
  MessageChain* write_buffer_ = nullptr;

  int64_t nrecv_ = 0;
};

}

#endif //!__MEDIA_RTMP_IO_BUFFER_H__
