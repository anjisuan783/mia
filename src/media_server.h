//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_SERVER_H__
#define __MEDIA_SERVER_H__

#include <memory>

#include "rtc_base/logging.h"
#include "common/media_log.h"
#include "h/media_server_api.h"
#include "common/media_kernel_error.h"

namespace ma {

class IMediaHttpHandler;
class MediaSource;
class MediaRequest;

class MediaServerImp final : public MediaServerApi,
                             public rtc::LogSink {
  MDECLARE_LOGGER();
  friend class MediaConnMgr;
 public:
  MediaServerImp() = default;
  ~MediaServerImp() override = default;

  int Init(const Config&) override;

  srs_error_t OnPublish(std::shared_ptr<MediaSource> s, 
                        std::shared_ptr<MediaRequest> r);
  void OnUnpublish(std::shared_ptr<MediaSource> s, 
                   std::shared_ptr<MediaRequest> r);

 private:
  //rtc::LogSink implement
  void OnLogMessage(const std::string& message) override;

 public:
  Config config_;
  
 private:
  std::unique_ptr<IMediaHttpHandler> mux_;
  bool inited_{false};
};

extern MediaServerImp g_server_;

} //namespace ma

#endif //!__MEDIA_SERVER_H__

