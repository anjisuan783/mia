#include <map>
#include <algorithm>
#include <string>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <ctime>

#include "erizo/MediaStream.h"
#include "erizo/SdpInfo.h"
#include "erizo/WebRtcConnection.h"
#include "erizo/rtp/RtpHeaders.h"
#include "erizo/rtp/RtcpForwarder.h"
#include "erizo/rtp/RtpUtils.h"
#include "erizo/WoogeenHandler.h"

namespace erizo {

//PacketReader
class PacketReader : public InboundHandler {
 public:
  explicit PacketReader(MediaStream *media_stream) 
    : media_stream_{media_stream} {}

  void enable() override {}
  void disable() override {}

  std::string getName() override {
    return "reader";
  }

  void read(Context *ctx, std::shared_ptr<DataPacket> packet) override {
    media_stream_->read(std::move(packet));
  }

  void notifyUpdate() override { }

 private:
  MediaStream *media_stream_;
};

//PacketWriter
class PacketWriter : public OutboundHandler {
 public:
  explicit PacketWriter(MediaStream *media_stream) 
    : media_stream_{media_stream} {}

  void enable() override {}
  void disable() override {}

  std::string getName() override {
    return "writer";
  }

  void write(Context *ctx, std::shared_ptr<DataPacket> packet) override {
    media_stream_->write(std::move(packet));
  }

  void notifyUpdate() override { }

