#ifndef __MEDIA_LIVE_RTC_ADAPTOR_SINK_H__
#define __MEDIA_LIVE_RTC_ADAPTOR_SINK_H__

#include <memory>

#include "h/rtc_media_frame.h"

namespace ma {

class LiveRtcAdapterSink {
 public:
  virtual ~LiveRtcAdapterSink() = default;
  virtual void OnLocalPublish(const std::string& streamName) = 0;
  virtual void OnLocalUnpublish() = 0;
  virtual void OnFrame(std::shared_ptr<owt_base::Frame>) = 0;
};

}

#endif //!__MEDIA_LIVE_RTC_ADAPTOR_SINK_H__

