#include "rtc_adapter/AudioReceiveAdapter.h"

#include "rtc_base/time_utils.h"

namespace rtc_adapter {

DEFINE_LOGGER(AudioReceiveAdapterImpl, "AudioReceiveAdapterImpl")

const uint32_t kLocalSsrc = 1;

AudioReceiveAdapterImpl::AudioReceiveAdapterImpl(
    CallOwner* owner, const RtcAdapter::Config& config)
    : config_(config), owner_(owner), rtcpListener_(config.rtp_listener) {
  assert(owner != nullptr);
  CreateReceiveAudio();
}

AudioReceiveAdapterImpl::~AudioReceiveAdapterImpl() {
  if (audioRecvStream_) {
    audioRecvStream_->Stop();
    call()->DestroyAudioReceiveStream(audioRecvStream_);
    audioRecvStream_ = nullptr;
  }
}

int AudioReceiveAdapterImpl::onRtpData(char* data, int len) {
  auto rv = call()->Receiver()->DeliverPacket(
          webrtc::MediaType::AUDIO,
          rtc::CopyOnWriteBuffer(data, len),
          rtc::TimeUTCMicros());
  if (webrtc::PacketReceiver::DELIVERY_OK != rv) {
    OLOG_ERROR_THIS("AudioReceiveAdapterImpl DeliverPacket failed code:" << rv);
  }
  return len;
}

bool AudioReceiveAdapterImpl::SendRtp(const uint8_t*,
                                      size_t,
                                      const webrtc::PacketOptions&) {
  OLOG_WARN_THIS("AudioReceiveAdapterImpl SendRtp called");
  return true;
}

bool AudioReceiveAdapterImpl::SendRtcp(const uint8_t* data, size_t length) {
  if (rtcpListener_) {
    rtcpListener_->onAdapterData(
        reinterpret_cast<char*>(const_cast<uint8_t*>(data)), length);
    return true;
  }
  return false;
}

void AudioReceiveAdapterImpl::CreateReceiveAudio() {
  if (audioRecvStream_) {
     return;
  }
  
  // Create Receive audio Stream
  webrtc::AudioReceiveStream::Config audio_recv_config;

  //config rtp 
  audio_recv_config.rtp.remote_ssrc = config_.ssrc;
  audio_recv_config.rtp.local_ssrc = kLocalSsrc;
   
  if (config_.transport_cc != -1) {
    audio_recv_config.rtp.transport_cc = true;
    audio_recv_config.rtp.extensions.emplace_back(
      webrtc::RtpExtension::kTransportSequenceNumberUri, config_.transport_cc);
  } else {
    audio_recv_config.rtp.transport_cc = false;
  }

  if (config_.mid_ext) {
    audio_recv_config.rtp.extensions.emplace_back(
      webrtc::RtpExtension::kMidUri, config_.mid_ext);
  }

  audio_recv_config.rtcp_send_transport = this;

  //config decoder
  auto audio_format = webrtc::SdpAudioFormat("opus", 48000, 2);
  audio_recv_config.decoder_map.emplace(config_.rtp_payload_type, audio_format);

  audioRecvStream_ = call()->CreateAudioReceiveStream(std::move(audio_recv_config));
  audioRecvStream_->Start();
  call()->SignalChannelNetworkState(webrtc::MediaType::AUDIO, webrtc::NetworkState::kNetworkUp);
}

} //namespace rtc_adapter