 private:
  MediaStream *media_stream_;
};

//MediaStream
DEFINE_LOGGER(MediaStream, "MediaStream");

log4cxx::LoggerPtr MediaStream::statsLogger = 
    log4cxx::Logger::getLogger("StreamStats");

static constexpr auto kStreamStatsPeriod = std::chrono::seconds(30);

MediaStream::MediaStream(wa::Worker* worker,
    std::shared_ptr<WebRtcConnection> connection,
    const std::string& media_stream_id,
    const std::string& media_stream_label,
    bool is_publisher)
    : connection_{std::move(connection)},
      stream_id_{media_stream_id},
      mslabel_{media_stream_label},
      stats_{std::make_shared<Stats>()},
      log_stats_{std::make_shared<Stats>()},
      pipeline_{Pipeline::create()},
      worker_{worker},
      is_publisher_{is_publisher} {
  OLOG_TRACE_THIS(toLog());
  source_fb_sink_ = this;
  sink_fb_source_ = this;
  std::srand(std::time(nullptr));
  audio_sink_ssrc_ = std::rand();
  video_sink_ssrc_ = std::rand();
  mark_ = wa::clock::now();
}

MediaStream::~MediaStream() {
  OLOG_TRACE_THIS(toLog());
}

uint32_t MediaStream::getMaxVideoBW() {
  uint32_t bitrate = rtcp_processor_ ? rtcp_processor_->getMaxVideoBW() : 0;
  return bitrate;
}

void MediaStream::close()  {
  OLOG_TRACE_THIS("Close called" << toLog());
  if (!sending_) {
    return;
  }
  
  sending_ = false;
  video_sink_ = nullptr;
  audio_sink_ = nullptr;
  fb_sink_ = nullptr;
  pipeline_initialized_ = false;
  pipeline_->close();
  pipeline_.reset();
  connection_.reset();
}

bool MediaStream::init() {
  return true;
}

bool MediaStream::isSourceSSRC(uint32_t ssrc) {
  return isVideoSourceSSRC(ssrc) || isAudioSourceSSRC(ssrc);
}

bool MediaStream::isSinkSSRC(uint32_t ssrc) {
  return isVideoSinkSSRC(ssrc) || isAudioSinkSSRC(ssrc);
}

bool MediaStream::setRemoteSdp(std::shared_ptr<SdpInfo> sdp)  {
  if (!sending_) {
    return true;
  }
  if (!sdp) {
    ELOG_WARN("%s setting remote SDP nullptr", toLog());
    return true;
  }
  remote_sdp_ =  std::make_shared<SdpInfo>(*sdp.get());
  if (remote_sdp_->videoBandwidth != 0) {
    ELOG_DEBUG("%s Setting remote BW, maxVideoBW: %u", 
               toLog(), remote_sdp_->videoBandwidth);
    //this->rtcp_processor_->setMaxVideoBW(remote_sdp_->videoBandwidth*1000);
  }

  auto video_ssrc_list_it = remote_sdp_->video_ssrc_map.find(getLabel());
  if (video_ssrc_list_it != remote_sdp_->video_ssrc_map.end()) {
    std::ostringstream oss;
    for(auto i : video_ssrc_list_it->second){
      oss << " ssrc:" << i;
    }
    OLOG_DEBUG(toLog() << ", setVideoSourceSSRCList, " <<  oss.str());
    setVideoSourceSSRCList(video_ssrc_list_it->second);
  }

  auto audio_ssrc_it = remote_sdp_->audio_ssrc_map.find(getLabel());
  if (audio_ssrc_it != remote_sdp_->audio_ssrc_map.end()) {
    OLOG_DEBUG(toLog() << ", setAudioSourceSSRC, ssrc: " << audio_ssrc_it->second);
    setAudioSourceSSRC(audio_ssrc_it->second);
  }

  if (getVideoSourceSSRCList().empty() ||
      (getVideoSourceSSRCList().size() == 1 && getVideoSourceSSRCList()[0] == 0)) {
    std::vector<uint32_t> default_ssrc_list;
    default_ssrc_list.push_back(kDefaultVideoSinkSSRC);
    setVideoSourceSSRCList(default_ssrc_list);
  }

  if (getAudioSourceSSRC() == 0) {
    setAudioSourceSSRC(kDefaultAudioSinkSSRC);
  }

  if (pipeline_initialized_ && pipeline_) {
    pipeline_->notifyUpdate();
    return true;
  }

  bundle_ = remote_sdp_->isBundle;

  audio_enabled_ = remote_sdp_->hasAudio;
  video_enabled_ = remote_sdp_->hasVideo;

  initializePipeline();

  //initializeStats();

  return true;
}

void MediaStream::initializeStats()  {
  log_stats_->getNode().insertStat("streamId", StringStat{getId()});
  log_stats_->getNode().insertStat("audioBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioFL", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioPL", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioJitter", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioMuted", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioNack", CumulativeStat{0});
  log_stats_->getNode().insertStat("audioRemb", CumulativeStat{0});

  log_stats_->getNode().insertStat("videoBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoFL", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoPL", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoJitter", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoMuted", CumulativeStat{0});
  log_stats_->getNode().insertStat("slideshow", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoNack", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoPli", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoFir", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoRemb", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoErizoRemb", CumulativeStat{0});
  log_stats_->getNode().insertStat("videoKeyFrames", CumulativeStat{0});

  log_stats_->getNode().insertStat("SL0TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL0TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL0TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL0TL3", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL1TL3", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL2TL3", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL0", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL1", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL2", CumulativeStat{0});
  log_stats_->getNode().insertStat("SL3TL3", CumulativeStat{0});

  log_stats_->getNode().insertStat("maxActiveSL", CumulativeStat{0});
  log_stats_->getNode().insertStat("maxActiveTL", CumulativeStat{0});
  log_stats_->getNode().insertStat("selectedSL", CumulativeStat{0});
  log_stats_->getNode().insertStat("selectedTL", CumulativeStat{0});
  log_stats_->getNode().insertStat("isPublisher", CumulativeStat{is_publisher_});

  log_stats_->getNode().insertStat("totalBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("rtxBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("paddingBitrate", CumulativeStat{0});
  log_stats_->getNode().insertStat("bwe", CumulativeStat{0});

  log_stats_->getNode().insertStat("maxVideoBW", CumulativeStat{0});

  auto weak_this = weak_from_this();
  worker_->scheduleEvery([weak_this] () {
    if (auto stream = weak_this.lock()) {
      if (stream->sending_) {
        stream->printStats();
        return true;
      }
    }
    return false;
  }, kStreamStatsPeriod);
}

void MediaStream::transferLayerStats(std::string spatial, std::string temporal) {
  std::string node = "SL" + spatial + "TL" + temporal;
  if (stats_->getNode().hasChild("qualityLayers") &&
      stats_->getNode()["qualityLayers"].hasChild(spatial) &&
      stats_->getNode()["qualityLayers"][spatial].hasChild(temporal)) {
    log_stats_->getNode()
      .insertStat(node, CumulativeStat{stats_->getNode()["qualityLayers"][spatial][temporal].value()});
  }
}

void MediaStream::transferMediaStats(
    std::string target_node, std::string source_parent, std::string source_node) {
  if (stats_->getNode().hasChild(source_parent) &&
      stats_->getNode()[source_parent].hasChild(source_node)) {
    log_stats_->getNode()
      .insertStat(target_node, CumulativeStat{stats_->getNode()[source_parent][source_node].value()});
  }
}

void MediaStream::printStats() {
  return;
  std::string video_ssrc;
  std::string audio_ssrc;

  log_stats_->getNode().insertStat("audioEnabled", CumulativeStat{audio_enabled_});
  log_stats_->getNode().insertStat("videoEnabled", CumulativeStat{video_enabled_});

  log_stats_->getNode().insertStat("maxVideoBW", CumulativeStat{getMaxVideoBW()});

  if (audio_enabled_) {
    audio_ssrc = std::to_string(is_publisher_ ? getAudioSourceSSRC() : getAudioSinkSSRC());
    transferMediaStats("audioBitrate", audio_ssrc, "bitrateCalculated");
    transferMediaStats("audioPL",      audio_ssrc, "packetsLost");
    transferMediaStats("audioFL",      audio_ssrc, "fractionLost");
    transferMediaStats("audioJitter",  audio_ssrc, "jitter");
    transferMediaStats("audioMuted",   audio_ssrc, "erizoAudioMute");
    transferMediaStats("audioNack",    audio_ssrc, "NACK");
    transferMediaStats("audioRemb",    audio_ssrc, "bandwidth");
  }
  if (video_enabled_) {
    video_ssrc = std::to_string(is_publisher_ ? getVideoSourceSSRC() : getVideoSinkSSRC());
    transferMediaStats("videoBitrate", video_ssrc, "bitrateCalculated");
    transferMediaStats("videoPL",      video_ssrc, "packetsLost");
    transferMediaStats("videoFL",      video_ssrc, "fractionLost");
    transferMediaStats("videoJitter",  video_ssrc, "jitter");
    transferMediaStats("videoMuted",   audio_ssrc, "erizoVideoMute");
    transferMediaStats("slideshow",    video_ssrc, "erizoSlideShow");
    transferMediaStats("videoNack",    video_ssrc, "NACK");
    transferMediaStats("videoPli",     video_ssrc, "PLI");
    transferMediaStats("videoFir",     video_ssrc, "FIR");
    transferMediaStats("videoRemb",    video_ssrc, "bandwidth");
    transferMediaStats("videoErizoRemb", video_ssrc, "erizoBandwidth");
    transferMediaStats("videoKeyFrames", video_ssrc, "keyFrames");
  }

  for (uint32_t spatial = 0; spatial <= 3; spatial++) {
    for (uint32_t temporal = 0; temporal <= 3; temporal++) {
      transferLayerStats(std::to_string(spatial), std::to_string(temporal));
    }
  }

  transferMediaStats("maxActiveSL", "qualityLayers", "maxActiveSpatialLayer");
  transferMediaStats("maxActiveTL", "qualityLayers", "maxActiveTemporalLayer");
  transferMediaStats("selectedSL", "qualityLayers", "selectedSpatialLayer");
  transferMediaStats("selectedTL", "qualityLayers", "selectedTemporalLayer");
  transferMediaStats("totalBitrate", "total", "bitrateCalculated");
  transferMediaStats("paddingBitrate", "total", "paddingBitrate");
  transferMediaStats("rtxBitrate", "total", "rtxBitrate");
  transferMediaStats("bwe", "total", "senderBitrateEstimation");

  ELOG_INFOT(statsLogger, "%s", log_stats_->getStats());
}

void MediaStream::initializePipeline() {
  pipeline_->addService(shared_from_this());
  pipeline_->addService(stats_);
  pipeline_->addFront(std::make_shared<PacketReader>(this));
  pipeline_->addFront(WoogeenHandler(this));
  pipeline_->addFront(std::make_shared<PacketWriter>(this));
  
  pipeline_->finalize();
  pipeline_initialized_ = true;
}

int MediaStream::deliverAudioData_(std::shared_ptr<DataPacket> audio_packet) {
  if (audio_enabled_) {
    sendPacket(std::make_shared<DataPacket>(*audio_packet));
  }
  return audio_packet->length;
}

int MediaStream::deliverVideoData_(std::shared_ptr<DataPacket> video_packet) {
  if (video_enabled_) {
    sendPacket(std::make_shared<DataPacket>(*video_packet));
  }
  return video_packet->length;
}

int MediaStream::deliverFeedback_(std::shared_ptr<DataPacket> fb_packet) {
/*
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(fb_packet->data);
  uint32_t recvSSRC = chead->getSourceSSRC();
  if (chead->isREMB()) {
    for (uint8_t index = 0; index < chead->getREMBNumSSRC(); index++) {
      uint32_t ssrc = chead->getREMBFeedSSRC(index);
      if (isVideoSourceSSRC(ssrc)) {
        recvSSRC = ssrc;
        break;
      }
    }
  }
  
  if (isVideoSourceSSRC(recvSSRC)) {
    fb_packet->type = VIDEO_PACKET;
    sendPacket(std::make_shared<DataPacket>(*fb_packet));
  } else if (isAudioSourceSSRC(recvSSRC)) {
    fb_packet->type = AUDIO_PACKET;
    sendPacket(std::make_shared<DataPacket>(*fb_packet));
  } else {
    ELOG_DEBUG("%s deliverFeedback unknownSSRC: %u," 
               "localVideoSSRC: %u, localAudioSSRC: %u",
               toLog(), recvSSRC, 
               this->getVideoSourceSSRC(), this->getAudioSourceSSRC());
  }
*/
  sendPacket(std::make_shared<DataPacket>(*fb_packet));
  
  return fb_packet->length;
}

int MediaStream::deliverEvent_(MediaEventPtr event) {
  if (!pipeline_initialized_) {
    return 1;
  }

  if (pipeline_) {
    pipeline_->notifyEvent(event);
  }
  return 1;
}

void MediaStream::onTransportData(std::shared_ptr<DataPacket> incoming_packet, 
                                  Transport*) {
  if (audio_sink_ == nullptr && video_sink_ == nullptr && fb_sink_ == nullptr) {
    return;
  }

  auto packet = std::make_shared<DataPacket>(*incoming_packet);

  if (!pipeline_initialized_) {
    ELOG_ERROR("%s message: Pipeline not initialized yet.", toLog());
    return;
  }

  char* buf = packet->data;
  RtpHeader *head = reinterpret_cast<RtpHeader*> (buf);
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*> (buf);

  // PROCESS RTCP
  if (chead->isRtcp()) {
    if (is_publisher_) {
      assert(fb_sink_ == nullptr);
      if (video_sink_) {
        video_sink_->deliverVideoData(std::move(packet));
      }

      if (audio_sink_) {
        audio_sink_->deliverAudioData(std::move(packet));
      }
      
    } else if (fb_sink_ != nullptr && should_send_feedback_) {
       fb_sink_->deliverFeedback(std::move(packet));
    }
    return;
  }
  
  uint32_t recvSSRC = head->getSSRC();
  if (isVideoSourceSSRC(recvSSRC)) {
    packet->type = VIDEO_PACKET;
  } else if (isAudioSourceSSRC(recvSSRC)) {
    packet->type = AUDIO_PACKET;      
  }

  if (pipeline_) {
    pipeline_->read(std::move(packet));
  }
}

void MediaStream::read(std::shared_ptr<DataPacket> packet) {
  char* buf = packet->data;
 
  RtpHeader *head = reinterpret_cast<RtpHeader*> (buf);
  uint32_t recvSSRC = 0;
  recvSSRC = head->getSSRC();

  // RTP
  if (bundle_) {
    // Check incoming SSRC
    // Deliver data
    int len = packet->length;
    if (isVideoSourceSSRC(recvSSRC) && video_sink_) {
      parseIncomingPayloadType(buf, len, VIDEO_PACKET);
      video_sink_->deliverVideoData(std::move(packet));
    } else if (isAudioSourceSSRC(recvSSRC) && audio_sink_) {
      parseIncomingPayloadType(buf, len, AUDIO_PACKET);
      audio_sink_->deliverAudioData(std::move(packet));
    } else {
      ELOG_WARN("%s read video unknownSSRC: %u, "
                "localVideoSSRC: %u, localAudioSSRC: %u",
                toLog(), recvSSRC, 
                this->getVideoSourceSSRC(), this->getAudioSourceSSRC());
    }
  } 

  //TODO if not bundle
}

void MediaStream::setMediaStreamEventListener(MediaStreamEventListener* listener) {
  this->media_stream_event_listener_ = listener;
}

void MediaStream::notifyMediaStreamEvent(
    const std::string& type, const std::string& message) {
  if (this->media_stream_event_listener_ != nullptr) {
    media_stream_event_listener_->notifyMediaStreamEvent(type, message);
  }
}

void MediaStream::notifyToEventSink(MediaEventPtr event) {
  event_sink_->deliverEvent(std::move(event));
}

int MediaStream::sendPLI() {
  RtcpHeader thePLI;
  thePLI.setPacketType(RTCP_PS_Feedback_PT);
  thePLI.setBlockCount(1);
  thePLI.setSSRC(this->getVideoSinkSSRC());
  thePLI.setSourceSSRC(this->getVideoSourceSSRC());
  thePLI.setLength(2);
  char *buf = reinterpret_cast<char*>(&thePLI);
  int len = (thePLI.getLength() + 1) * 4;
  sendPacket(std::make_shared<DataPacket>(0, buf, len, VIDEO_PACKET));
  return len;
}

void MediaStream::sendPLIToFeedback() {
  if (fb_sink_) {
    fb_sink_->deliverFeedback(RtpUtils::createPLI(this->getVideoSinkSSRC(),
      this->getVideoSourceSSRC()));
  }
}

// changes the outgoing payload type for in the given data packet
void MediaStream::sendPacket(std::shared_ptr<DataPacket> packet) {
  if (!sending_) {
    return;
  }
  
  if (packet->comp == -1) {
    sending_ = false;
    auto p = std::make_shared<DataPacket>();
    p->comp = -1;
    sendPacket_i(p);
    return;
  }

  changeDeliverPayloadType(packet.get(), packet->type);
  sendPacket_i(packet);
}

void MediaStream::muteStream(bool mute_video, bool mute_audio) {
  OLOG_TRACE_THIS(toLog() << (mute_video?" mute_video ":" ") << (mute_audio?" mute_audio ":" "));
  audio_muted_ = mute_audio;
  video_muted_ = mute_video;
  stats_->getNode()[getAudioSinkSSRC()].insertStat("erizoAudioMute", CumulativeStat{mute_audio});
  stats_->getNode()[getAudioSinkSSRC()].insertStat("erizoVideoMute", CumulativeStat{mute_video});
  if (pipeline_) {
    pipeline_->notifyUpdate();
  }
}

void MediaStream::setTransportInfo(std::string audio_info, std::string video_info) {
  if (video_enabled_) {
    uint32_t video_sink_ssrc = getVideoSinkSSRC();
    uint32_t video_source_ssrc = getVideoSourceSSRC();

    if (video_sink_ssrc != kDefaultVideoSinkSSRC) {
      stats_->getNode()[video_sink_ssrc].insertStat("clientHostType", StringStat{video_info});
    }
    if (video_source_ssrc != 0) {
      stats_->getNode()[video_source_ssrc].insertStat("clientHostType", StringStat{video_info});
    }
  }

  if (audio_enabled_) {
    uint32_t audio_sink_ssrc = getAudioSinkSSRC();
    uint32_t audio_source_ssrc = getAudioSourceSSRC();

    if (audio_sink_ssrc != kDefaultAudioSinkSSRC) {
      stats_->getNode()[audio_sink_ssrc].insertStat("clientHostType", StringStat{audio_info});
    }
    if (audio_source_ssrc != 0) {
      stats_->getNode()[audio_source_ssrc].insertStat("clientHostType", StringStat{audio_info});
    }
  }
}

/*
void MediaStream::setFeedbackReports(bool will_send_fb, uint32_t target_bitrate) {
  this->should_send_feedback_ = will_send_fb;
  if (target_bitrate == 1) {
    this->video_enabled_ = false;
  }
  rate_control_ = target_bitrate;
}
*/

void MediaStream::setMetadata(std::map<std::string, std::string> metadata) {
  for (const auto &item : metadata) {
    log_stats_->getNode().insertStat("metadata-" + item.first, StringStat{item.second});
  }
  setLogContext(metadata);
}

WebRTCEvent MediaStream::getCurrentState() {
  return connection_->getCurrentState();
}

void MediaStream::getJSONStats(std::function<void(std::string)> callback) {
  std::string requested_stats = stats_->getStats();
  //  ELOG_DEBUG("%s message: Stats, stats: %s", stream->toLog(), requested_stats.c_str());
  callback(requested_stats);
}

void MediaStream::changeDeliverPayloadType(DataPacket *dp, packetType type) {
  RtpHeader* h = reinterpret_cast<RtpHeader*>(dp->data);
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(dp->data);
  if (!chead->isRtcp()) {
    int internalPT = h->getPayloadType();
    int externalPT = internalPT;
    if (type == AUDIO_PACKET) {
        externalPT = remote_sdp_->getAudioExternalPT(internalPT);
    } else if (type == VIDEO_PACKET) {
        externalPT = remote_sdp_->getVideoExternalPT(externalPT);
    }
    if (internalPT != externalPT) {
        h->setPayloadType(externalPT);
    }
  }
}

// parses incoming payload type, replaces occurence in buf
void MediaStream::parseIncomingPayloadType(char *buf, int len, packetType type) {
  RtcpHeader* chead = reinterpret_cast<RtcpHeader*>(buf);
  RtpHeader* h = reinterpret_cast<RtpHeader*>(buf);
  if (!chead->isRtcp()) {
    int externalPT = h->getPayloadType();
    int internalPT = externalPT;
    if (type == AUDIO_PACKET) {
      internalPT = remote_sdp_->getAudioInternalPT(externalPT);
    } else if (type == VIDEO_PACKET) {
      internalPT = remote_sdp_->getVideoInternalPT(externalPT);
    }
    
    if (externalPT != internalPT) {
      h->setPayloadType(internalPT);
    }
  }
}

void MediaStream::write(std::shared_ptr<DataPacket> packet) {
  if (connection_) {
    connection_->write(packet);
  }
}

void MediaStream::enableHandler(const std::string &name) {
  if (pipeline_) {
    pipeline_->enable(name);
  }
}

void MediaStream::disableHandler(const std::string &name) {
  if (pipeline_) {
    pipeline_->disable(name);
  }
}

void MediaStream::notifyUpdateToHandlers(){
  if (pipeline_) {
    pipeline_->notifyUpdate();
  }
}

void MediaStream::sendPacket_i(std::shared_ptr<DataPacket> p) {
  if (!sending_) {
    return;
  }
  uint32_t partial_bitrate = 0;
  uint64_t sentVideoBytes = 0;
  uint64_t lastSecondVideoBytes = 0;

  if (rate_control_) {
    if (p->type == VIDEO_PACKET) {
      if (rate_control_ == 1) {
        return;
      }
      now_ = wa::clock::now();
      if ((now_ - mark_) >= kBitrateControlPeriod) {
        mark_ = now_;
        lastSecondVideoBytes = sentVideoBytes;
      }
      partial_bitrate = ((sentVideoBytes - lastSecondVideoBytes) * 8) * 10;
      if (partial_bitrate > this->rate_control_) {
        return;
      }
      sentVideoBytes += p->length;
    }
  }
  
  if (!pipeline_initialized_) {
    ELOG_DEBUG("%s message: Pipeline not initialized yet.", toLog());
    return;
  }

  if (pipeline_) {
    pipeline_->write(std::move(p));
  }
}

}  // namespace erizo

