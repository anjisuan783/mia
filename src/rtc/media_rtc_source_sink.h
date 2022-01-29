#ifndef __MEDIA_RTC_SOURCE_SINK_H__
#define __MEDIA_RTC_SOURCE_SINK_H__

#include <memory>

#include "h/rtc_media_frame.h"

namespace ma {

class RtcMediaSink {
 public:
  virtual ~RtcMediaSink() = default;
  virtual void OnMediaFrame(std::shared_ptr<owt_base::Frame> frm) = 0;
};

}

#endif //!__MEDIA_RTC_SOURCE_SINK_H__

