#ifndef __MEDIA_RTMP_CONN_H__
#define __MEDIA_RTMP_CONN_H__

#include <memory>

#include "common/media_log.h"
#include "connection/h/conn_interface.h"
#include "rtmp/media_rtmp_stack.h"

namespace ma {

class IMediaHttpHandler;
class RtmpServerSide;
class MessageChain;
class IMediaIOFactory;
class IMediaIO;
class MediaResponse;

// client information
class ClientInfo {
 public:
  ClientInfo() = default;
  ~ClientInfo() = default;
 public:
  // The type of client, play or publish.
  RtmpConnType type = RtmpConnUnknown;

  // Original request object from client.
  std::shared_ptr<MediaRequest> req_;
  // Response object to client.
  int stream_id_ = 1;
};

class MediaRtmpConn final : public IMediaConnection,
                            public RtmpStackSink {
  MDECLARE_LOGGER();
 public:
  MediaRtmpConn(std::unique_ptr<IMediaIOFactory> fac, 
                IMediaHttpHandler*);
  ~MediaRtmpConn() override;

  srs_error_t Start() override;
  void Disconnect() override;
  std::string Ip() override;

  // RtmpStackSink implement
  srs_error_t OnConnect(std::shared_ptr<MediaRequest>) override;
  srs_error_t OnClientInfo(RtmpConnType type, 
      std::string stream_name, srs_utime_t) override;
  srs_error_t OnMessage(std::shared_ptr<MediaMessage>) override;
  srs_error_t OnRedirect(bool accepted) override;

 private:
  std::shared_ptr<IMediaIO> io_;
  std::shared_ptr<RtmpServerSide> rtmp_;

  IMediaHttpHandler* handler_;

  ClientInfo cli_info_;
};

} //namespace ma

#endif //!__MEDIA_RTMP_CONN_H__
