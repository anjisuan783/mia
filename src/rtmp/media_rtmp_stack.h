//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_RTMP_STACK_H__
#define __MEDIA_RTMP_STACK_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "common/media_log.h"
#include "common/media_define.h"
#include "utils/sigslot.h"
#include "rtmp/media_rtmp_msg.h"

namespace ma {

class MediaMessage;
class RtmpPacket;
class RtmpCreateStreamPacket;
class RtmpFMLEStartPacket;
class RtmpPublishPacket;
class RtmpPlayPacket;
class MediaRtmpHandshake;
class RtmpProtocal;
class MediaRequest;
class IMediaIO;
class MessageChain;

// The rtmp client type.
enum RtmpConnType {
  RtmpConnUnknown,
  RtmpConnPlay,
  RtmpConnFMLEPublish,
  RtmpConnFlashPublish,
  RtmpConnHaivisionPublish,
};
std::string RtmpClientTypeString(RtmpConnType type);
bool RtmpClientTypeIsPublish(RtmpConnType type);

// The information return from RTMP server.
struct RtmpServerInfo {
  std::string ip;
  std::string sig;
  int pid = 0;
  int cid = 0;
  int major = 0;
  int minor = 0;
  int revision = 0;
  int build = 0;
};

class RtmpStackSink {
 public:
  virtual ~RtmpStackSink() = default;

  virtual srs_error_t OnConnect(std::shared_ptr<MediaRequest>) = 0;
  virtual srs_error_t OnClientInfo(RtmpConnType type, 
      std::string stream_name, srs_utime_t) = 0;
  virtual srs_error_t OnMessage(std::shared_ptr<MediaMessage>) = 0;
  virtual srs_error_t OnRedirect(bool accepted) = 0;
};

class RtmpBufferIO;

// The rtmp provices rtmp-command-protocol services,
// a high level protocol, media stream oriented services,
// such as connect to vhost/app, play stream, get audio/video data.
class RtmpServerSide final : 
    public std::enable_shared_from_this<RtmpServerSide>,
    public sigslot::has_slots<>,
    public RtmpProtocalSink {
  MDECLARE_LOGGER();
 public:
  RtmpServerSide(RtmpStackSink*);
  ~RtmpServerSide();

  // For RTMP proxy, the real IP. 0 if no proxy.
  // @doc https://github.com/ossrs/go-oryx/wiki/RtmpProxy
  uint32_t ProxyRealIp();
  // Protocol methods proxy

  // Do handshake with client, try complex then simple.
  srs_error_t Handshake(std::shared_ptr<IMediaIO>);
  // @param server_ip the ip of server.
  srs_error_t ResponseConnect(
      MediaRequest* req, const char* server_ip = nullptr);
  
  // Response  client the onBWDone message.
  srs_error_t OnBwDone();

  // packet entry
  srs_error_t OnPacket(std::shared_ptr<MediaMessage> pkt) override;

  // callback
  void HandshakeOk(uint32_t real_ip, 
                   MessageChain*, 
                   std::shared_ptr<RtmpBufferIO>);
  void HandshakeFailed(srs_error_t);
  
  // Set output ack size to client, client will send ack-size 
  //for each ack window
  srs_error_t SetWinAckSize(int ack_size);
  // Set the default input ack size value.
  srs_error_t SetInWinAckSize(int ack_size);
  // @type: The sender can mark this message hard (0), soft (1), or dynamic (2)
  // using the Limit type field.
  srs_error_t SetPeerBandwidth(int bandwidth, int type);

  // Redirect the connection to another rtmp server.
  // @param a RTMP url to redirect to.
  srs_error_t Redirect(MediaRequest* r, std::string url);
  // Reject the connect app request.
  void ResponseConnectReject(MediaRequest* req, const char* desc);
  
  // Set the chunk size when client type identified.
  srs_error_t SetChunkSize(int chunk_size);
  // When client type is play, response with packets:
  // StreamBegin,
  // onStatus(NetStream.Play.Reset), onStatus(NetStream.Play.Start).,
  // |RtmpSampleAccess(false, false),
  // onStatus(NetStream.Data.Start).
  srs_error_t StartPlay(int stream_id);
  // When client(type is play) send pause message,
  // if is_pause, response the following packets:
  //     onStatus(NetStream.Pause.Notify)
  //     StreamEOF
  // if not is_pause, response the following packets:
  //     onStatus(NetStream.Unpause.Notify)
  //     StreamBegin
  srs_error_t OnPlayClientPause(int stream_id, bool is_pause);
  // When client type is publish, response with packets:
  // releaseStream response
  // FCPublish
  // FCPublish response
  // createStream response
  // onFCPublish(NetStream.Publish.Start)
  // onStatus(NetStream.Publish.Start)
  srs_error_t StartFmlePublish(int stream_id);
  // For encoder of Haivision, response the startup request.
  // @see https://github.com/ossrs/srs/issues/844
  srs_error_t StartHaivisionPublish(int stream_id);
  // process the FMLE unpublish event.
  // @unpublish_tid the unpublish request transaction id.
  srs_error_t FmleUnpublish(int stream_id, double unpublish_tid);
  // When client type is publish, response with packets:
  // onStatus(NetStream.Publish.Start)
  srs_error_t StartFlashPublish(int stream_id);
 private:
  srs_error_t OnConnectApp(RtmpPacket* packet);

  // Recv some message to identify the client.
  // @stream_id, client will createStream to play or publish by flash,
  //         the stream_id used to response the createStream request.
  // @type, output the client type.
  // @stream_name, output the client publish/play stream name. 
  //   @see: MediaRequest.stream
  // @duration, output the play client duration. @see: MediaRequest.duration
  srs_error_t IdentifyClient(RtmpPacket* packet, int stream_id, 
      RtmpConnType& type, std::string& stream_name, srs_utime_t& duration);

  srs_error_t CreateStreamClient(RtmpCreateStreamPacket* req, 
      int stream_id, int depth, RtmpConnType& type, 
      std::string& stream_name, srs_utime_t& duration);
  srs_error_t FmlePublishClient(RtmpFMLEStartPacket* req,
      RtmpConnType& type, std::string& stream_name);
  srs_error_t HaivisionPublishClient(RtmpFMLEStartPacket* req,
      RtmpConnType& type, std::string& stream_name);
  srs_error_t FlashPublishClient(RtmpPublishPacket* req,
      RtmpConnType& type, std::string& stream_name);
  srs_error_t PlayClient(RtmpPlayPacket* req, RtmpConnType& type,
      std::string& stream_name, srs_utime_t& duration);

  srs_error_t ProcessPushingPending(RtmpPacket* packet);
  srs_error_t ProcessPushingData(RtmpPacket* packet);

 private:
  enum RtmpState{
    RTMP_INIT,
    RTMP_HANDSHAKE_DONE,
    RTMP_CONNECT_PENDING,
    RTMP_CONNECT_DONE,
    RTMP_PLAYING,
    RTMP_PUBLISHING_PENDING,
    RTMP_PUBLISHING,
    RTMP_REDIRECTING,
    RTMP_DISCONNECTED
  };
  RtmpState      state_ = RTMP_INIT;
  RtmpStackSink* sink_;
  std::unique_ptr<MediaRtmpHandshake> handshake_;
  std::unique_ptr<RtmpProtocal> protocol_;
  std::shared_ptr<RtmpBufferIO> sender_;
  uint32_t real_ip_ = 0;
  int depth_ = 0;  //For encoder of Haivision
  int32_t stream_id_ = 0;
};

} //namespace ma

#endif //!__MEDIA_RTMP_STACK_H__
