#ifndef __MEDIA_RTC_LIVE_ADAPTOR_SINK_H__
#define __MEDIA_RTC_LIVE_ADAPTOR_SINK_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "common/media_message.h"

namespace ma {

class RtcLiveAdapterSink {
 public:
  virtual ~RtcLiveAdapterSink() = default;
  virtual void OnPublish() = 0;
  virtual void OnUnpublish() = 0;
  virtual srs_error_t OnAudio(std::shared_ptr<MediaMessage>, bool from_adaptor) = 0;
  virtual srs_error_t OnVideo(std::shared_ptr<MediaMessage>, bool from_adaptor) = 0;
};

}

#endif //!__MEDIA_RTC_LIVE_ADAPTOR_SINK_H__

