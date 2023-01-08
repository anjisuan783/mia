//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#include "media_rtmp_stack.h"

#include <unistd.h>
#include <stdlib.h>

#include "common/media_log.h"
#include "common/media_message.h"
#include "utils/media_msg_chain.h"
#include "utils/media_kernel_buffer.h"
#include "rtmp/media_amf0.h"
#include "rtmp/media_rtmp_const.h"
#include "rtmp/media_rtmp_msg.h"
#include "rtc_base/location.h"
#include "rtc_base/thread.h"
#include "rtmp/media_rtmp_handshake.h"
#include "rtmp/media_req.h"
#include "utils/media_protocol_utility.h"

namespace ma {

// The onStatus consts.
#define StatusLevel                             "level"
#define StatusCode                              "code"
#define StatusDescription                       "description"
#define StatusDetails                           "details"
#define StatusClientId                          "clientid"
// The status value
#define StatusLevelStatus                       "status"
// The status error
#define StatusLevelError                        "error"
// The code value
#define StatusCodeConnectSuccess                "NetConnection.Connect.Success"
#define StatusCodeConnectRejected               "NetConnection.Connect.Rejected"
#define StatusCodeStreamReset                   "NetStream.Play.Reset"
#define StatusCodeStreamStart                   "NetStream.Play.Start"
#define StatusCodeStreamPause                   "NetStream.Pause.Notify"
#define StatusCodeStreamUnpause                 "NetStream.Unpause.Notify"
#define StatusCodePublishStart                  "NetStream.Publish.Start"
#define StatusCodeDataStart                     "NetStream.Data.Start"
#define StatusCodeUnpublishSuccess              "NetStream.Unpublish.Success"

// The signature for packets to client.
#define RTMP_SIG_AMF0_VER                       0
#define RTMP_SIG_CLIENT_ID                      "ASAICiss"

// FMLE
#define RTMP_AMF0_COMMAND_ON_FC_PUBLISH         "onFCPublish"
#define RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH       "onFCUnpublish"

std::string RtmpClientTypeString(RtmpConnType type) {
  switch (type) {
    case RtmpConnPlay: return "rtmp-play";
    //case RtcConnPlay: return "rtc-play";
    case RtmpConnFlashPublish: return "flash-publish";
    case RtmpConnFMLEPublish: return "fmle-publish";
    case RtmpConnHaivisionPublish: return "haivision-publish";
    //case RtcConnPublish: return "rtc-publish";
    default: return "Unknown";
  }
}

bool RtmpClientTypeIsPublish(RtmpConnType type) {
  return (type != RtmpConnPlay);
}

////////////////////////////////////////////////////////////////////////////////
//RtmpServerSide
////////////////////////////////////////////////////////////////////////////////
MDEFINE_LOGGER(RtmpServerSide, "ma.rtmp");

RtmpServerSide::RtmpServerSide(RtmpStackSink* sink)
    : sink_(sink) {
}

RtmpServerSide::~RtmpServerSide() {
}

uint32_t RtmpServerSide::ProxyRealIp() {
  return handshake_->ProxyRealIp();
}

srs_error_t RtmpServerSide::Handshake(std::shared_ptr<IMediaIO> io) {
  srs_error_t err = srs_success;
  handshake_.reset(new MediaRtmpHandshakeS);
  
  if ((err = handshake_->Start(io)) != srs_success) {
    return srs_error_wrap(err, "server handshake start");
    return err;
  }

  handshake_->SignalHandshakeDone_.connect(this, &RtmpServerSide::HandshakeOk);
  handshake_->SignalHandshakefailed_.connect(this, &RtmpServerSide::HandshakeFailed);

  return err;
}

srs_error_t RtmpServerSide::ResponseConnect(
    MediaRequest *req, const char* server_ip) {
  srs_error_t err = srs_success;
  RtmpConnectAppResPacket pkt;

  // @remark For windows, 
  // there must be a space between const string and macro.
  pkt.props->set("fmsVer", RtmpAmf0Any::str("FMS/" RTMP_SIG_FMS_VER));
  pkt.props->set("capabilities", RtmpAmf0Any::number(127));
  pkt.props->set("mode", RtmpAmf0Any::number(1));
  
  pkt.info->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
  pkt.info->set(StatusCode, RtmpAmf0Any::str(StatusCodeConnectSuccess));
  pkt.info->set(StatusDescription, RtmpAmf0Any::str("Connection succeeded"));
  pkt.info->set("objectEncoding", RtmpAmf0Any::number(req->objectEncoding));
  RtmpAmf0EcmaArray* data = RtmpAmf0Any::ecma_array();
  pkt.info->set("data", data);
  
  data->set("version", RtmpAmf0Any::str(RTMP_SIG_FMS_VER));
  data->set("mia_sig", RtmpAmf0Any::str(RTMP_SIG_KEY));
  data->set("mia_server", RtmpAmf0Any::str(RTMP_SIG_SERVER));
  data->set("mia_license", RtmpAmf0Any::str(RTMP_SIG_LICENSE));
  data->set("mia_version", RtmpAmf0Any::str(RTMP_SIG_VERSION));
  data->set("mia_authors", RtmpAmf0Any::str(RTMP_SIG_SRS_AUTHORS));

  state_ = RTMP_CONNECT_DONE;

  if (server_ip) {
    data->set("srs_server_ip", RtmpAmf0Any::str(server_ip));
  }
  
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send connect app response");
  }
  
