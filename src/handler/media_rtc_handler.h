//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTC_HANDLER_H__
#define __MEDIA_RTC_HANDLER_H__

#include "h/rtc_stack_api.h"

#include "handler/h/media_handler.h"
#include "common/media_kernel_error.h"

namespace ma {

class MediaHttpRtcServeMux final : public IMediaHttpHandler{
 public:
  MediaHttpRtcServeMux();
  ~MediaHttpRtcServeMux() override;

  srs_error_t init() override;

  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                         std::shared_ptr<ISrsHttpMessage>) override;

  void conn_destroy(std::shared_ptr<IMediaConnection>) override;

  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                            std::shared_ptr<MediaRequest> r) override;
  
  void unmount_service(std::shared_ptr<MediaSource> s, 
                       std::shared_ptr<MediaRequest> r) override;

 private:
  std::unique_ptr<wa::rtc_api> api_;
  std::vector<std::unique_ptr<IMediaHttpHandler>> handlers_;
};

}

#endif //!__MEDIA_RTC_HANDLER_H__

