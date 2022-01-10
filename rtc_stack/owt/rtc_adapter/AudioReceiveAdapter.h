#ifndef __RTC_ADAPTER_AUDIO_RECEIVE_ADAPTER_H__
#define __RTC_ADAPTER_AUDIO_RECEIVE_ADAPTER_H__

#include "common/logger.h"

#include "api/transport.h"
#include "call/call.h"
#include "call/audio_receive_stream.h"
#include "common/logger.h"

#include "rtc_adapter/AdapterInternalDefinitions.h"
#include "rtc_adapter/RtcAdapter.h"

namespace rtc_adapter {

class AdapterDataListener;
class CallOwner;

class AudioReceiveAdapterImpl : public AudioReceiveAdapter,
                                public webrtc::Transport {
  DECLARE_LOGGER();
 public:
  AudioReceiveAdapterImpl(CallOwner* owner, const RtcAdapter::Config& config);
  ~AudioReceiveAdapterImpl() override;
  int onRtpData(char* data, int len) override;
  bool SendRtp(const uint8_t* packet,
               size_t length,
               const webrtc::PacketOptions& options) override;
  bool SendRtcp(const uint8_t* packet, size_t length) override;

 private:
  void CreateReceiveAudio();
  std::shared_ptr<webrtc::Call> call() {
     return owner_ ? owner_->call() : nullptr;
  }

 private:
  RtcAdapter::Config config_;
  CallOwner* owner_{nullptr};
  webrtc::AudioReceiveStream* audioRecvStream_{nullptr};
  AdapterDataListener* rtcpListener_;
};

} //namespace rtc_adapter

#endif

