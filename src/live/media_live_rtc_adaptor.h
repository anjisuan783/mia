#ifndef __MEDIA_LIVE_RTC_ADAPTOR_H__
#define __MEDIA_LIVE_RTC_ADAPTOR_H__

#include <memory>

#include "common/media_message.h"
#include "h/rtc_media_frame.h"
#include "live/media_live_source_sink.h"


namespace ma {

class LiveRtcAdapterSink;
class MediaLiveSource;
class MediaRequest;
class MediaConsumer;
  
class MediaLiveRtcAdaptor : public RtmpMediaSink {
 public:
  MediaLiveRtcAdaptor(const std::string& streamName);
  ~MediaLiveRtcAdaptor();

  void Open(MediaLiveSource*, LiveRtcAdapterSink* sink);
  void Close();

  srs_error_t OnAudio(std::shared_ptr<MediaMessage>, bool from_adaptor) override;
  srs_error_t OnVideo(std::shared_ptr<MediaMessage>, bool from_adaptor) override;
 private:
  void OnTimer();

  std::string stream_name_;
  std::shared_ptr<MediaConsumer> consumer_;
  LiveRtcAdapterSink* rtc_source_{nullptr};
  MediaLiveSource* live_source_{nullptr};
};

}

#endif //!__MEDIA_LIVE_RTC_ADAPTOR_H__