  return err;
}

void RtmpServerSide::HandshakeOk(uint32_t real_ip, 
    MessageChain* app_data, std::shared_ptr<RtmpBufferIO> sender) {
  state_ = RTMP_HANDSHAKE_DONE;
  real_ip_ = real_ip;
  protocol_.reset(new RtmpProtocal);
  protocol_->Open(sender, this);

  if (app_data)
    protocol_->OnRead(app_data);

  sender_ = std::move(sender);
  handshake_->SignalHandshakeDone_.disconnect(this);
  handshake_->SignalHandshakefailed_.disconnect(this);
  handshake_.reset(nullptr);
}

void RtmpServerSide::HandshakeFailed(srs_error_t err) {
  handshake_->SignalHandshakeDone_.disconnect(this);
  handshake_->SignalHandshakefailed_.disconnect(this);
  handshake_.reset(nullptr);
}

srs_error_t RtmpServerSide::OnPacket(std::shared_ptr<MediaMessage> msg) {
  if(RTMP_DISCONNECTED == state_)
    return srs_error_new(ERROR_INVALID_STATE, "RtmpServerSide state:%d", state_);

  srs_error_t err = srs_success;

  if (RTMP_PUBLISHING == state_) {
    err = sink_->OnMessage(std::move(msg));
    return err;
  }

  RtmpPacket* packet = nullptr;

  if (srs_success != (err = protocol_->DecodeMessage(msg.get(), packet))) {
    return err;
  }

  std::unique_ptr<RtmpPacket> packetGuard(packet);

  if (packet->get_message_type() != RTMP_MSG_AMF0CommandMessage) {
    return srs_error_new(ERROR_RTMP_AMF0_INVALID, 
        "invalid command:%d", packet->get_message_type());
  }

  if (RTMP_HANDSHAKE_DONE == state_) {
    err = OnConnectApp(packet);
  } else if (RTMP_CONNECT_DONE == state_) {
    MessageHeader& h = msg->header_;
    if (h.is_ackledgement() || 
        h.is_set_chunk_size() || 
        h.is_window_ackledgement_size() || 
        h.is_user_control_message()) {
      return err;
    }

    if (!h.is_amf0_command() && !h.is_amf3_command()) {
      MLOG_CINFO("ignore message type=%#x", h.message_type);
      return err;
    }

    int stream_id = 0;
    RtmpConnType type = RtmpConnUnknown;
    std::string stream_name;
    srs_utime_t duration;
    err = IdentifyClient(packet, stream_id, type, stream_name, duration);
    if (srs_success != err) {
      return err;
    }

    if (RtmpConnUnknown != type) {
      err = sink_->OnClientInfo(type, stream_name, duration);
    }
  } else if (RTMP_PUBLISHING_PENDING == state_) {
     err = ProcessPushingPending(packet);
  } else if (RTMP_REDIRECTING == state_) {
    err = sink_->OnRedirect(true);
  }
  return err;
}

