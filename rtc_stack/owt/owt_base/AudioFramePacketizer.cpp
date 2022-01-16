#include "owt_base/AudioFramePacketizer.h"
#include "owt_base/AudioUtilitiesNew.h"
#include "myrtc/api/task_queue_base.h"
#include "rtc_adapter/thread/StaticTaskQueueFactory.h"

using namespace rtc_adapter;

namespace owt_base {

DEFINE_LOGGER(AudioFramePacketizer, "owt.AudioFramePacketizer");

AudioFramePacketizer::AudioFramePacketizer(AudioFramePacketizer::Config& config)
    : rtcAdapter_(std::move(config.factory->CreateRtcAdapter())) {
  auto factory = rtc_adapter::createDummyTaskQueueFactory(config.task_queue);
  auto task_queue = factory->CreateTaskQueue(
      "deliver_frame", webrtc::TaskQueueFactory::Priority::NORMAL);
  task_queue_ = std::move(
      std::make_unique<rtc::TaskQueue>(std::move(task_queue)));
  
  init(config);
}

AudioFramePacketizer::~AudioFramePacketizer() {
  close();
  if (audioSend_) {
    rtcAdapter_->destoryAudioSender(audioSend_);
    rtcAdapter_.reset();
    audioSend_ = nullptr;
  }
}

void AudioFramePacketizer::bindTransport(erizo::MediaSink* sink) {
  audio_sink_ = sink;
  audio_sink_->setAudioSinkSSRC(audioSend_->ssrc());
  erizo::FeedbackSource* fbSource = audio_sink_->getFeedbackSource();
  if (fbSource)
      fbSource->setFeedbackSink(this);
}

void AudioFramePacketizer::unbindTransport() {
  audio_sink_ = nullptr;
}

int AudioFramePacketizer::deliverFeedback_(
    std::shared_ptr<erizo::DataPacket> data_packet) {
  if (audioSend_) {
    audioSend_->onRtcpData(data_packet->data, data_packet->length);
    return data_packet->length;
  }
  return 0;
}

void AudioFramePacketizer::receiveRtpData(char* buf, int len, 
    erizoExtra::DataType type, uint32_t channelId) {
  if (!audio_sink_) {
      return;
  }

  assert(type == erizoExtra::AUDIO);
  audio_sink_->deliverAudioData(
      std::make_shared<erizo::DataPacket>(0, buf, len, erizo::AUDIO_PACKET));
}

void AudioFramePacketizer::onFrame(std::shared_ptr<Frame> f) {
  if (f->length <= 0) {
    return;
  }
 
  task_queue_->PostTask([this, weak_ptr = weak_from_this(), frame = std::move(f)] () {
    if (auto shared_this = weak_ptr.lock()) {
      if (!enable_) {
        return;
      }

      if (!audio_sink_) {
        return;
      }

      if (audioSend_) {
          audioSend_->onFrame(std::move(frame));
      }
    }
  }); 
}

bool AudioFramePacketizer::init(AudioFramePacketizer::Config& config) {
  if (!audioSend_) {
    // Create Send audio Stream
    rtc_adapter::RtcAdapter::Config sendConfig;
    sendConfig.rtp_listener = this;
    sendConfig.stats_listener = this;
    if (!config.mid.empty()) {
      strncpy(sendConfig.mid, config.mid.c_str(), sizeof(sendConfig.mid) - 1);
      sendConfig.mid_ext = config.midExtId;
    }
    audioSend_ = rtcAdapter_->createAudioSender(sendConfig);
    ssrc_ = audioSend_->ssrc();
    return true;
  }
  return false;
}

void AudioFramePacketizer::onAdapterStats(const AdapterStats& stats) {}

void AudioFramePacketizer::onAdapterData(char* data, int len) {
  if (audio_sink_) {
    audio_sink_->deliverAudioData(
        std::make_shared<erizo::DataPacket>(0, data, len, erizo::AUDIO_PACKET));
  }
}

void AudioFramePacketizer::close() {
  unbindTransport();
}

int AudioFramePacketizer::sendPLI() {
  return 0;
}

} //namespace owt_base

