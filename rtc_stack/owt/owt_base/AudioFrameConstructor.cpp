#include "owt_base/AudioFrameConstructor.h"

#include "rtp_rtcp/byte_io.h"
#include "owt_base/AudioUtilitiesNew.h"
#include "common/rtputils.h"

namespace owt_base {

DEFINE_LOGGER(AudioFrameConstructor, "owt.AudioFrameConstructor");

// TODO: Get the extension ID from SDP
constexpr uint8_t kAudioLevelExtensionId = 1;

namespace {
class AudioLevel {
 public:
  inline uint8_t getId() {
    return ext_info >> 4;
  }
  inline uint8_t getLength() {
    return (ext_info & 0x0F);
  }
  inline bool getVoice() {
    return (al_data & 0x80) != 0;
  }
  inline uint8_t getLevel() {
    return al_data & 0x7F;
  }
 private:
  uint32_t ext_info:8;
  uint8_t al_data:8;
};

std::unique_ptr<AudioLevel> parseAudioLevel(std::shared_ptr<erizo::DataPacket> p) {
  const erizo::RtpHeader* head = reinterpret_cast<const erizo::RtpHeader*>(p->data);
  std::unique_ptr<AudioLevel> ret;
  if (head->getExtension()) {
    uint16_t totalExtLength = head->getExtLength();
    if (head->getExtId() == 0xBEDE) {
      char* extBuffer = (char*)&head->extensions;  // NOLINT
      uint8_t extByte = 0;
      uint16_t currentPlace = 1;
      uint8_t extId = 0;
      uint8_t extLength = 0;
      while (currentPlace < (totalExtLength*4)) {
        extByte = (uint8_t)(*extBuffer);
        extId = extByte >> 4;
        extLength = extByte & 0x0F;
        if (extId == kAudioLevelExtensionId) {
          ret.reset(new AudioLevel());
          memcpy(ret.get(), extBuffer, sizeof(AudioLevel));
          break;
        }
        extBuffer = extBuffer + extLength + 2;
        currentPlace = currentPlace + extLength + 2;
      }
    }
  }
  return ret;
}

}

AudioFrameConstructor::AudioFrameConstructor(const config& config)
    : config_{config},
      rtcAdapter_{std::move(config.factory->CreateRtcAdapter())} {
  sink_fb_source_ = this;
}

AudioFrameConstructor::~AudioFrameConstructor() {
  unbindTransport();
  if (audioReceive_) {
    rtcAdapter_->destoryAudioReceiver(audioReceive_);
    audioReceive_ = nullptr;
  }
}

void AudioFrameConstructor::bindTransport(
    erizo::MediaSource* source, erizo::FeedbackSink* fbSink) {
  transport_ = source;
  transport_->setAudioSink(this);
  transport_->setEventSink(this);
  setFeedbackSink(fbSink);
}

void AudioFrameConstructor::unbindTransport() {
  if (transport_) {
    setFeedbackSink(nullptr);
    transport_ = nullptr;
  }
}

int AudioFrameConstructor::deliverVideoData_(
    std::shared_ptr<erizo::DataPacket> video_packet) {
  assert(false);
  return 0;
}

int AudioFrameConstructor::deliverAudioData_(
    std::shared_ptr<erizo::DataPacket> audio_packet) {
  if (audio_packet->length <= 0) {
    return 0;
  }

  erizo::RtcpHeader* chead = 
      reinterpret_cast<erizo::RtcpHeader*>(audio_packet->data);

  if (chead->isRtcp()) {
    if (chead->getPacketType() == RTCP_Sender_PT)
      onSr(chead);

    if (audioReceive_)
      audioReceive_->onRtpData(audio_packet.get());
    return audio_packet->length;
  }

  RTPHeader* head = reinterpret_cast<RTPHeader*>(audio_packet->data);
  if (!ssrc_ && head->getSSRC()) {
    createAudioReceiver();
  }

  FrameFormat frameFormat;
  Frame frame;
  memset(&frame, 0, sizeof(frame));

  frameFormat = getAudioFrameFormat(head->getPayloadType());
  if (frameFormat == FRAME_FORMAT_UNKNOWN) {
    ELOG_ERROR("audio format not found f:%d. pt:%d", frameFormat, head->getPayloadType());
    return 0;
  }

  frame.additionalInfo.audio.sampleRate = getAudioSampleRate(frameFormat);
  frame.additionalInfo.audio.channels = getAudioChannels(frameFormat);

  frame.format = frameFormat;
  frame.payload = reinterpret_cast<uint8_t*>(audio_packet->data);
  frame.length = audio_packet->length;
  frame.timeStamp = head->getTimestamp();  
  frame.ntpTimeMs = getNtpTimestamp(frame.timeStamp);
  frame.additionalInfo.audio.isRtpPacket = 1;

  std::unique_ptr<AudioLevel> audioLevel = parseAudioLevel(audio_packet);
  if (audioLevel) {
    frame.additionalInfo.audio.audioLevel = audioLevel->getLevel();
    frame.additionalInfo.audio.voice = audioLevel->getVoice();
  } else if (!no_audio_level_) {
    ELOG_TRACE("No audio level extension");
    no_audio_level_ = true;
  }

  auto copy = std::make_shared<Frame>(frame);
  
  if (enabled_) {
    deliverFrame(std::move(copy));
  }

  // support audio twcc, 
  // see @https://github.com/anjisuan783/mia/issues/8
  if (audioReceive_)
    audioReceive_->onRtpData(audio_packet.get());
  
  return audio_packet->length;
}

void AudioFrameConstructor::onFeedback(const FeedbackMsg& msg) {
  if (msg.type == owt_base::AUDIO_FEEDBACK) {
    if (msg.cmd == RTCP_PACKET && fb_sink_)
      fb_sink_->deliverFeedback(std::make_shared<erizo::DataPacket>(
          0, msg.data.rtcp.buf, msg.data.rtcp.len, erizo::AUDIO_PACKET));
  }
}

int AudioFrameConstructor::deliverEvent_(erizo::MediaEventPtr event) {
  return 0;
}

void AudioFrameConstructor::onAdapterData(char* data, int len) {
  // Data come from audio receive stream is RTCP
  if (fb_sink_) {
    fb_sink_->deliverFeedback(
      std::make_shared<erizo::DataPacket>(0, data, len, erizo::AUDIO_PACKET));
  }
}

void AudioFrameConstructor::close() {
  unbindTransport();
}

void AudioFrameConstructor::createAudioReceiver() {
  if (audioReceive_) {
    return;
  }
  ssrc_ = config_.ssrc;

  //audio do not support twcc
  if (-1 == config_.transportcc)
    return;
  
  // Create Receive audio Stream for transport-cc
  rtc_adapter::RtcAdapter::Config recvConfig;

  recvConfig.ssrc = config_.ssrc;
  recvConfig.rtcp_rsize = config_.rtcp_rsize;
  recvConfig.rtp_payload_type = config_.rtp_payload_type;
  recvConfig.transport_cc = config_.transportcc;
  recvConfig.rtp_listener = this;

  audioReceive_ = rtcAdapter_->createAudioReceiver(recvConfig);
}

void AudioFrameConstructor::onSr(erizo::RtcpHeader *chead) {
  const uint8_t* const payload = reinterpret_cast<const uint8_t*>(&(chead->ssrc));
  uint32_t ntp_secs = webrtc::ByteReader<uint32_t>::ReadBigEndian(&payload[4]);
  uint32_t ntp_frac = webrtc::ByteReader<uint32_t>::ReadBigEndian(&payload[8]);
  webrtc::NtpTime ntp;
  ntp.Set(ntp_secs, ntp_frac);
  
  uint32_t rtp_timestamp = webrtc::ByteReader<uint32_t>::ReadBigEndian(&payload[12]);  
  bool new_rtcp_sr = false;
  ntp_estimator_.UpdateMeasurements(ntp_secs, ntp_frac, rtp_timestamp, &new_rtcp_sr);
}

int64_t AudioFrameConstructor::getNtpTimestamp(uint32_t ts) {
  int64_t ntp = 0;

  if(!ntp_estimator_.Estimate(ts, &ntp))
    return -1;

  return ntp;
}

}//namespace owt_base

