#ifndef __MEDIA_RTMP_CONN_H__
#define __MEDIA_RTMP_CONN_H__

#include <memory>

#include "connection/h/conn_interface.h"
#include "rtmp/media_rtmp_stack.h"

namespace ma {

class IMediaHttpHandler;
class RtmpServerSide;
class MessageChain;
class IMediaIOFactory;
class IMediaIO;

class MediaRtmpConn final : public IMediaConnection,
                            public RtmpStackSink {
 public:
  MediaRtmpConn(std::unique_ptr<IMediaIOFactory> fac, 
                IMediaHttpHandler*);
  ~MediaRtmpConn() override;

  srs_error_t Start() override;
  void Disconnect() override;
  std::string Ip() override;

  // RtmpStackSink implement
  void OnConnect(std::shared_ptr<MediaRequest>, srs_error_t err) override;
  void OnClientInfo(RtmpConnType type, 
      std::string stream_name, srs_utime_t) override;
  void OnMessage(std::shared_ptr<MediaMessage>) override;
  void OnRedirect(bool accepted) override;

 private:
  std::shared_ptr<IMediaIO> io_;
  std::shared_ptr<RtmpServerSide> rtmp_;

  IMediaHttpHandler* handler_;
};

} //namespace ma

#endif //!__MEDIA_RTMP_CONN_H__
