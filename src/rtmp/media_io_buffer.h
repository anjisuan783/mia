#ifndef __MEDIA_RTMP_IO_BUFFER_H__
#define __MEDIA_RTMP_IO_BUFFER_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "utils/sigslot.h"

namespace ma {
class MessageChain;
class IMediaIO;

using MediaIOPtr = std::shared_ptr<IMediaIO>;

class RtmpBufferIOSink {
 public:
  virtual ~RtmpBufferIOSink() = default;
  virtual void OnRead(MessageChain*) = 0;
  virtual void OnWrite() = 0;
  virtual void OnDisc(int) = 0;
};

class RtmpBufferIO final : public sigslot::has_slots<> {
 public:
  RtmpBufferIO(MediaIOPtr io, RtmpBufferIOSink* sink);
  ~RtmpBufferIO();
  
  void SetSink(RtmpBufferIOSink*);

  // MediaIO callback
  void OnRead(MessageChain*);
  void OnWrite();
  void OnDisc(int);

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
