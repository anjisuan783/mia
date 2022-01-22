#include <cstdio>
#include <map>
#include <algorithm>
#include <string>
#include <cstring>
#include <vector>

#include "utils/Worker.h"
#include "utils/IOWorker.h"
#include "erizo/MediaStream.h"
#include "erizo/DtlsTransport.h"
#include "erizo/SdpInfo.h"
#include "erizo/rtp/RtpHeaders.h"
#include "erizo/rtp/RtcpForwarder.h"
#include "erizo/rtp/RtcpProcessorHandler.h"
#include "erizo/rtp/RtpUtils.h"
#include "erizo/rtp/RtpExtensionProcessor.h"
#include "erizo/SdpInfo.h"

namespace erizo {
DEFINE_LOGGER(WebRtcConnection, "WebRtcConnection");

WebRtcConnection::WebRtcConnection(wa::Worker* worker, 
    wa::IOWorker* io_worker,
    const std::string& connection_id, 
    const IceConfig& ice_config,
    const std::vector<RtpMap>& rtp_mappings,
    const std::vector<erizo::ExtMap>& ext_mappings, 
    WebRtcConnectionEventListener* listener) 
    : connection_id_{connection_id},
      conn_event_listener_{listener},
      ice_config_{ice_config}, 
      rtp_mappings_{rtp_mappings}, 
      extension_processor_{std::make_unique<RtpExtensionProcessor>(ext_mappings)},
      worker_{worker}, 
      io_worker_{io_worker},
      remote_sdp_{std::make_shared<SdpInfo>(rtp_mappings)}, 
      local_sdp_{std::make_shared<SdpInfo>(rtp_mappings)},
      stats_{std::make_shared<Stats>()} {
  ELOG_TRACE("%s message: ctor, stunserver: %s, stunPort: %d, minPort: %d, maxPort: %d",
      toLog(), ice_config.stun_server.c_str(), ice_config.stun_port, 
      ice_config.min_port, ice_config.max_port);
  trickle_enabled_ = ice_config_.should_trickle;
}

WebRtcConnection::~WebRtcConnection() {
  ELOG_TRACE("%s message: dtor called", toLog());
}

bool WebRtcConnection::init() {
  maybeNotifyWebRtcConnectionEvent(global_state_, "");
  return true;
}

void WebRtcConnection::close() {
  ELOG_TRACE("%s message: Close called", toLog());

  sending_ = false;
  for(auto& x : media_streams_) {
    x->close();
  }
  media_streams_.clear();
  if (video_transport_.get()) {
    video_transport_->close();
  }
  if (audio_transport_.get()) {
    audio_transport_->close();
  }
  global_state_ = CONN_FINISHED;
  if (conn_event_listener_ != nullptr) {
    conn_event_listener_ = nullptr;
  }

  ELOG_DEBUG("%s message: Close ended", toLog());
}

bool WebRtcConnection::createOffer(bool video_enabled, bool audioEnabled, bool bundle) {
  bundle_ = bundle;
  video_enabled_ = video_enabled;
  audio_enabled_ = audioEnabled;
  local_sdp_->createOfferSdp(video_enabled_, audio_enabled_, bundle_);
  local_sdp_->dtlsRole = ACTPASS;

  ELOG_DEBUG("%s message: Creating sdp offer, isBundle: %d", toLog(), bundle_);

  if (video_enabled_) {
    forEachMediaStream([this] (const std::shared_ptr<MediaStream> &media_stream) {
      std::vector<uint32_t> video_ssrc_list = std::vector<uint32_t>();
      video_ssrc_list.push_back(media_stream->getVideoSinkSSRC());
      local_sdp_->video_ssrc_map[media_stream->getLabel()] = video_ssrc_list;
    });
  }
  if (audio_enabled_) {
    forEachMediaStream([this] (const std::shared_ptr<MediaStream> &media_stream) {
      local_sdp_->audio_ssrc_map[media_stream->getLabel()] = media_stream->getAudioSinkSSRC();
    });
  }


  auto listener = std::dynamic_pointer_cast<TransportListener>(shared_from_this());

  if (bundle_) {
    video_transport_= std::make_shared<DtlsTransport>(
        VIDEO_TYPE, 
        "video", 
        connection_id_, 
        bundle_,
        true,
        listener, 
        ice_config_ , 
        "", 
        "", 
        true,
        worker_, 
        io_worker_);
    video_transport_->copyLogContextFrom(*this);
    video_transport_->start();
  } else {
    if (video_transport_.get() == nullptr && video_enabled_) {
      // For now we don't re/check transports, if they are already created we leave them there
      video_transport_= std::make_shared<DtlsTransport>(
          VIDEO_TYPE, 
          "video", 
          connection_id_, 
          bundle_, 
          true,
          listener, 
          ice_config_ , 
          "", 
          "", 
          true, 
          worker_, 
          io_worker_);
      video_transport_->copyLogContextFrom(*this);
      video_transport_->start();
    }
    
    if (audio_transport_.get() == nullptr && audio_enabled_) {
      audio_transport_ =std::make_shared<DtlsTransport>(
          AUDIO_TYPE, 
          "audio", 
          connection_id_, 
          bundle_,
          true,
          listener, 
          ice_config_,
          "",
          "",
          true,
          worker_,
          io_worker_);
      audio_transport_->copyLogContextFrom(*this);
      audio_transport_->start();
    }
  }

  std::string msg = this->getLocalSdp();
  maybeNotifyWebRtcConnectionEvent(global_state_, msg);

  return true;
}

void WebRtcConnection::addMediaStream(std::shared_ptr<MediaStream> media_stream) {
  ELOG_DEBUG("%s message: Adding mediaStream, id: %s", 
             toLog(), media_stream->getId().c_str());
  media_streams_.push_back(media_stream);
}

void WebRtcConnection::removeMediaStream(const std::string& stream_id) {
  ELOG_DEBUG("%s message: removing mediaStream, id: %s", toLog(), stream_id.c_str());
  media_streams_.erase(
  std::remove_if(media_streams_.begin(), media_streams_.end(),
      [stream_id, this](const std::shared_ptr<MediaStream> &stream) {
        bool isStream = stream->getId() == stream_id;
        if (isStream) {
          auto video_it = local_sdp_->video_ssrc_map.find(stream->getLabel());
          if (video_it != local_sdp_->video_ssrc_map.end()) {
            local_sdp_->video_ssrc_map.erase(video_it);
          }
          auto audio_it = local_sdp_->audio_ssrc_map.find(stream->getLabel());
          if (audio_it != local_sdp_->audio_ssrc_map.end()) {
            local_sdp_->audio_ssrc_map.erase(audio_it);
          }
        }
        return isStream;
      })
  );
}

void WebRtcConnection::forEachMediaStream(
    std::function<void(const std::shared_ptr<MediaStream>&)> func) {
  std::for_each(media_streams_.begin(), media_streams_.end(), func);
}

bool WebRtcConnection::setRemoteSdpInfo(std::shared_ptr<SdpInfo> sdp, std::string stream_id) {
  ELOG_DEBUG("%s message: setting remote SDPInfo", toLog());

  if (!sending_) {
    return false;
  }

  remote_sdp_ = sdp;
  processRemoteSdp(stream_id);

  return true;
}

std::shared_ptr<SdpInfo> WebRtcConnection::getLocalSdpInfo() {
  ELOG_DEBUG("%s message: getting local SDPInfo", toLog());
  forEachMediaStream([this] (const std::shared_ptr<MediaStream> &media_stream) {
    if (!media_stream->isRunning() || media_stream->isPublisher()) {
      ELOG_DEBUG("%s message: getting local SDPInfo stream not running, stream_id: %s", toLog(), media_stream->getId());
      return;
    }
    std::vector<uint32_t> video_ssrc_list = std::vector<uint32_t>();
    if (media_stream->getVideoSinkSSRC() != kDefaultVideoSinkSSRC && media_stream->getVideoSinkSSRC() != 0) {
      video_ssrc_list.push_back(media_stream->getVideoSinkSSRC());
    }
    ELOG_DEBUG("%s message: getting local SDPInfo, stream_id: %s, audio_ssrc: %u",
               toLog(), media_stream->getId(), media_stream->getAudioSinkSSRC());
    if (!video_ssrc_list.empty()) {
      local_sdp_->video_ssrc_map[media_stream->getLabel()] = video_ssrc_list;
    }
    if (media_stream->getAudioSinkSSRC() != kDefaultAudioSinkSSRC && media_stream->getAudioSinkSSRC() != 0) {
      local_sdp_->audio_ssrc_map[media_stream->getLabel()] = media_stream->getAudioSinkSSRC();
    }
  });

  bool sending_audio = local_sdp_->audio_ssrc_map.size() > 0;
  bool sending_video = local_sdp_->video_ssrc_map.size() > 0;

  bool receiving_audio = remote_sdp_->audio_ssrc_map.size() > 0;
  bool receiving_video = remote_sdp_->video_ssrc_map.size() > 0;

  if (!sending_audio && receiving_audio) {
    local_sdp_->audioDirection = erizo::RECVONLY;
  } else if (sending_audio && !receiving_audio) {
    local_sdp_->audioDirection = erizo::SENDONLY;
  } else {
    local_sdp_->audioDirection = erizo::SENDRECV;
  }

  if (!sending_video && receiving_video) {
    local_sdp_->videoDirection = erizo::RECVONLY;
  } else if (sending_video && !receiving_video) {
    local_sdp_->videoDirection = erizo::SENDONLY;
  } else {
    local_sdp_->videoDirection = erizo::SENDRECV;
  }

  return local_sdp_;
}

bool WebRtcConnection::setRemoteSdp(const std::string &sdp, const std::string& stream_id) {
  ELOG_DEBUG("%s message: setting remote SDP", toLog());
  if (!sending_) {
    return false;
  }

  remote_sdp_->initWithSdp(sdp, "");
  processRemoteSdp(stream_id);
  return true;
}

void WebRtcConnection::setRemoteSdpsToMediaStreams(const std::string& stream_id) {
  ELOG_DEBUG("%s message: setting remote SDP, stream: %s", toLog(), stream_id);

  auto stream = std::find_if(media_streams_.begin(), media_streams_.end(),
    [stream_id](const std::shared_ptr<MediaStream> &media_stream) {
      return media_stream->getId() == stream_id;
    });

  if (stream != media_streams_.end()) {
    (*stream)->setRemoteSdp(remote_sdp_);
    ELOG_DEBUG("%s message: setting remote SDP to stream, stream: %s", toLog(), stream_id);
    onRemoteSdpsSetToMediaStreams(stream_id);
  } else {
    onRemoteSdpsSetToMediaStreams(stream_id);
  }
}

void WebRtcConnection::onRemoteSdpsSetToMediaStreams(const std::string& stream_id) {
  ELOG_DEBUG("%s message: SDP processed", toLog());
  std::string sdp = getLocalSdp();
  maybeNotifyWebRtcConnectionEvent(CONN_SDP_PROCESSED, sdp, stream_id);
}

bool WebRtcConnection::processRemoteSdp(const std::string& stream_id) {
  ELOG_DEBUG("%s message: processing remote SDP remote dtlsRole %d", toLog(), remote_sdp_->dtlsRole);
  // update remote_sdp_'s ssrc map
  remote_sdp_->audio_ssrc_map = local_sdp_->audio_ssrc_map;
  remote_sdp_->video_ssrc_map = local_sdp_->video_ssrc_map;
  // Update extensions
  local_sdp_->setOfferSdp(remote_sdp_);
  extension_processor_->setSdpInfo(local_sdp_);
  local_sdp_->updateSupportedExtensionMap(extension_processor_->getSupportedExtensionMap());


  if (first_remote_sdp_processed_) {
    setRemoteSdpsToMediaStreams(stream_id);
    return true;
  }

  bundle_ = remote_sdp_->isBundle;

  if (remote_sdp_->dtlsRole == ACTPASS) {
    local_sdp_->dtlsRole = PASSIVE;
  }

  audio_enabled_ = remote_sdp_->hasAudio;
  video_enabled_ = remote_sdp_->hasVideo;

  if (remote_sdp_->profile == SAVPF) {
    if (remote_sdp_->isFingerprint) {
      auto listener = std::dynamic_pointer_cast<TransportListener>(shared_from_this());
      if (remote_sdp_->hasVideo || bundle_) {
        std::string username = remote_sdp_->getUsername(VIDEO_TYPE);
        std::string password = remote_sdp_->getPassword(VIDEO_TYPE);
        if (video_transport_.get() == nullptr) {
          ELOG_DEBUG("%s message: Creating videoTransport, ufrag: %s, pass: %s",
                      toLog(), username.c_str(), password.c_str());
          video_transport_ = 
              std::make_shared<DtlsTransport>(VIDEO_TYPE, 
                                              "video", 
                                              connection_id_, 
                                              bundle_, 
                                              remote_sdp_->isRtcpMux,
                                              listener, 
                                              ice_config_ , 
                                              username, 
                                              password, 
                                              true,
                                              worker_, 
                                              io_worker_);
          video_transport_->copyLogContextFrom(*this);
          video_transport_->start();
        } else {
          ELOG_DEBUG("%s message: Updating videoTransport, ufrag: %s, pass: %s",
                      toLog(), username.c_str(), password.c_str());
          video_transport_->getIceConnection()->setRemoteCredentials(username, password);
        }
      }
      if (!bundle_ && remote_sdp_->hasAudio) {
        std::string username = remote_sdp_->getUsername(AUDIO_TYPE);
        std::string password = remote_sdp_->getPassword(AUDIO_TYPE);
        if (audio_transport_.get() == nullptr) {
          ELOG_DEBUG("%s message: Creating audioTransport, ufrag: %s, pass: %s",
                      toLog(), username.c_str(), password.c_str());
          audio_transport_ = 
              std::make_shared<DtlsTransport>(AUDIO_TYPE, 
                                              "audio", 
                                              connection_id_, 
                                              bundle_, 
                                              remote_sdp_->isRtcpMux,
                                              listener, 
                                              ice_config_, 
                                              username, 
                                              password, 
                                              true,
                                              worker_, 
                                              io_worker_);
          audio_transport_->copyLogContextFrom(*this);
          audio_transport_->start();
        } else {
          ELOG_DEBUG("%s message: Update audioTransport, ufrag: %s, pass: %s",
                      toLog(), username.c_str(), password.c_str());
          audio_transport_->getIceConnection()->setRemoteCredentials(username, password);
        }
      }
    }
  }
  
  if (this->getCurrentState() >= CONN_GATHERED) {
    if (!remote_sdp_->getCandidateInfos().empty()) {
      ELOG_DEBUG("%s message: Setting remote candidates after gathered", toLog());
      if (remote_sdp_->hasVideo) {
        video_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
      }
      if (!bundle_ && remote_sdp_->hasAudio) {
        audio_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
      }
    }
  }
  setRemoteSdpsToMediaStreams(stream_id);
  first_remote_sdp_processed_ = true;
  return true;
}

bool WebRtcConnection::addRemoteCandidate(
    const std::string &mid, int mLineIndex, const std::string &sdp) {
  // TODO(pedro) Check type of transport.
  ELOG_DEBUG("%s message: Adding remote Candidate, candidate: %s, mid: %s, sdpMLine: %d",
              toLog(), sdp.c_str(), mid.c_str(), mLineIndex);
  if (video_transport_ == nullptr && audio_transport_ == nullptr) {
    ELOG_WARN("%s message: addRemoteCandidate on NULL transport", toLog());
    return false;
  }
  MediaType theType;
  std::string theMid;

  // TODO(pedro) check if this works with video+audio and no bundle
  if (mLineIndex == -1) {
    ELOG_DEBUG("%s message: All candidates received", toLog());
    if (video_transport_) {
      video_transport_->getIceConnection()->setReceivedLastCandidate(true);
    } else if (audio_transport_) {
      audio_transport_->getIceConnection()->setReceivedLastCandidate(true);
    }
    return true;
  }

  if ((!mid.compare("video")) || (mLineIndex == remote_sdp_->videoSdpMLine)) {
    theType = VIDEO_TYPE;
    theMid = "video";
  } else {
    theType = AUDIO_TYPE;
    theMid = "audio";
  }
  SdpInfo tempSdp(rtp_mappings_);
  std::string username = remote_sdp_->getUsername(theType);
  std::string password = remote_sdp_->getPassword(theType);
  tempSdp.setCredentials(username, password, OTHER);
  bool res = false;
  if (tempSdp.initWithSdp(sdp, theMid)) {
    if (theType == VIDEO_TYPE || bundle_) {
      res = video_transport_->setRemoteCandidates(tempSdp.getCandidateInfos(), bundle_);
    } else if (theType == AUDIO_TYPE) {
      res = audio_transport_->setRemoteCandidates(tempSdp.getCandidateInfos(), bundle_);
    } else {
      ELOG_ERROR("%s message: add remote candidate with no Media (video or audio), candidate: %s",
                  toLog(), sdp.c_str() );
    }
  }

  for (uint8_t it = 0; it < tempSdp.getCandidateInfos().size(); it++) {
    remote_sdp_->addCandidate(tempSdp.getCandidateInfos()[it]);
  }
  return res;
}

bool WebRtcConnection::removeRemoteCandidate(
    const std::string &mid, int mLineIndex, const std::string &sdp) {
  // TODO(pedro) Check type of transport.
  ELOG_DEBUG("%s message: Removing remote Candidate, candidate: %s, mid: %s, sdpMLine: %d",
              toLog(), sdp.c_str(), mid.c_str(), mLineIndex);
  if (video_transport_ == nullptr && audio_transport_ == nullptr) {
    ELOG_WARN("%s message: removeRemoteCandidate on NULL transport", toLog());
    return false;
  }

  if (mLineIndex == -1) {
    // End of removed candidates, retriger checks for candidates remained in remote_sdp_
    if (video_transport_) {
      video_transport_->removeRemoteCandidates();
      video_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
    }
    if (audio_transport_) {
      audio_transport_->removeRemoteCandidates();
      audio_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
    }
  } else {
    // Remove candidate in remote_sdp_, do not care about mediaType
    SdpInfo tempSdp(rtp_mappings_);
    if (tempSdp.initWithSdp(sdp, "whatever")) {
      std::vector<CandidateInfo>& rcands = remote_sdp_->getCandidateInfos();
      auto isRemovedCand = [&tempSdp](CandidateInfo& cand) -> bool {
        for (uint8_t it = 0; it < tempSdp.getCandidateInfos().size(); it++) {
          CandidateInfo& tempCand = tempSdp.getCandidateInfos()[it];
          if (tempCand.componentId == cand.componentId &&
              tempCand.netProtocol == cand.netProtocol &&
              tempCand.hostAddress == cand.hostAddress &&
              tempCand.hostPort == cand.hostPort &&
              tempCand.hostType == cand.hostType) {
            return true;
          }
        }
        return false;
      };
      rcands.erase(std::remove_if(rcands.begin(), rcands.end(), isRemovedCand), rcands.end());
    }
  }
  return true;
}

std::string WebRtcConnection::getLocalSdp() {
  ELOG_DEBUG("%s message: Getting Local Sdp", toLog());
  if (video_transport_ != nullptr && getCurrentState() != CONN_READY) {
    video_transport_->processLocalSdp(local_sdp_.get());
  }
  if (!bundle_ && audio_transport_ != nullptr && getCurrentState() != CONN_READY) {
    audio_transport_->processLocalSdp(local_sdp_.get());
  }
  local_sdp_->profile = remote_sdp_->profile;
  return local_sdp_->getSdp();
}

std::string WebRtcConnection::getJSONCandidate(const std::string& mid, const std::string& sdp) {
  std::map <std::string, std::string> object;
  object["sdpMid"] = mid;
  object["candidate"] = sdp;
  object["sdpMLineIndex"] =
  std::to_string((mid.compare("video")?local_sdp_->audioSdpMLine : local_sdp_->videoSdpMLine));

  std::ostringstream theString;
  theString << "{";
  for (std::map<std::string, std::string>::const_iterator it = object.begin(); 
      it != object.end(); ++it) {
    theString << "\"" << it->first << "\":\"" << it->second << "\"";
    if (++it != object.end()) {
      theString << ",";
    }
    --it;
  }
  theString << "}";
  return theString.str();
}

void WebRtcConnection::onCandidate(const CandidateInfo& cand, Transport *transport) {
  std::string sdp = local_sdp_->addCandidate(cand);
  ELOG_TRACE("%s message: Discovered New Candidate, candidate: %s", toLog(), sdp.c_str());
  if (!trickle_enabled_) {
    return;
  }
  
  if (!bundle_) {
    std::string object = this->getJSONCandidate(transport->transport_name, sdp);
    maybeNotifyWebRtcConnectionEvent(CONN_CANDIDATE, object);
    return;
  }
  
  if (remote_sdp_->hasAudio) {
    std::string object = this->getJSONCandidate("audio", sdp);
    maybeNotifyWebRtcConnectionEvent(CONN_CANDIDATE, object);
  }
  
  if (remote_sdp_->hasVideo) {
    std::string object2 = this->getJSONCandidate("video", sdp);
    maybeNotifyWebRtcConnectionEvent(CONN_CANDIDATE, object2);
  }
}

void WebRtcConnection::onREMBFromTransport(RtcpHeader *, Transport *) {
  //TODO not implement
}

void WebRtcConnection::onRtcpFromTransport(
    DataPacket* packet, Transport *transport) {
  RtpUtils::forEachRtcpBlock(packet, [this, packet, transport](RtcpHeader *chead) {
    uint32_t ssrc = chead->isFeedback() ? chead->getSourceSSRC() : chead->getSSRC();
    if (chead->isREMB()) {
      onREMBFromTransport(chead, transport);
      return;
    }
    auto new_rtcp = std::make_shared<DataPacket>(
        packet->comp, (const char*)chead, (ntohs(chead->length) + 1) * 4, 
        packet->type, packet->received_time_ms);
    
    forEachMediaStream([this, rtcp=std::move(new_rtcp), transport, ssrc] 
        (const std::shared_ptr<MediaStream> &media_stream) {
      if (media_stream->isSourceSSRC(ssrc) || media_stream->isSinkSSRC(ssrc)) {
        media_stream->onTransportData(std::move(rtcp), transport);
      }
    });
  });
}

void WebRtcConnection::onTransportData(
    std::shared_ptr<DataPacket> packet, Transport *transport) {
  if (getCurrentState() != CONN_READY) {
    return;
  }
  
  char* buf = packet->data;
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (buf);
  if (chead->isRtcp()) {
    onRtcpFromTransport(packet.get(), transport);
    return;
  }
  
  RtpHeader *head = reinterpret_cast<RtpHeader*> (buf);
  uint32_t ssrc = head->getSSRC();
  extension_processor_->processRtpExtensions(packet);
  const std::string& mid = this->extension_processor_->lastMid();
  const std::string& rid = this->extension_processor_->lastRid();
  
  if (!mid.empty()) {
    std::string streamId{mid};
    if (!rid.empty()) {
      // Compose ID as wa layer
      streamId = mid + ":" + rid;
    }

    if (mapping_ssrcs_.find(streamId) == mapping_ssrcs_.end()) {
      // Set SSRC for mid/rsid
      forEachMediaStream([this, streamId, ssrc] (const std::shared_ptr<MediaStream> &media_stream) {
        if (media_stream->getId() == streamId) {
          media_stream->setVideoSourceSSRC(ssrc);
          mapping_ssrcs_[streamId] = ssrc;
        }
      });
    }
  }

  forEachMediaStream([packet, transport, ssrc] (const std::shared_ptr<MediaStream> &media_stream) {
    if (media_stream->isSourceSSRC(ssrc) || media_stream->isSinkSSRC(ssrc)) {
      media_stream->onTransportData(std::move(packet), transport);
    }
  });
}

void WebRtcConnection::maybeNotifyWebRtcConnectionEvent(
    const WebRTCEvent& event, 
    const std::string& message,
    const std::string& stream_id) {
  if (!conn_event_listener_) {
      return;
  }
  conn_event_listener_->notifyEvent(event, message, stream_id);
}

void WebRtcConnection::updateState(TransportState state, Transport* transport)  {
  WebRTCEvent temp = global_state_;
  std::string msg = "";
  ELOG_TRACE("%s transportName: %s, new_state: %d", toLog(), transport->transport_name.c_str(), state);
  if (video_transport_.get() == nullptr && audio_transport_.get() == nullptr) {
    ELOG_ERROR("%s message: Updating NULL transport, state: %d", toLog(), state);
    return;
  }
  if (global_state_ == CONN_FAILED) {
    // if current state is failed -> noop
    return;
  }
  switch (state) {
    case TRANSPORT_STARTED:
      if (bundle_) {
        temp = CONN_STARTED;
      } else {
        if ((!remote_sdp_->hasAudio || (audio_transport_.get() != nullptr
                  && audio_transport_->getTransportState() == TRANSPORT_STARTED)) &&
            (!remote_sdp_->hasVideo || (video_transport_.get() != nullptr
                  && video_transport_->getTransportState() == TRANSPORT_STARTED))) {
            // WebRTCConnection will be ready only when all channels are ready.
            temp = CONN_STARTED;
          }
      }
      break;
    case TRANSPORT_GATHERED:
      if (bundle_) {
        if (!remote_sdp_->getCandidateInfos().empty()) {
          // Passing now new candidates that could not be passed before
          if (remote_sdp_->hasVideo) {
            video_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
          }
          if (!bundle_ && remote_sdp_->hasAudio) {
            audio_transport_->setRemoteCandidates(remote_sdp_->getCandidateInfos(), bundle_);
          }
        }
        if (!trickle_enabled_) {
          temp = CONN_GATHERED;
          msg = this->getLocalSdp();
        }
      } else {
        if ((!local_sdp_->hasAudio || (audio_transport_.get() != nullptr
                  && audio_transport_->getTransportState() == TRANSPORT_GATHERED)) &&
            (!local_sdp_->hasVideo || (video_transport_.get() != nullptr
                  && video_transport_->getTransportState() == TRANSPORT_GATHERED))) {
            // WebRTCConnection will be ready only when all channels are ready.
            if (!trickle_enabled_) {
              temp = CONN_GATHERED;
              msg = this->getLocalSdp();
            }
          }
      }
      break;
    case TRANSPORT_READY:
      if (bundle_) {
        temp = CONN_READY;
        trackTransportInfo();
        forEachMediaStream([] (const std::shared_ptr<MediaStream> &media_stream) {
          media_stream->sendPLIToFeedback();
        });
      } else {
        if ((!remote_sdp_->hasAudio || (audio_transport_.get() != nullptr
                  && audio_transport_->getTransportState() == TRANSPORT_READY)) &&
            (!remote_sdp_->hasVideo || (video_transport_.get() != nullptr
                  && video_transport_->getTransportState() == TRANSPORT_READY))) {
            // WebRTCConnection will be ready only when all channels are ready.
            temp = CONN_READY;
            trackTransportInfo();
            forEachMediaStream([] (const std::shared_ptr<MediaStream> &media_stream) {
              media_stream->sendPLIToFeedback();
            });
          }
      }
      break;
    case TRANSPORT_FAILED:
      temp = CONN_FAILED;
      sending_ = false;
      // TODO error description
      char buf[256];
      snprintf(buf, 256, 
          "%s message: Transport Failed, transportType:%s, code:%d", 
          toLog().c_str(), 
          transport->transport_name.c_str(), 
          transport->getErrorCode());
      msg = buf;
      break;
    default:
      ELOG_WARN("%s message: Doing nothing on state, state %d", toLog(), state);
      break;
  }

  if (audio_transport_.get() != nullptr && video_transport_.get() != nullptr) {
    ELOG_DEBUG("%s message: %s, transportName: %s, videoTransportState: %d"
               ", audioTransportState: %d, calculatedState: %d, globalState: %d",
               toLog(),
               "Update Transport State",
               transport->transport_name.c_str(),
               static_cast<int>(audio_transport_->getTransportState()),
               static_cast<int>(video_transport_->getTransportState()),
               static_cast<int>(temp),
               static_cast<int>(global_state_));
  }

  if (global_state_ == temp) {
    return;
  }

  global_state_ = temp;

  ELOG_INFO("%s newGlobalState: %d", toLog(), global_state_);
  maybeNotifyWebRtcConnectionEvent(global_state_, msg);
}

void WebRtcConnection::trackTransportInfo() {
  CandidatePair candidate_pair;
  std::string audio_info;
  std::string video_info;
  if (video_enabled_ && video_transport_) {
    candidate_pair = video_transport_->getIceConnection()->getSelectedPair();
    video_info = candidate_pair.clientHostType;
  }

  if (audio_enabled_ && audio_transport_) {
    candidate_pair = audio_transport_->getIceConnection()->getSelectedPair();
    audio_info = candidate_pair.clientHostType;
  }

  forEachMediaStream(
    [audio_info, video_info] (const std::shared_ptr<MediaStream> &media_stream) {
      media_stream->setTransportInfo(audio_info, video_info);
    });
}

void WebRtcConnection::setMetadata(std::map<std::string, std::string> metadata)  {
  setLogContext(metadata);
}

WebRTCEvent WebRtcConnection::getCurrentState() {
  return global_state_;
}

void WebRtcConnection::write(std::shared_ptr<DataPacket> packet) {
  if (!sending_) {
    return;
  }
  Transport *transport = 
      (bundle_ || packet->type == VIDEO_PACKET) ? video_transport_.get() 
                                                : audio_transport_.get();
  if (transport == nullptr) {
    return;
  }
  extension_processor_->processRtpExtensions(packet);
  transport->write(packet->data, packet->length);
}

// Only for Testing purposes
void WebRtcConnection::setTransport(std::shared_ptr<Transport> transport) {  
  video_transport_ = std::move(transport);
  bundle_ = true;
}

}  // namespace erizo

