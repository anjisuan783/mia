#ifndef __MEDIA_RTMP_HANDSHAKE_H__
#define __MEDIA_RTMP_HANDSHAKE_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "rtmp/media_rtmp_handshake.h"
#include "utils/sigslot.h"
#include "rtmp/media_io_buffer.h"

namespace ma {

class MessageChain;
class IMediaIO;
class HandshakeHelper;

class RtmpHandshakeStrategy {
 public:
  virtual ~RtmpHandshakeStrategy() = default;

  virtual srs_error_t ServerHandshakeWithClient(HandshakeHelper* helper,
      MessageChain* msg, RtmpBufferIO*) = 0;
  virtual srs_error_t OnClientAck(HandshakeHelper*, MessageChain* msg) = 0;

  virtual srs_error_t ClientHandshakeWithServer(HandshakeHelper* helper,
      RtmpBufferIO*) = 0;
  virtual srs_error_t OnServerAck(HandshakeHelper*,
      MessageChain*, RtmpBufferIO*) = 0;
};

class MediaRtmpHandshake : public RtmpBufferIOSink,
    public sigslot::has_slots<> {
 public:
  MediaRtmpHandshake();
  virtual ~MediaRtmpHandshake();

  virtual srs_error_t Start(std::shared_ptr<IMediaIO> io);
  void Close();
  uint32_t ProxyRealIp();
  
  void OnDisc(srs_error_t reason);

  sigslot::signal3<uint32_t, MessageChain*, std::shared_ptr<RtmpBufferIO>>
      SignalHandshakeDone_;
  sigslot::signal1<srs_error_t> SignalHandshakefailed_;
 protected:
  std::shared_ptr<RtmpBufferIO> sender_;
  MessageChain* read_buffer_ = nullptr;
  bool waiting_ack_ = false;
  std::unique_ptr<RtmpHandshakeStrategy> handshake_;
  std::unique_ptr<HandshakeHelper> helper_;
};

class MediaRtmpHandshakeS : public MediaRtmpHandshake {
 public:
  //RtmpBufferIOSink
  srs_error_t OnRead(MessageChain*) override;
};

class MediaRtmpHandshakeC : public MediaRtmpHandshake {
 public:
  srs_error_t Start(std::shared_ptr<IMediaIO> io) override;

  //RtmpBufferIOSink
  srs_error_t OnRead(MessageChain*) override;
};

} //namespace ma

#endif //!__MEDIA_RTMP_HANDSHAKE_H__