srs_error_t RtmpServerSide::SetWinAckSize(int ack_size) {
  srs_error_t err = srs_success;
  RtmpSetWindowAckSizePacket pkt;
  pkt.ackowledgement_window_size = ack_size;
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send ack");
  }
  return err;
}

srs_error_t RtmpServerSide::SetInWinAckSize(int ack_size) {
  return protocol_->SetInWinAckSize(ack_size);
}

srs_error_t RtmpServerSide::SetPeerBandwidth(int bandwidth, int type) {
  srs_error_t err = srs_success;
  RtmpSetPeerBandwidthPacket pkt;
  pkt.bandwidth = bandwidth;
  pkt.type = type;
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send set peer bandwidth");
  }
  return err;
}

#define SRS_RTMP_REDIRECT_TIMEOUT (3 * SRS_UTIME_SECONDS)
srs_error_t RtmpServerSide::Redirect(MediaRequest* r, std::string url) {
  srs_error_t err = srs_success;
  
  RtmpAmf0Object* ex = RtmpAmf0Any::object();
  ex->set("code", RtmpAmf0Any::number(302));

  // The redirect is tcUrl while redirect2 is RTMP URL.
  std::string tcUrl = srs_path_dirname(url);
  ex->set("redirect", RtmpAmf0Any::str(tcUrl.c_str()));
  ex->set("redirect2", RtmpAmf0Any::str(url.c_str()));

  RtmpOnStatusCallPacket pkt;
  pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelError));
  pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeConnectRejected));
  pkt.data->set(StatusDescription, RtmpAmf0Any::str("RTMP 302 Redirect"));
  pkt.data->set("ex", ex);
  
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send redirect/reject");
  }

  // client must response a call message.
  // or we never know whether the client is ok to redirect.
  state_ = RTMP_REDIRECTING;
  rtc::Thread::Current()->PostDelayTask(RTC_FROM_HERE, 
      [weak_ptr=weak_from_this(), this]() {
        auto self = weak_ptr.lock();
        if (!self) return ;

        if (RTMP_REDIRECTING == state_)
          self->sink_->OnRedirect(false);
  }, SRS_RTMP_REDIRECT_TIMEOUT);

  return err;
}

void RtmpServerSide::ResponseConnectReject(
    MediaRequest* /*req*/, const char* desc) {
  srs_error_t err = srs_success;
  
  RtmpOnStatusCallPacket pkt;
  pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelError));
  pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeConnectRejected));
  pkt.data->set(StatusDescription, RtmpAmf0Any::str(desc));
  
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    MLOG_CWARN("send reject response err %s", srs_error_desc(err).c_str());
    delete err;
  }
}

srs_error_t RtmpServerSide::OnBwDone() {
  srs_error_t err = srs_success;
  RtmpOnBWDonePacket pkt;
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send onBWDone");
  }
  return err;
}

srs_error_t RtmpServerSide::SetChunkSize(int chunk_size) {
  srs_error_t err = srs_success;
  
  RtmpSetChunkSizePacket pkt;
  pkt.chunk_size = chunk_size;
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send set chunk size");
  }
  
  return err;
}

srs_error_t RtmpServerSide::StartPlay(int stream_id) {
  srs_error_t err = srs_success;
  stream_id_ = stream_id;
  // StreamBegin
  if (true) {
    RtmpUserControlPacket pkt;
    pkt.event_type = SrcPCUCStreamBegin;
    pkt.event_data = stream_id;
    if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
      return srs_error_wrap(err, "send StreamBegin");
    }
  }
  
  // onStatus(NetStream.Play.Reset)
  if (true) {
    RtmpOnStatusCallPacket pkt;
    pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
    pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeStreamReset));
    pkt.data->set(StatusDescription, 
        RtmpAmf0Any::str("Playing and resetting stream."));
    pkt.data->set(StatusDetails, RtmpAmf0Any::str("stream"));
    pkt.data->set(StatusClientId, RtmpAmf0Any::str(RTMP_SIG_CLIENT_ID));
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send NetStream.Play.Reset");
    }
  }
  
  // onStatus(NetStream.Play.Start)
  if (true) {
    RtmpOnStatusCallPacket pkt;
    pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
    pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeStreamStart));
    pkt.data->set(StatusDescription, 
        RtmpAmf0Any::str("Started playing stream."));
    pkt.data->set(StatusDetails, RtmpAmf0Any::str("stream"));
    pkt.data->set(StatusClientId, RtmpAmf0Any::str(RTMP_SIG_CLIENT_ID));
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send NetStream.Play.Start");
    }
  }
  
  // |RtmpSampleAccess(false, false)
  if (true) {
    RtmpSampleAccessPacket pkt;
    // allow audio/video sample.
    // @see: https://github.com/ossrs/srs/issues/49
    pkt.audio_sample_access = true;
    pkt.video_sample_access = true;
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send |RtmpSampleAccess true");
    }
  }
  
  // onStatus(NetStream.Data.Start)
  if (true) {
    RtmpOnStatusDataPacket pkt;
    pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeDataStart));
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send NetStream.Data.Start");
    }
  }
  
  state_ = RTMP_PLAYING;

  return err;
}

