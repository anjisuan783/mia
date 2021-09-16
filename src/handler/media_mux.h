//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

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

class MediaHttpServeMux final: public IMediaHttpHandler,
                            public sigslot::has_slots<> {
public:
  MediaHttpServeMux();
  ~MediaHttpServeMux();

  srs_error_t init() override;
  
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter> s, 
                         std::shared_ptr<ISrsHttpMessage> r) override;

  srs_error_t mount_service(std::shared_ptr<MediaSource> s,   
                            std::shared_ptr<MediaRequest> r) override;
  void unmount_service(std::shared_ptr<MediaSource> s, 
                       std::shared_ptr<MediaRequest> r) override;

  void conn_destroy(std::shared_ptr<IMediaConnection>) override;
private:
  std::unique_ptr<IMediaHttpHandler> flv_sevice_;
  std::unique_ptr<IMediaHttpHandler> rtc_sevice_;
};

}

#endif //!__RTC_SERVER_API_H__

