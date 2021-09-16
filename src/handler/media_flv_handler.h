//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_FLV_SERVER_HANDLER_H__
#define __MEDIA_FLV_SERVER_HANDLER_H__

#include <memory>
#include <map>
#include <mutex>

#include "common/media_log.h"
#include "encoder/media_flv_encoder.h"
#include "handler/h/media_handler.h"

namespace ma {

class MediaConsumer;
class MediaSource;
class IMediaConnection;
class ISrsHttpMessage;
class StreamEntry;

class MediaFlvPlayHandler : public IMediaHttpHandler {

  MDECLARE_LOGGER();

 public:
  MediaFlvPlayHandler();
  ~MediaFlvPlayHandler() override;
  
  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                            std::shared_ptr<MediaRequest> r) override;

 void unmount_service(std::shared_ptr<MediaSource> s, 
                      std::shared_ptr<MediaRequest> r) override;
  
 private:
  void conn_destroy(std::shared_ptr<IMediaConnection> conn) override;
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter> writer, 
                         std::shared_ptr<ISrsHttpMessage> msg) override;

 private:
  std::mutex stream_lock_;
  std::map<std::string, std::shared_ptr<StreamEntry>> steams_; //need lock

  std::mutex index_lock_;
  std::map<IMediaConnection*, std::shared_ptr<StreamEntry>> index_;
};

}

#endif //!__MEDIA_FLV_SERVER_HANDLER_H__

