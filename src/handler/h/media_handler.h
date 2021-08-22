#ifndef __MEDIA_SERVER_HANDLER_H__
#define __MEDIA_SERVER_HANDLER_H__

#include <memory>

#include "common/srs_kernel_error.h"

namespace ma {

#define RTC_PALY_PREFIX  "/rtc/v1/play/"
#define RTC_PUBLISH_PREFIX  "/rtc/v1/publish/"
#define LIVE_PUBLISH_PREFIX  "/live/v1/publish/"
#define LIVE_PLAY_PREFIX  "/live/v1/play/"

class ISrsHttpMessage;
class IHttpResponseWriter;
class IMediaConnection;
class MediaSource;
class MediaRequest;

class IGsHttpHandler {
public:
  IGsHttpHandler() = default;
  virtual ~IGsHttpHandler() = default;

  virtual srs_error_t serve_http(IHttpResponseWriter*, ISrsHttpMessage*) = 0;

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

  std::unique_ptr<IGsHttpHandler> Create();
};

}
#endif //!__MEDIA_SERVER_HANDLER_H__