srs_error_t RtmpServerSide::OnPlayClientPause(int stream_id, bool is_pause) {
  srs_error_t err = srs_success;
  
  if (is_pause) {
    // onStatus(NetStream.Pause.Notify)
    if (true) {
      RtmpOnStatusCallPacket pkt;
      pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
      pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeStreamPause));
      pkt.data->set(StatusDescription, RtmpAmf0Any::str("Paused stream."));
      
      if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
        return srs_error_wrap(err, "send NetStream.Pause.Notify");
      }
    }
    // StreamEOF
    if (true) {
      RtmpUserControlPacket pkt;
      pkt.event_type = SrcPCUCStreamEOF;
      pkt.event_data = stream_id;
      if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
        return srs_error_wrap(err, "send StreamEOF");
      }
    }
    return err;
  } 

  // onStatus(NetStream.Unpause.Notify)
  if (true) {
    RtmpOnStatusCallPacket pkt;
    pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
    pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeStreamUnpause));
    pkt.data->set(StatusDescription, RtmpAmf0Any::str("Unpaused stream."));
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send NetStream.Unpause.Notify");
    }
  }
  // StreamBegin
  if (true) {
    RtmpUserControlPacket pkt;
    pkt.event_type = SrcPCUCStreamBegin;
    pkt.event_data = stream_id;
    if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
      return srs_error_wrap(err, "send StreamBegin");
    }
  }
  return err;
}

srs_error_t RtmpServerSide::StartFmlePublish(int stream_id) {
  srs_error_t err = srs_success;
  state_ = RTMP_PUBLISHING_PENDING;
  stream_id_ = stream_id;
  return err;
}

srs_error_t RtmpServerSide::StartHaivisionPublish(int stream_id) {
  srs_error_t err = srs_success;
  state_ = RTMP_PUBLISHING_PENDING;
  stream_id_ = stream_id;
  return err;
}

srs_error_t RtmpServerSide::StartFlashPublish(int stream_id) {
  srs_error_t err = srs_success;
  // publish response onStatus(NetStream.Publish.Start)
  RtmpOnStatusCallPacket pkt;
  pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
  pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodePublishStart));
  pkt.data->set(StatusDescription, 
      RtmpAmf0Any::str("Started publishing stream."));
  pkt.data->set(StatusClientId, RtmpAmf0Any::str(RTMP_SIG_CLIENT_ID));
  state_ = RTMP_PUBLISHING;
  if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
    return srs_error_wrap(err, "send NetStream.Publish.Start");
  }
  return err;
}

