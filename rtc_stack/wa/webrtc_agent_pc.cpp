//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "webrtc_agent_pc.h"

#include <atomic>

#include "media_config.h"
#include "h/rtc_return_value.h"
#include "sdp_processor.h"
#include "erizo/MediaStream.h"
#include "webrtc_track.h"

namespace wa {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("wa.pc");

///////////////////////////////////////////////////////////////////////////////
//WebrtcTrackBase

///////////////////////////////////////////////////////////////////////////////
WebrtcTrackBase* WrtcAgentPcBase::getTrack(const std::string& name) {
  WebrtcTrackBase* result = nullptr;
  for (auto& item : track_map_) {
    if (item.second->getName() == name) {
      result = item.second.get();
      break;
    }
  }
  return result;
}

void WrtcAgentPcBase::onVideoInfo(const std::string& videoInfoJSON) {
  OLOG_INFO_THIS(id_ << ", video info changed :" << videoInfoJSON);
}

void WrtcAgentPcBase::subscribe_i(
    const WEBRTC_TRACK_TYPE& dest_tracks, bool isSub) {
  
  WebrtcTrackBase* src_track = nullptr;
  bool isAudio = true;
  std::shared_ptr<owt_base::FrameDestination> receiver;

  // add or remove FrameDestination by name
  for (auto& i : dest_tracks) {
    auto dest_track = i.lock();
    if (!dest_track) 
      continue;
    
    std::string track_name = dest_track->getName();
    isAudio = dest_track->isAudio();
    src_track = getTrack(track_name);
    if (!src_track) {
      OLOG_ERROR((isSub?"sub:":"unsub:") << track_name <<
                 ", src track not found! s:" << 
                 id() << " d:" << dest_track->pcId());
      continue;
    }

    receiver = dest_track->receiver(isAudio);
    if (!receiver) {
      OLOG_ERROR((isSub?"sub:":"unsub:") << track_name <<
                 ", dest track receiver not found! s:" << 
                 id() << ", d:" << dest_track->pcId());
      continue;
    }
    
    if (isSub) {
      OLOG_INFO("sub, s:" << track_name <<
                id() << ", d:" << dest_track->pcId());
      src_track->addDestination(isAudio, receiver);
    } else {
      OLOG_INFO("unsub, s:" << track_name <<
                id() << ", d:" << dest_track->pcId());
      src_track->removeDestination(isAudio, receiver.get());
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
//WrtcAgentPc
///////////////////////////////////////////////////////////////////////////////
WrtcAgentPc::WrtcAgentPc() { 
  thread_check_.Detach();
}

WrtcAgentPc::~WrtcAgentPc() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  OLOG_TRACE_THIS(id_ << ", dtor");
  this->close_i();
}

int WrtcAgentPc::init(TOption& config, 
                      WebrtcAgent&,
                      std::shared_ptr<Worker>& worker, 
                      std::shared_ptr<IOWorker>& ioworker, 
                      const std::vector<std::string>& ipAddresses,
                      const std::string& stun_addr) {

  id_ = config.connectId_;
  OLOG_TRACE_THIS(id_ << ", init");
  config_ = config;
  sink_ = std::move(config_.call_back_);
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

  RTC_DCHECK_RUN_ON(&thread_check_);
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
  RTC_DCHECK_RUN_ON(&thread_check_);

  std::for_each(track_map_.begin(), track_map_.end(), 
      [](auto& i) { i.second->close(); });

  track_map_.clear();
  
  if(connection_) {
    connection_->close();
    connection_ = nullptr;
  }

  adapter_factory_.reset(nullptr);

  if(remote_sdp_) {
    delete remote_sdp_;
    remote_sdp_ = nullptr;
  }
  if(local_sdp_) {
    delete local_sdp_;
    local_sdp_ = nullptr;
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
  op.mid_ = mid;
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
                        result.first->second.mid_.c_str());
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
  RTC_DCHECK_RUN_ON(&thread_check_);
  WLOG_TRACE("%s, update status:%d", id_.c_str(), newStatus);
  connection_state_ = newStatus;
  switch(newStatus) {
    case erizo::CONN_GATHERED:
      processSendAnswer(stream_id, message);
      break;
    
    case erizo::CONN_READY:
      WLOG_DEBUG("%s, CONN_READY", id_.c_str());
      if (!ready_) {
        ready_ = true;
        callBack(E_READY, "");
      }
      break;

    case erizo::CONN_CANDIDATE:
      callBack(E_CANDIDATE, message);
      WLOG_DEBUG("%s, CONN_CANDIDATE, c:%s", id_.c_str(), message.c_str());
      break;

    case erizo::CONN_INITIAL:
    case erizo::CONN_STARTED:
    case erizo::CONN_SDP_PROCESSED:
    case erizo::CONN_FINISHED:
      break;
    
    case erizo::CONN_FAILED:
      WLOG_DEBUG("%s, CONN_FAILED, msg:%s", id_.c_str(), message.c_str());
      callBack(E_FAILED, message);
      break;
  }
}

void WrtcAgentPc::processSendAnswer(const std::string& streamId, 
                                    const std::string& sdpMsg) {
  WLOG_TRACE("%s, processSendAnswer stream:%s", id_.c_str(),streamId.c_str());
  LOG_ASSERT(sdpMsg.length());
  
  if(!sdpMsg.empty()) {
    // First answer from native
    try{
      WaSdpInfo tempSdp(sdpMsg);

      if (!tempSdp.empty()) {
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
                                      const std::string& stream_name) {
  RTC_DCHECK_RUN_ON(&thread_check_);
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
      if (!stream_name.empty()) {
        local_sdp_->SetToken(stream_name);
      }
    }
    catch(std::exception& ex){
      delete local_sdp_;
      local_sdp_ = nullptr;
      return srs_error_new(wa::wa_e_parse_offer_failed, 
          "parse locad sdp from remote failed");
    }

    // negotiate sdp
    // TODO optimize code
    for(auto& x : operation_map_){
      local_sdp_->filterByPayload(
          x.first, x.second.final_format_, true, false, true);
    }
    local_sdp_->filterExtmap();

    // Setup transport
    for (auto& mid : local_sdp_->media_descs_) {
      bool bPublish;
      if (mid.port_ != 0 && 
          (result = setupTransport(mid, bPublish)) != srs_success) {
        return srs_error_wrap(result, "setupTransport failed");
      }

      // for firefoxM96 see@https://github.com/anjisuan783/mia/issues/34
      if (bPublish) 
        mid.clearSsrcInfo();
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
    return srs_error_new(wa_e_found, "mid[%s]:%s has mapped operation", 
        media.mid_.c_str(), media.type_.c_str());
  }

  const std::string& mid = media.mid_;
  operation& op = found->second;

  // process ambiguous direction
  if (media.direction_ == "sendrecv") {
    media.direction_ = op.sdp_direction_;
  }
  
  if (op.sdp_direction_ != media.direction_) {
    return srs_error_new(wa_failed,
        "mid[%s]:%s in offer has conflict direction with "
        "opid[%s],opd[%s]!=md[%s]", 
        mid.c_str(), media.type_.c_str(), 
        op.mid_.c_str(), op.sdp_direction_.c_str(), media.direction_.c_str());
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
        mid.c_str(), op.mid_.c_str());
  }
  
  if (op.enabled_ && (media.port_ == 0)) {
    WLOG_WARNING("%s, %s in offer has conflict port with operation %s disabled", 
                 id_.c_str(), mid.c_str(), op.mid_.c_str());
    op.enabled_ = false;
  }

  // Determine media format in offer
  op.final_format_ = remote_sdp_->filterMediaPayload(mid, op.format_preference_);

  return srs_success;
}

srs_error_t WrtcAgentPc::setupTransport(MediaDesc& media, bool& bPublish) {
  OLOG_TRACE_THIS(id_ << ", t:" << media.type_ << ", mid:" << media.mid_);
  srs_error_t result = srs_success;
  
  auto op_found = operation_map_.find(media.mid_);
  operation& opSettings = op_found->second;

  auto& rids = media.rids_;
  bPublish = (opSettings.sdp_direction_ == "sendonly");
  TrackSetting trackSetting = media.getTrackSettings();

  if (rids.empty()) {
    // No simulcast    
    auto track_found = track_map_.find(media.mid_);
    if (track_found == track_map_.end()) {
      WebrtcTrackBase* track = addTrack(media.mid_, 
                                    trackSetting, 
                                    bPublish, 
                                    opSettings.request_keyframe_second_);
      const std::string& opId = opSettings.mid_;
      std::string msid = media.msid_;
      
      if (!bPublish) {
        // setting ssrc info
        uint32_t ssrc = track->ssrc(trackSetting.is_audio);
        if (ssrc) {
          ELOG_INFO("%s, Add ssrc %u to %s in SDP for %s", 
                    id_.c_str(), ssrc, media.mid_.c_str(), id_.c_str());
          
          auto msid_found = msid_map_.find(opId);
          if (msid_found != msid_map_.end()) {
            media.setSsrcs(std::vector<uint32_t>{ssrc,}, msid_found->second);
          } else {
            msid = media.setSsrcs(std::vector<uint32_t>{ssrc,}, "");
          }
        } else {
          abort();
        }
      }

      msid_map_.emplace(opId, msid);
      
      connection_->setRemoteSdp(
          remote_sdp_->singleMediaSdp(media.mid_), media.mid_);
    } else {
      result = srs_error_new(wa_e_found, "Conflict trackId %s with %s", 
                             media.mid_.c_str(), id_.c_str());
    }
  } else {
    // TODO implement Simulcast
  }

  return result;
}

void WrtcAgentPc::Subscribe(const WEBRTC_TRACK_TYPE& tracks) {
  asyncTask([tracks](std::shared_ptr<WrtcAgentPc> this_pc) {
    this_pc->subscribe_i(tracks, true);
  });
}

void WrtcAgentPc::unSubscribe(const WEBRTC_TRACK_TYPE& tracks) {
  asyncTask([tracks](std::shared_ptr<WrtcAgentPc> this_pc) {
    this_pc->subscribe_i(tracks, false);
  });
}

void WrtcAgentPc::frameCallback(bool on) {
  asyncTask([on](std::shared_ptr<WrtcAgentPc> pc) {
      for (auto& i : pc->track_map_) {
        WebrtcTrackBase* track = i.second.get();
        bool isVideo = !track->isAudio();
        // callback frames
        if (on) {
          track->addDestination(!isVideo,
              std::move(
                  std::dynamic_pointer_cast<owt_base::FrameDestination>(pc)));
          if (isVideo) {
            track->requestKeyFrame();
          }
        } else {
          track->removeDestination(!isVideo, pc.get());
          if (isVideo) {
            track->stopRequestKeyFrame();
          }
        }
      }
    }
 );
}

WebrtcTrackBase* WrtcAgentPc::addTrack(
    const std::string& mid, const TrackSetting& trackSetting, 
    bool isPublish, int32_t kframe_s) {
  OLOG_TRACE_THIS(id_ << "," << (isPublish?"push": "play") << ", mid:" << mid);
  WebrtcTrackBase* result = nullptr;

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
    track_map_.emplace(mid, std::move(newTrack));
  }

  return result;
}

srs_error_t WrtcAgentPc::removeTrack(const std::string& mid) {
  OLOG_TRACE_THIS(id_ << ", mid:%s" << mid);

  srs_error_t result = nullptr;
  
  auto found = track_map_.find(mid);

  if (track_map_.end() == found) {
    return srs_error_wrap(result, "not found mid:%s", mid.c_str());
  }
  
  connection_->removeMediaStream(mid);
  found->second->close();
  track_map_.erase(found);

  return result;
}

void WrtcAgentPc::setAudioSsrc(const std::string& mid, uint32_t ssrc) {
  connection_->getLocalSdpInfo()->audio_ssrc_map[mid] = ssrc;
}

void WrtcAgentPc::setVideoSsrcList(const std::string& mid, 
                                   const std::vector<uint32_t>& ssrc_list) {
  connection_->getLocalSdpInfo()->video_ssrc_map[mid] = ssrc_list;
}

void WrtcAgentPc::onFrame(std::shared_ptr<owt_base::Frame> f) {
  auto sink = std::atomic_load<WebrtcAgentSink>(&sink_);
  if (!sink) {
    return;
  }

  sink->callBack([frame = std::move(f)](std::shared_ptr<WebrtcAgentSink> pc_sink){
    pc_sink->onFrame(std::move(frame));
  });
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

void WrtcAgentPc::asyncTask(
    std::function<void(std::shared_ptr<WrtcAgentPc>)> f) {
  worker_->task([weak_this = weak_from_this(), f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

///////////////////////////////////////////////////////////////////////////////
// WrtcAgentPcDummy 
///////////////////////////////////////////////////////////////////////////////
int WrtcAgentPcDummy::init(TOption& option, 
                           WebrtcAgent&,
                           std::shared_ptr<Worker>& worker, 
                           std::shared_ptr<IOWorker>&, 
                           const std::vector<std::string>&,
                           const std::string&) {
  id_ = option.connectId_;
  OLOG_TRACE_THIS(id_ << ", init");
  worker_ = worker;

  adapter_factory_ = std::move(std::make_unique<rtc_adapter::RtcAdapterFactory>(
      worker->getTaskQueue()));

  for (int i = 0; i < 2; ++i) {
    TrackSetting setting;
    setting.is_audio = (option.tracks_[i].type_ == media_audio);
    setting.format = 
        get_pt_by_preference(option.tracks_[i].preference_.format_);
    setting.ssrcs.push_back(123456);
    setting.mid = option.tracks_[i].mid_;
    auto track = std::make_shared<WebrtcTrackDumy>(
        setting.mid, this, true, setting);
    track_map_.emplace(setting.mid, track);
    tracks_[i] = track.get();
  }

  option.pc_ = std::dynamic_pointer_cast<RtcPeer>(shared_from_this());
  return wa_ok;
}

void WrtcAgentPcDummy::close_i() {
  adapter_factory_.reset(nullptr);
}

void WrtcAgentPcDummy::close() {
  worker_->task([shared_this = shared_from_this()] {
    shared_this->close_i();
  });
}

void WrtcAgentPcDummy::Subscribe(const WEBRTC_TRACK_TYPE& dest_tracks) {
  subscribe_i(dest_tracks, true);
}

void WrtcAgentPcDummy::unSubscribe(const WEBRTC_TRACK_TYPE& dest_tracks) {
  subscribe_i(dest_tracks, false);
}

void WrtcAgentPcDummy::DeliveryFrame(std::shared_ptr<owt_base::Frame> frm) {
  int i = 0;
  if (owt_base::isVideoFrame(*frm.get())) {
    i = 1;
  }

  tracks_[i]->onFrame(std::move(frm));
}

///////////////////////////////////////////////////////////////////////////////
// WrtcAgentPcDummy 
///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<WrtcAgentPcBase> WebrtcPeerFactory::CreatePeer(PeerType t) {
  if (peer_real == t) {
    return std::make_shared<WrtcAgentPc>();
  }
  return std::make_shared<WrtcAgentPcDummy>();
}

} //namespace wa

