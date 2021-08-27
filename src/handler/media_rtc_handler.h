#ifndef __MEDIA_RTC_HANDLER_H__
#define __MEDIA_RTC_HANDLER_H__

#include "handler/h/media_handler.h"
#include "common/srs_kernel_error.h"

namespace ma {

class MediaHttpRtcServeMux final : public IMediaHttpHandler{
 public:
  MediaHttpRtcServeMux();
  virtual ~MediaHttpRtcServeMux();

  srs_error_t serve_http(IHttpResponseWriter*, ISrsHttpMessage*) override;

  void conn_destroy(std::shared_ptr<IMediaConnection>) override;

 private:
  //std::unique_ptr<wa::rtc_api> api_;
};

}

#endif //!__MEDIA_RTC_HANDLER_H__