srs_error_t RtmpServerSide::ProcessPushingPending(RtmpPacket* packet) {
  srs_error_t err = srs_success;

  // FCPublish
  if (dynamic_cast<RtmpFMLEStartPacket*>(packet)) {
    double fc_publish_tid = 
        dynamic_cast<RtmpFMLEStartPacket*>(packet)->transaction_id;
    RtmpFMLEStartResPacket pkt(fc_publish_tid);
    if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
      return srs_error_wrap(err, "send FCPublish response");
    }
  }

  // createStream
  if (dynamic_cast<RtmpCreateStreamPacket*>(packet)) {
    double create_stream_tid = 
        dynamic_cast<RtmpCreateStreamPacket*>(packet)->transaction_id;
    RtmpCreateStreamResPacket pkt(create_stream_tid, stream_id_);
    if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
      return srs_error_wrap(err, "send createStream response");
    }
  }

  // publish
  if (dynamic_cast<RtmpPublishPacket*>(packet)) {
    // publish response onFCPublish(NetStream.Publish.Start)
    if (true) {
      RtmpOnStatusCallPacket pkt;
      pkt.command_name = RTMP_AMF0_COMMAND_ON_FC_PUBLISH;
      pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodePublishStart));
      pkt.data->set(StatusDescription, 
          RtmpAmf0Any::str("Started publishing stream."));
      if ((err = protocol_->Write(&pkt, stream_id_)) != srs_success) {
        return srs_error_wrap(err, "send NetStream.Publish.Start");
      }
    }
    
    // publish response onStatus(NetStream.Publish.Start)
    if (true) {
      RtmpOnStatusCallPacket pkt;
      pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
      pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodePublishStart));
      pkt.data->set(StatusDescription, 
          RtmpAmf0Any::str("Started publishing stream."));
      pkt.data->set(StatusClientId, RtmpAmf0Any::str(RTMP_SIG_CLIENT_ID));
      if ((err = protocol_->Write(&pkt, stream_id_)) != srs_success) { 
        return srs_error_wrap(err, "send NetStream.Publish.Start");
      }
    }
    state_ = RTMP_PUBLISHING;
  }

  return err;
}

srs_error_t RtmpServerSide::FmleUnpublish(int stream_id, double unpublish_tid) {
  srs_error_t err = srs_success;
  
  // publish response onFCUnpublish(NetStream.unpublish.Success)
  if (true) {
    RtmpOnStatusCallPacket pkt;  
    pkt.command_name = RTMP_AMF0_COMMAND_ON_FC_UNPUBLISH;
    pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeUnpublishSuccess));
    pkt.data->set(StatusDescription, 
        RtmpAmf0Any::str("Stop publishing stream."));
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send NetStream.unpublish.Success");
    }
  }
  // FCUnpublish response
  if (true) {
    RtmpFMLEStartResPacket pkt(unpublish_tid);
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send FCUnpublish response");
    }
  }
  // publish response onStatus(NetStream.Unpublish.Success)
  if (true) {
    RtmpOnStatusCallPacket pkt;
    pkt.data->set(StatusLevel, RtmpAmf0Any::str(StatusLevelStatus));
    pkt.data->set(StatusCode, RtmpAmf0Any::str(StatusCodeUnpublishSuccess));
    pkt.data->set(StatusDescription, 
        RtmpAmf0Any::str("Stream is now unpublished"));
    pkt.data->set(StatusClientId, RtmpAmf0Any::str(RTMP_SIG_CLIENT_ID));
    if ((err = protocol_->Write(&pkt, stream_id)) != srs_success) {
      return srs_error_wrap(err, "send NetStream.Unpublish.Success");
    }
  }
  return err;
}

srs_error_t RtmpServerSide::OnConnectApp(RtmpPacket* packet) {
  RtmpConnectAppPacket* pkt = dynamic_cast<RtmpConnectAppPacket*>(packet);

  RtmpAmf0Any* prop = pkt->command_object->ensure_property_string("tcUrl");
  if (nullptr == prop) {
    return srs_error_new(ERROR_RTMP_REQ_CONNECT, "invalid request without tcUrl");
  }

  auto req = std::make_shared<MediaRequest>();
  req->tcUrl = prop->to_str();
  
  prop = pkt->command_object->ensure_property_string("pageUrl");
  if (nullptr != prop) {
    req->pageUrl = prop->to_str();
  }
  
  prop = pkt->command_object->ensure_property_string("swfUrl");
  if (nullptr != prop) {
    req->swfUrl = prop->to_str();
  }
  
  prop = pkt->command_object->ensure_property_number("objectEncoding");
  if (nullptr != prop) {
    req->objectEncoding = prop->to_number();
  }
  
  if (pkt->args) {
    req->args = pkt->args->copy()->to_object();
  }
  
  srs_discovery_tc_url(req->tcUrl, req->schema, req->host, 
      req->vhost, req->app, req->stream, req->port, req->param);
  req->strip();
  return sink_->OnConnect(std::move(req));
}

