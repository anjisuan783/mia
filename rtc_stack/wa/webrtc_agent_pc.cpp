//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "webrtc_agent_pc.h"

#include <atomic>

#include "./wa_log.h"
#include "media_config.h"
#include "h/rtc_return_value.h"
#include "sdp_processor.h"

namespace wa {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("wa.pc");

/////////////////////////////
//WebrtcTrack
WrtcAgentPc::WebrtcTrack::WebrtcTrack(const std::string& mid, 
                                      WrtcAgentPc* pc, 
                                      bool isPublish, 
                                      const media_setting& setting,
                                      erizo::MediaStream* ms,
                                      int32_t request_kframe_s)
  : pc_(pc), mid_(mid), request_kframe_period_(request_kframe_s) {
  if (isPublish) {
    if (setting.is_audio) {
      audioFormat_ = setting.format;
      owt_base::AudioFrameConstructor::config config;
      config.ssrc = setting.ssrcs[0];
      config.rtcp_rsize = setting.rtcp_rsize;
      config.rtp_payload_type = setting.format;
      config.transportcc = setting.transportcc;
      config.factory = pc->adapter_factory_.get();
      audioFrameConstructor_ = 
          std::move(std::make_shared<owt_base::AudioFrameConstructor>(config));
      audioFrameConstructor_->bindTransport(
          dynamic_cast<erizo::MediaSource*>(ms),
          dynamic_cast<erizo::FeedbackSink*>(ms));
      pc_->setAudioSsrc(mid_, setting.ssrcs[0]);
      name_ = "audio";
    } else {
      videoFormat_ = setting.format;
      owt_base::VideoFrameConstructor::config config;
      config.ssrc = setting.ssrcs[0];
      config.rtx_ssrc = setting.ssrcs[1];
      config.rtcp_rsize = setting.rtcp_rsize;
      config.rtp_payload_type = setting.format;
      config.ulpfec_payload = setting.ulpfec?setting.ulpfec:-1;
      config.flex_fec = setting.flexfec;
      config.transportcc = setting.transportcc;
      config.red_payload = setting.red?setting.red:-1;
      config.worker = pc->worker_.get();
      config.factory = pc->adapter_factory_.get();
      
      videoFrameConstructor_ = 
          std::move(std::make_shared<owt_base::VideoFrameConstructor>(
              pc, config));
      videoFrameConstructor_->bindTransport(
          dynamic_cast<erizo::MediaSource*>(ms),
          dynamic_cast<erizo::FeedbackSink*>(ms));
      pc_->setVideoSsrcList(mid_, setting.ssrcs);
      name_ = "video";
    }
  } else {
    //subscribe
    if (setting.is_audio) {
      owt_base::AudioFramePacketizer::Config config;
      config.mid = setting.mid;
      config.midExtId = setting.mid_ext;
      config.factory = pc->adapter_factory_.get();
      config.task_queue = pc->worker_->getTaskQueue();
      audioFramePacketizer_ = 
          std::move(std::make_shared<owt_base::AudioFramePacketizer>(config));
      audioFramePacketizer_->bindTransport(dynamic_cast<erizo::MediaSink*>(ms));
      audioFormat_ = setting.format;
      name_ = "audio";
    } else {
      owt_base::VideoFramePacketizer::Config config;
      config.Red = setting.red, 
      config.Ulpfec = setting.ulpfec, 
      config.transportccExt = setting.transportcc?true:false,
      config.selfRequestKeyframe = true,
      config.mid = setting.mid,
      config.midExtId = setting.mid_ext;
      config.factory = pc->adapter_factory_.get();
      config.task_queue = pc->worker_->getTaskQueue();
      videoFramePacketizer_ =
          std::move(std::make_shared<owt_base::VideoFramePacketizer>(config));
      videoFramePacketizer_->bindTransport(dynamic_cast<erizo::MediaSink*>(ms));
      videoFormat_ = setting.format;
      name_ = "video";
    }
  }

  OLOG_TRACE_THIS(pc_->id_ << ",WebrtcTrack ctor mid:" << mid << "," << name_);
}

WrtcAgentPc::WebrtcTrack::~WebrtcTrack() {
  OLOG_TRACE_THIS(pc_->id_ << ",WebrtcTrack dtor mid:" << mid_ << "," << name_);
}

void WrtcAgentPc::WebrtcTrack::close() {
  if (audioFramePacketizer_) {
    audioFramePacketizer_->unbindTransport();
    audioFramePacketizer_ = nullptr;
  }

  if (audioFrameConstructor_) {
    audioFrameConstructor_->unbindTransport();
    audioFrameConstructor_ = nullptr;
  }

  if (videoFramePacketizer_) {
    videoFramePacketizer_->unbindTransport();
    videoFramePacketizer_ = nullptr;
  }

  if (videoFrameConstructor_) {
    videoFrameConstructor_->unbindTransport();
    videoFrameConstructor_ = nullptr;
  }
}

uint32_t WrtcAgentPc::WebrtcTrack::ssrc(bool isAudio) {
  if (isAudio && audioFramePacketizer_) {
    return audioFramePacketizer_->getSsrc();
  } else if (!isAudio && videoFramePacketizer_) {
    return videoFramePacketizer_->getSsrc();
  }
  return 0;
}

void WrtcAgentPc::WebrtcTrack::addDestination(
    bool isAudio, std::shared_ptr<owt_base::FrameDestination> dest) {
  OLOG_TRACE_THIS(pc_->id_ << "," << (isAudio?"a":"v") << ", dest:" << dest);
  if (isAudio && audioFrameConstructor_) {
    audioFrameConstructor_->addAudioDestination(std::move(dest));
  } else if (!isAudio && videoFrameConstructor_) {
    videoFrameConstructor_->addVideoDestination(std::move(dest));
    if (request_kframe_period_ != -1) {
      pc_->worker_->scheduleEvery([weak_this = weak_from_this()]() {
          auto shared_this = weak_this.lock();
          if (shared_this) {
            shared_this->requestKeyFrame();
            return true;
          }
          return false;
        }, std::chrono::seconds(request_kframe_period_));
    }
  }
}

void WrtcAgentPc::WebrtcTrack::removeDestination(
    bool isAudio, owt_base::FrameDestination* dest) {
  OLOG_TRACE_THIS(pc_->id_ << "," << (isAudio?"a":"v") << ", dest:" << dest);
  if (isAudio && audioFrameConstructor_) {
    audioFrameConstructor_->removeAudioDestination(dest);
  } else if (!isAudio && videoFrameConstructor_) {
    videoFrameConstructor_->removeVideoDestination(dest);
  } 
}

std::shared_ptr<owt_base::FrameDestination>
    WrtcAgentPc::WebrtcTrack::receiver(bool isAudio) {
  if (isAudio) {
    return audioFramePacketizer_;
  } 
  return videoFramePacketizer_;
}

srs_error_t WrtcAgentPc::WebrtcTrack::trackControl(
    ETrackCtrl track, bool isIn, bool isOn) {
  bool trackUpdate = false;
  if (track == e_av || track == e_audio) {
    if (isIn && audioFrameConstructor_) {
      audioFrameConstructor_->enable(isOn);
      trackUpdate = true;
    }
    if (!isIn && audioFramePacketizer_) {
      audioFramePacketizer_->enable(isOn);
      trackUpdate = true;
    }
  }
  
  if (track == e_av || track == e_video) {
    if (isIn && videoFrameConstructor_) {
      videoFrameConstructor_->enable(isOn);
      trackUpdate = true;
    }
    if (!isIn && videoFramePacketizer_) {
      videoFramePacketizer_->enable(isOn);
      trackUpdate = true;
    }
  }
  srs_error_t result = srs_success;
  
  if (!trackUpdate) {
    result = srs_error_wrap(result, "No track found");
  }

  return result;
}

void WrtcAgentPc::WebrtcTrack::requestKeyFrame() { 
  if (videoFrameConstructor_) {
    videoFrameConstructor_->RequestKeyFrame();
  }
}

/////////////////////////////
//WrtcAgentPc
WrtcAgentPc::WrtcAgentPc(const TOption& config, WebrtcAgent& mgr)
  : config_(config), 
    id_(config.connectId_), 
    mgr_(mgr),
    sink_(std::move(config_.call_back_)) {
  OLOG_TRACE_THIS(id_ << ", ctor");
}

WrtcAgentPc::~WrtcAgentPc() {
  this->close_i();
  if(remote_sdp_)
    delete remote_sdp_;
  if(local_sdp_)
    delete local_sdp_;
    
  OLOG_TRACE_THIS(id_ << ", dtor");
}

int WrtcAgentPc::init(std::shared_ptr<Worker>& worker, 
                      std::shared_ptr<IOWorker>& ioworker, 
                      const std::vector<std::string>& ipAddresses,
                      const std::string& stun_addr) {
  worker_ = worker;
  ioworker_ = ioworker;

  adapter_factory_ = std::move(std::make_unique<rtc_adapter::RtcAdapterFactory>(
      worker->getTaskQueue()));

  asyncTask([ipAddresses, stun_addr](std::shared_ptr<WrtcAgentPc> pc){
    pc->init_i(ipAddresses, stun_addr);
  });
  return wa_ok;
}

void WrtcAgentPc::init_i(const std::vector<std::string>& ipAddresses, 
                         const std::string&) {
  erizo::IceConfig ice_config;
  ice_config.ip_addresses = ipAddresses;
  
  std::vector<erizo::RtpMap> rtp_mappings{rtpH264, rtpRed, rtpOpus};
  
  std::vector<erizo::ExtMap> ext_mappings;

  size_t extMappings_size = extMappings.size();
  for(size_t i = 0; i < extMappings_size; ++i) {
    ext_mappings.emplace_back(i, extMappings[i]);
  }
  
  connection_ = 
      std::make_shared<erizo::WebRtcConnection>(worker_.get(), 
                                                ioworker_.get(),
                                                id_, 
                                                ice_config, 
                                                rtp_mappings, 
                                                ext_mappings, 
                                                this);
  connection_->init();
}

void WrtcAgentPc::close_i() {
  std::for_each(track_map_.begin(), track_map_.end(), 
      [](auto& i) {
        i.second->close();
      });

  track_map_.clear();
  
  if(connection_) {
    connection_->close();
    connection_ = nullptr;
  }
}

void WrtcAgentPc::close() {
  std::shared_ptr<WebrtcAgentSink> nul_sink;
  std::atomic_store<WebrtcAgentSink>(&sink_, nul_sink);
  
  worker_->task([shared_this = shared_from_this()] {
    shared_this->close_i();
  });
}

srs_error_t WrtcAgentPc::addTrackOperation(const std::string& mid, 
                                           EMediaType type, 
                                           const std::string& direction, 
                                           const FormatPreference& prefer,
                                           int32_t kframe_s) {
  srs_error_t ret = srs_success;

  operation op;
  op.type_ = type;
  op.sdp_direction_ = direction;
  op.format_preference_ = prefer;
  op.enabled_ = true;
  op.request_keyframe_second_ = kframe_s;
  
  auto result = operation_map_.emplace(mid, op);
  if (!result.second) {
    ret = srs_error_new(wa_e_found, 
                        "%s has mapped operation %s", 
                        mid.c_str(), 
                        result.first->second.operation_id_.c_str());
  }
  return ret;
}

void WrtcAgentPc::signalling(const std::string& signal, 
                             const std::string& content) {
  asyncTask([this, signal, content](std::shared_ptr<WrtcAgentPc> this_ptr) {
    srs_error_t result = srs_success;  
    if (signal == "offer") {
      result = this_ptr->processOffer(content, config_.stream_name_);
    } else if (signal == "candidate") {
      result = this_ptr->addRemoteCandidate(content);
    } else if (signal == "removed-candidates") {
      result = this_ptr->removeRemoteCandidates(content);
    }
    if (result != srs_success) {
      WLOG_ERROR("process %s error, desc:%s",
                 signal.c_str(),  
                 srs_error_desc(result).c_str());
      delete result;
    }
  });
}

void WrtcAgentPc::notifyEvent(erizo::WebRTCEvent newStatus, 
                              const std::string& message, 
                              const std::string& stream_id) {
  WLOG_TRACE("%s, update status:%d", id_.c_str(), newStatus);
  connection_state_ = newStatus;
  switch(newStatus) {
    case erizo::CONN_GATHERED:
      processSendAnswer(stream_id, message);
      break;

    case erizo::CONN_CANDIDATE:
      callBack(E_CANDIDATE, message);
      WLOG_DEBUG("%s, CONN_CANDIDATE, c:%s", id_.c_str(), message.c_str());
      break;

    case erizo::CONN_FAILED:
      WLOG_DEBUG("%s, CONN_FAILED, msg:%s", id_.c_str(), message.c_str());
      callBack(E_FAILED, message);
      break;

    case erizo::CONN_READY:
      WLOG_DEBUG("%s, CONN_READY", id_.c_str());
      if (!ready_) {
        ready_ = true;
        callBack(E_READY, "");
      }
      break;
    case erizo::CONN_INITIAL:
    case erizo::CONN_STARTED:
    case erizo::CONN_SDP_PROCESSED:
    case erizo::CONN_FINISHED:
      break;
  }
}

void WrtcAgentPc::processSendAnswer(const std::string& streamId, 
                                    const std::string& sdpMsg) {
  WLOG_TRACE("%s, processSendAnswer streamId:%s", id_.c_str(),streamId.c_str());
  LOG_ASSERT(sdpMsg.length());
  
  if(!sdpMsg.empty()) {
    // First answer from native
    try{
      WaSdpInfo tempSdp(sdpMsg);

      if (!tempSdp.empty()) {
        local_sdp_->session_name_ = "wa/0.1(anjisuan783@sina.com)";
        local_sdp_->setCredentials(tempSdp);
        local_sdp_->setCandidates(tempSdp); 
        local_sdp_->ice_lite_ = true;
      } else {
        WLOG_ERROR("%s, No mid in answer: streamId:%s, sdp:%s", 
                   id_.c_str(), streamId.c_str(), sdpMsg.c_str());
      }
    }
    catch(std::exception& ex){
      OLOG_ERROR(id_ << ", process sdp exception catched :" << ex.what());
      return;
    }
  }
  std::string answerSdp = local_sdp_->toString();

  callBack(E_ANSWER, answerSdp);
}

using namespace erizo;

srs_error_t WrtcAgentPc::processOffer(const std::string& sdp, 
                                      const std::string& stream_id) {
  srs_error_t result = srs_success;
  if (!remote_sdp_) {
    // First offer
    try{
      remote_sdp_ = new WaSdpInfo(sdp);
    }
    catch(std::exception& ex){
      delete remote_sdp_;
      remote_sdp_ = nullptr;
      return srs_error_new(wa::wa_e_parse_offer_failed, 
          "parse remote sdp failed");
    }
 
    // Check mid
    for (auto& mid : remote_sdp_->media_descs_) {
      for (auto& i : config_.tracks_) {
        if (i.type_ == media_audio && mid.type_ == "audio") {
          addTrackOperation(mid.mid_, media_audio, i.direction_, 
              i.preference_, i.request_keyframe_period_);
          break;
        } else if (i.type_ == media_video && mid.type_ == "video") {
          addTrackOperation(mid.mid_, media_video, i.direction_, 
              i.preference_, i.request_keyframe_period_);
          break;
        }
      }
    
      if((result = processOfferMedia(mid)) != srs_success){
        return srs_error_wrap(result, "processOfferMedia failed");
      }
    }

    try{
      local_sdp_ = remote_sdp_->answer();
      if (!stream_id.empty()) {
        local_sdp_->SetMsid(stream_id);
      }
    }
    catch(std::exception& ex){
      delete local_sdp_;
      local_sdp_ = nullptr;
      return srs_error_new(wa::wa_e_parse_offer_failed, 
          "parse locad sdp from remote failed");
    }
    
    for(auto& x : operation_map_){
      local_sdp_->filterByPayload(
          x.first, x.second.final_format_, true, false, true);
    }

    local_sdp_->filterExtmap();
    
    // Setup transport
    for (auto& mid : local_sdp_->media_descs_) {
      if (mid.port_ != 0 && (result = setupTransport(mid)) != srs_success) {
        return srs_error_wrap(result, "setupTransport failed");
      }
    }

  } else {
    // TODO: Later offer not implement
  }
  
  return result;
}

srs_error_t WrtcAgentPc::addRemoteCandidate(const std::string& candidates) {
  srs_error_t result = srs_success;
  return result;
}

srs_error_t WrtcAgentPc::removeRemoteCandidates(const std::string& candidates) {
  srs_error_t result = srs_success;
  return result;
}

srs_error_t WrtcAgentPc::processOfferMedia(MediaDesc& media) {
  OLOG_TRACE_THIS(id_ << ", t:" << media.type_ << ", mid:" << media.mid_);
  // Check Media MID with saved operation
  auto found = operation_map_.find(media.mid_);
  if (found == operation_map_.end()) {
    media.port_ = 0;
    return srs_error_new(wa_e_found, "%s has mapped operation %s", 
        media.mid_.c_str(), found->second.operation_id_.c_str());
  }

  const std::string& mid = media.mid_;
  operation& op = found->second;
  
  if (op.sdp_direction_ != media.direction_) {
    return srs_error_new(wa_failed, 
        "mid[%s] in offer has conflict direction with opid[%s],opd[%s]!=md[%s]", 
        mid.c_str(), op.operation_id_.c_str(), op.sdp_direction_.c_str(), 
        media.direction_.c_str());
  }

  std::string media_type{"unknow"};
  if(op.type_ == EMediaType::media_audio){
    media_type = "audio";
  }
  else if(op.type_ == EMediaType::media_video){
   media_type = "video";
  }
  
  if (media_type != media.type_) {
    return srs_error_new(wa_failed, 
        "%s in offer has conflict media type with %s", 
        mid.c_str(), op.operation_id_.c_str());
  }
  
  if (op.enabled_ && (media.port_ == 0)) {
    WLOG_WARNING("%s, %s in offer has conflict port with operation %s disabled", 
                 id_.c_str(), mid.c_str(), op.operation_id_.c_str());
    op.enabled_ = false;
  }

  // Determine media format in offer
  op.final_format_ = remote_sdp_->filterMediaPayload(mid, op.format_preference_);

  return srs_success;
}

srs_error_t WrtcAgentPc::setupTransport(MediaDesc& media) {
  OLOG_TRACE_THIS(id_ << ", t:" << media.type_ << ", mid:" << media.mid_);
  srs_error_t result = srs_success;
  
  auto op_found = operation_map_.find(media.mid_);
  operation& opSettings = op_found->second;

  auto& rids = media.rids_;
  std::string direction = 
      (opSettings.sdp_direction_ == "sendonly") ? "in" : "out";
  media_setting trackSetting = media.get_media_settings();
  
  //if (opSettings.final_format_) {
  //  trackSetting.format = opSettings.final_format_;
  //}

  if(rids.empty()) {
    // No simulcast    
    auto track_found = track_map_.find(media.mid_);
    if(track_found == track_map_.end()){
      WebrtcTrack* track = addTrack(media.mid_, 
                                    trackSetting, 
                                    (direction=="in"?true:false), 
                                    opSettings.request_keyframe_second_);
     
      uint32_t ssrc = track->ssrc(trackSetting.is_audio);
      if(ssrc){
        ELOG_INFO("%s, Add ssrc %u to %s in SDP for %s", 
                  id_.c_str(), ssrc, media.mid_.c_str(), id_.c_str());
        
        const std::string& opId = opSettings.operation_id_;
        auto msid_found = msid_map_.find(opId);
        if (msid_found != msid_map_.end()){
          media.setSsrcs(std::vector<uint32_t>{ssrc,}, msid_found->second);
        } else {
          std::string msid = media.setSsrcs(std::vector<uint32_t>{ssrc,}, "");
          msid_map_.insert(std::make_pair(opId, std::move(msid)));
        }
      }
      
      if (direction == "in") {
        // TODO choise
        track->addDestination(trackSetting.is_audio, 
            std::move(std::dynamic_pointer_cast<owt_base::FrameDestination>(
                shared_from_this())));
      }
      connection_->setRemoteSdp(
          remote_sdp_->singleMediaSdp(media.mid_), media.mid_);
    } else {
      result = srs_error_new(wa_e_found, "Conflict trackId %s with %s", 
                             media.mid_.c_str(), id_.c_str());
    }
  } else {
#if 0
    // Simulcast
    rids.forEach((rid, index) => {
      const trackId = composeId(mid, rid);        
      if (simSsrcs) {
        // Assign ssrcs for legacy simulcast
        trackSettings[mediaType].ssrc = simSsrcs[index];
      }

      if (!trackMap.has(trackId)) {
        trackMap.set(trackId, new WrtcStream(trackId, wrtc, direction, trackSettings));
        wrtc.setRemoteSdp(remoteSdp.singleMediaSdp(mid).toString(), trackId);
        // Notify new track
        on_track({
          type: 'track-added',
          track: trackMap.get(trackId),
          operationId: opSettings.operationId,
          mid: mid,
          rid: rid
        });
      } else {
        log.warn(`Conflict trackId ${trackId} for ${wrtcId}`);
      }
    });
#endif
  }

  return result;
}

void WrtcAgentPc::Subscribe(std::shared_ptr<WrtcAgentPc> subscriber) {
  OLOG_TRACE_THIS("s:" << id_ << " d:" << subscriber->id());
  subscribe_i(std::move(subscriber), true);
}

void WrtcAgentPc::unSubscribe(std::shared_ptr<WrtcAgentPc> subscriber) {
  OLOG_TRACE_THIS("s:" << id_ << " d:" << subscriber->id());
  subscribe_i(std::move(subscriber), false);
}

void WrtcAgentPc::subscribe_i(
    std::shared_ptr<WrtcAgentPc> sub_pc, bool isSub) {
  asyncTask([sub_pc, isSub] (std::shared_ptr<WrtcAgentPc> this_pc) {
    
    WebrtcTrack* dest_track = nullptr;
    WebrtcTrack* src_track = nullptr;
    bool isAudio = true;
    std::shared_ptr<owt_base::FrameDestination> receiver;
    auto& dest_tracks = sub_pc->getTracks();

    if (dest_tracks.empty()) {
      OLOG_ERROR("dest tracks empty! s:" << this_pc->id() << 
                 " d:" << sub_pc->id());
    }
    
    // add or remove FrameDestination by name
    for (auto& i : dest_tracks) {
      std::string track_name = i.second->getName();
      isAudio = i.second->isAudio();
      src_track = this_pc->getTrack(track_name);
      dest_track = sub_pc->getTrack(track_name);
      if (!src_track) {
        OLOG_ERROR(track_name << (isSub?" sub":" unsub") << 
                   ", src track not found! s:" << 
                   this_pc->id() << " d:" << sub_pc->id());
        continue;
      }
      
      if (!dest_track) {
        OLOG_ERROR(track_name << (isSub?" sub":" unsub") << 
                   ", dest track not found! s:" << 
                   this_pc->id() << " d:" << sub_pc->id());
        continue;
      }

      receiver = dest_track->receiver(isAudio);
      if (!receiver) {
        OLOG_ERROR(track_name << (isSub?" sub":" unsub") << 
                   ", dest track receiver not found! s:" << 
                   this_pc->id() << " d:" << sub_pc->id());
        continue;
      }
      
      if (isSub) {
        OLOG_INFO(track_name << " sub, s:" << 
                  this_pc->id() << " d:" << sub_pc->id());
        src_track->addDestination(isAudio, receiver);
      } else {
        OLOG_INFO(track_name << " unsub, s:" << 
                  this_pc->id() << " d:" << sub_pc->id());
        src_track->removeDestination(isAudio, receiver.get());
      }
    }
  });
}

WrtcAgentPc::WebrtcTrack* WrtcAgentPc::addTrack(
    const std::string& mid, const media_setting& trackSetting, 
    bool isPublish, int32_t kframe_s) {
  OLOG_TRACE_THIS(id_ << "," << (isPublish?"push": "sub") << 
                  ", mediaStreamId:" << mid);
  WebrtcTrack* result = nullptr;

  auto found = track_map_.find(mid);
  if (track_map_.end() != found) {
    result = found->second.get();
  } else {
    auto ms = std::make_shared<MediaStream>(
        worker_.get(), connection_, mid, mid, isPublish);
    
    connection_->addMediaStream(ms);

    auto newTrack = std::make_shared<WebrtcTrack>(
        mid, this, isPublish, trackSetting, ms.get(), kframe_s);

    result = newTrack.get();
    track_map_.insert(std::make_pair(mid, std::move(newTrack)));
  }
  return result;
}

srs_error_t WrtcAgentPc::removeTrack(const std::string& mid) {
  OLOG_TRACE_THIS(id_ << ", mediaStreamId:%s" << mid);

  srs_error_t result = nullptr;
  
  auto found = track_map_.find(mid);

  if (track_map_.end() == found) {
    return srs_error_wrap(result, "not found mediaStreamId:%s", mid.c_str());
  }
  
  connection_->removeMediaStream(mid);
  found->second->close();
  track_map_.erase(found);

  return result;
}

WrtcAgentPc::WebrtcTrack* WrtcAgentPc::getTrack(const std::string& name) {
  for (auto& item : track_map_) {
    if (item.second->getName() == name) {
      return item.second.get();
    }
  }
  return nullptr;
}

void WrtcAgentPc::setAudioSsrc(const std::string& mid, uint32_t ssrc) {
  connection_->getLocalSdpInfo()->audio_ssrc_map[mid] = ssrc;
}

void WrtcAgentPc::setVideoSsrcList(const std::string& mid, 
                                   std::vector<uint32_t> ssrc_list) {
  connection_->getLocalSdpInfo()->video_ssrc_map[mid] = ssrc_list;
}

void WrtcAgentPc::onFrame(const owt_base::Frame& f) {
  callBack(E_DATA, f);
}

void WrtcAgentPc::onVideoInfo(const std::string& videoInfoJSON) {
  OLOG_INFO_THIS(id_ << ", video info changed :" << videoInfoJSON);
}

void WrtcAgentPc::callBack(E_SINKID id, const std::string& message) {
  auto sink = std::atomic_load<WebrtcAgentSink>(&sink_);
  if (!sink) {
    return;
  }
  
  sink->callBack([id, message](std::shared_ptr<WebrtcAgentSink> pc_sink) {
    switch(id) {
      case E_CANDIDATE :
        pc_sink->onCandidate(message);
        break;
      case E_FAILED :
        pc_sink->onFailed(message);
        break;
      case E_READY :
        pc_sink->onReady();
        break;
      case E_ANSWER :
        pc_sink->onAnswer(message);
        break;
      case E_DATA:
        ;
    }
  });  
}

void WrtcAgentPc::callBack(E_SINKID, const owt_base::Frame& message) {
  auto sink = std::atomic_load<WebrtcAgentSink>(&sink_);
  if (!sink) {
    return;
  }

  sink->callBack([message](std::shared_ptr<WebrtcAgentSink> pc_sink){
    pc_sink->onFrame(message);
  });
}

void WrtcAgentPc::asyncTask(
    std::function<void(std::shared_ptr<WrtcAgentPc>)> f) {
  std::weak_ptr<WrtcAgentPc> weak_this = weak_from_this();
  worker_->task([weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

} //namespace wa

