//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_SERVER_HANDLER_H__
#define __MEDIA_SERVER_HANDLER_H__

#include <memory>

#include "common/media_kernel_error.h"

namespace ma {

#define RTC_PALY_PREFIX      "/rtc/v1/play/"
#define RTC_PUBLISH_PREFIX   "/rtc/v1/publish/"
#define LIVE_PUBLISH_PREFIX  "/live/v1/publish/"
#define LIVE_PLAY_PREFIX     "/live/v1/play/"

class ISrsHttpMessage;
class IHttpResponseWriter;
class IMediaConnection;
class MediaSource;
class MediaRequest;

class IMediaHttpHandler {
 public:
  IMediaHttpHandler() = default;
  virtual ~IMediaHttpHandler() = default;

  virtual srs_error_t init() {
    return srs_success;
  }

  virtual srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                                 std::shared_ptr<ISrsHttpMessage>) = 0;

  virtual srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                                    std::shared_ptr<MediaRequest> r)  {
    return srs_success;
  }
  
  virtual void unmount_service(std::shared_ptr<MediaSource> s, 
                               std::shared_ptr<MediaRequest> r) {
  }

  virtual void conn_destroy(std::shared_ptr<IMediaConnection>) = 0;
};

class ServerHandlerFactor {
 public:
  ServerHandlerFactor() = default;

  std::unique_ptr<IMediaHttpHandler> Create();
};

}
#endif //!__MEDIA_SERVER_HANDLER_H__

