#include "webrtc_track.h"

#include "wa_log.h"
#include "webrtc_agent_pc.h"
#include "sdp_processor.h"
#include "webrtc_agent_pc.h"

namespace wa {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("wa.track");

///////////////////////////////////////////////////////////////////////////////
//WebrtcTrackBase
///////////////////////////////////////////////////////////////////////////////
WebrtcTrackBase::WebrtcTrackBase(const std::string& mid, WrtcAgentPcBase* pc, 
    bool isPublish, const TrackSetting& setting, erizo::MediaStream* ms) 
    : mid_(mid),  pc_(pc), pc_id_(pc_->id()), name_(setting.is_audio?"audio":"video") {
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
      pc->setAudioSsrc(mid_, setting.ssrcs[0]);
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
      pc->setVideoSsrcList(mid_, setting.ssrcs);
    }
  }
}

void WebrtcTrackBase::close() {
  if (audioFrameConstructor_) {
    audioFrameConstructor_->close();
    audioFrameConstructor_ = nullptr;
  }

  if (videoFrameConstructor_) {
    videoFrameConstructor_->close();
    videoFrameConstructor_ = nullptr;
  }
}
///////////////////////////////////////////////////////////////////////////////
//WebrtcTrack
///////////////////////////////////////////////////////////////////////////////
WebrtcTrack::WebrtcTrack(const std::string& mid, 
                         WrtcAgentPcBase* pc,
                         bool isPublish,
                         const TrackSetting& setting,
                         erizo::MediaStream* ms,
                         int32_t request_kframe_s)
  : WebrtcTrackBase(mid, pc, isPublish, setting, ms), 
    request_kframe_period_(request_kframe_s) {
  if (!isPublish) {
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
    }
  }
  OLOG_TRACE_THIS(pc_->id() << ", track ctor mid:" << mid << "," << name_);
}

WebrtcTrack::~WebrtcTrack() {
  OLOG_TRACE_THIS(pc_->id() << ", track dtor mid:" << mid_ << "," << name_);
}

void WebrtcTrack::close() {
  if (audioFramePacketizer_) {
    audioFramePacketizer_->close();
    audioFramePacketizer_ = nullptr;
  }

  if (videoFramePacketizer_) {
    videoFramePacketizer_->close();
    videoFramePacketizer_ = nullptr;
  }

  WebrtcTrackBase::close();
}

uint32_t WebrtcTrack::ssrc(bool isAudio) {
  if (isAudio && audioFramePacketizer_) {
    return audioFramePacketizer_->getSsrc();
  } else if (!isAudio && videoFramePacketizer_) {
    return videoFramePacketizer_->getSsrc();
  }
  return 0;
}

void WebrtcTrack::addDestination(
    bool isAudio, std::shared_ptr<owt_base::FrameDestination> dest) {
  OLOG_TRACE_THIS(pc_->id() << "," << (isAudio?"a":"v") << ", dest:" << dest);
  if (isAudio && audioFrameConstructor_) {
    audioFrameConstructor_->addAudioDestination(std::move(dest));
  } else if (!isAudio && videoFrameConstructor_) {
    videoFrameConstructor_->addVideoDestination(std::move(dest));
  }
}

void WebrtcTrack::removeDestination(
    bool isAudio, owt_base::FrameDestination* dest) {
  OLOG_TRACE_THIS(pc_->id() << "," << (isAudio?"a":"v") << ", dest:" << dest);
  if (isAudio && audioFrameConstructor_) {
    audioFrameConstructor_->removeAudioDestination(dest);
  } else if (!isAudio && videoFrameConstructor_) {
    videoFrameConstructor_->removeVideoDestination(dest);
  } 
}

std::shared_ptr<owt_base::FrameDestination>
    WebrtcTrack::receiver(bool isAudio) {
  if (isAudio) {
    return audioFramePacketizer_;
  } 
  return videoFramePacketizer_;
}

srs_error_t WebrtcTrack::trackControl(
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

void WebrtcTrack::requestKeyFrame() {
  if (!videoFrameConstructor_) {
    return ;
  }

  videoFrameConstructor_->RequestKeyFrame();

  if (request_kframe_period_ != -1) {
    stop_request_kframe_period_ = false;
    dynamic_cast<WrtcAgentPc*>(pc_)->worker_->scheduleEvery(
        [weak_this = weak_from_this()]() {
          auto shared_this = weak_this.lock();
          if (shared_this && 
              !shared_this->stop_request_kframe_period_ && 
              shared_this->videoFrameConstructor_) {
            shared_this->videoFrameConstructor_->RequestKeyFrame();
            return true;
          }
          return false;
        }, std::chrono::seconds(request_kframe_period_));
  }
}

void WebrtcTrack::stopRequestKeyFrame() {
  stop_request_kframe_period_ = true;
}

///////////////////////////////////////////////////////////////////////////////
//WebrtcTrackDumy
///////////////////////////////////////////////////////////////////////////////
WebrtcTrackDumy::WebrtcTrackDumy(const std::string& mid, 
                                 WrtcAgentPcBase* pc,
                                 bool isPublish,
                                 const TrackSetting& setting)
  : WebrtcTrackBase(mid, pc, isPublish, setting, nullptr) { }

void WebrtcTrackDumy::close() {
  WebrtcTrackBase::close();
}

void WebrtcTrackDumy::addDestination(bool isAudio, 
    std::shared_ptr<owt_base::FrameDestination> dest) {
  if (isAudio) {
    addAudioDestination(std::move(dest));
  } else {
    addVideoDestination(std::move(dest));
  }
}

void WebrtcTrackDumy::removeDestination(
    bool isAudio, owt_base::FrameDestination* dest) {
  if (isAudio) {
    removeAudioDestination(dest);
  } else {
    removeVideoDestination(dest);
  }    
}

void WebrtcTrackDumy::onFrame(std::shared_ptr<owt_base::Frame> frm) {  
  deliverFrame(std::move(frm));
}

}

