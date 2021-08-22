#ifndef __RTC_SERVER_API_H__
#define __RTC_SERVER_API_H__

#include <string>
#include <map>
#include <memory>

#include "utils/sigslot.h"
#include "handler/h/media_handler.h"

namespace ma {

class IHttpResponseWriter;
class ISrsHttpMessage;
class IMediaConnection;

class GsHttpServeMux final: public IGsHttpHandler,
                            public sigslot::has_slots<> {
public:
  GsHttpServeMux();
  ~GsHttpServeMux();
  
  srs_error_t serve_http(IHttpResponseWriter*, ISrsHttpMessage*) override;

  srs_error_t mount_service(std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) override;
  void unmount_service(std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) override;

  void conn_destroy(std::shared_ptr<IMediaConnection>) override;
private:
  std::map<std::string, IGsHttpHandler*> entry_;

  std::unique_ptr<IGsHttpHandler> flv_sevice_;
};

}

#endif //!__RTC_SERVER_API_H__

