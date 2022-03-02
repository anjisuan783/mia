#ifndef __MEDIA_LIVE_RTC_ADAPTOR_SINK_H__
#define __MEDIA_LIVE_RTC_ADAPTOR_SINK_H__

#include <memory>

#include "h/rtc_media_frame.h"

namespace ma {

class LiveRtcAdapterSink {
 public:
  virtual ~LiveRtcAdapterSink() = default;
  virtual srs_error_t OnLocalPublish(const std::string& streamName) = 0;
  virtual srs_error_t OnLocalUnpublish() = 0;
  virtual void OnFrame(std::shared_ptr<owt_base::Frame>) = 0;
};

}

#endif //!__MEDIA_LIVE_RTC_ADAPTOR_SINK_H__