srs_error_t RtmpServerSide::IdentifyClient(
    RtmpPacket* packet, int stream_id, RtmpConnType& type,
    std::string& stream_name, srs_utime_t& duration) {
  type = RtmpConnUnknown;
  srs_error_t err = srs_success;
  
  if (dynamic_cast<RtmpCreateStreamPacket*>(packet)) {
    return CreateStreamClient(
        dynamic_cast<RtmpCreateStreamPacket*>(packet), 
        stream_id, ++depth_, type, stream_name, duration);
  }
  if (dynamic_cast<RtmpFMLEStartPacket*>(packet)) {
    if (0 == depth_) {
      return FmlePublishClient(
        dynamic_cast<RtmpFMLEStartPacket*>(packet), type, stream_name);
    }
    return HaivisionPublishClient(
        dynamic_cast<RtmpFMLEStartPacket*>(packet), type, stream_name);
  }
  if (dynamic_cast<RtmpPlayPacket*>(packet)) {
    return PlayClient(
        dynamic_cast<RtmpPlayPacket*>(packet), type, stream_name, duration);
  }

  if (dynamic_cast<RtmpPublishPacket*>(packet)) {
    return FlashPublishClient(
        dynamic_cast<RtmpPublishPacket*>(packet), type, stream_name);
  }

  // call msg,
  // support response null first,
  // @see https://github.com/ossrs/srs/issues/106
  // TODO: FIXME: response in right way, or forward in edge mode.
  RtmpCallPacket* call = dynamic_cast<RtmpCallPacket*>(packet);
  if (call) {
    RtmpCallResPacket res(call->transaction_id);
    res.command_object = RtmpAmf0Any::null();
    res.response = RtmpAmf0Any::null();
    if ((err = protocol_->Write(&res, 0)) != srs_success) {
      return srs_error_wrap(err, "response call");
    }
    
    // For encoder of Haivision, it always send a _checkbw call message.
    // @remark the next message is createStream, 
    // so we continue to identify it.
    MA_ASSERT(call->command_name == "_checkbw");
  }
  MLOG_INFO("ignore AMF0/AMF3 command message.");

  return err;
}

srs_error_t RtmpServerSide::CreateStreamClient(
    RtmpCreateStreamPacket* req, int stream_id, int depth, 
    RtmpConnType& type, std::string& stream_name, srs_utime_t& duration) {
  srs_error_t err = srs_success;

  if (depth >= 3) {
    return srs_error_new(ERROR_RTMP_CREATE_STREAM_DEPTH, 
        "create stream too many times depth:%d", depth);
  }
  
  if (true) {
    RtmpCreateStreamResPacket pkt(req->transaction_id, stream_id);
    if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
      return srs_error_wrap(err, "send createStream response");
    }
  }
  return err;
}

srs_error_t RtmpServerSide::FmlePublishClient(RtmpFMLEStartPacket* req,
    RtmpConnType& type, std::string& stream_name) {
  srs_error_t err = srs_success;
  type = RtmpConnFMLEPublish;
  stream_name = req->stream_name;
  // releaseStream response
  RtmpFMLEStartResPacket pkt(req->transaction_id);
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send releaseStream response");
  }
  return err;
}

srs_error_t RtmpServerSide::HaivisionPublishClient(
    RtmpFMLEStartPacket* req, RtmpConnType& type, std::string& stream_name) {
  srs_error_t err = srs_success;
  type = RtmpConnHaivisionPublish;
  stream_name = req->stream_name;
  // FCPublish response
  RtmpFMLEStartResPacket pkt(req->transaction_id);
  if ((err = protocol_->Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send FCPublish");
  }
  return err;
}

srs_error_t RtmpServerSide::FlashPublishClient(RtmpPublishPacket* req, 
    RtmpConnType& type, std::string& stream_name) {
  type = RtmpConnFlashPublish;
  stream_name = req->stream_name;
  return srs_success;
}

srs_error_t RtmpServerSide::PlayClient(RtmpPlayPacket* req, 
    RtmpConnType& type, std::string& stream_name, srs_utime_t& duration) {
  type = RtmpConnPlay;
  stream_name = req->stream_name;
  duration = srs_utime_t(req->duration) * SRS_UTIME_MILLISECONDS;
  return srs_success;
}

} //namespace ma
