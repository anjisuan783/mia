//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "handler/media_mux.h"

#include "common/media_kernel_error.h"
#include "utils/json.h"
#include "connection/http_conn.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"
#include "connection/h/media_conn_mgr.h"
#include "rtmp/media_req.h"
#include "handler/media_live_handler.h"
#include "handler/media_rtc_handler.h"
#include "handler/media_file_handler.h"

namespace ma {

MediaHttpServeMux::MediaHttpServeMux() 
    : rtc_sevice_{new MediaHttpRtcServeMux},
      flv_sevice_{new MediaFlvPlayHandler},
      file_sevice_{new MediaFileHandler} {
  g_conn_mgr_.signal_destroy_conn_.connect(this, &MediaHttpServeMux::conn_destroy);
}

MediaHttpServeMux::~MediaHttpServeMux() = default;

srs_error_t MediaHttpServeMux::init() {
  return rtc_sevice_->init();
}

srs_error_t MediaHttpServeMux::serve_http( 
    std::shared_ptr<IHttpResponseWriter> writer, 
    std::shared_ptr<ISrsHttpMessage> msg) {
  
  std::string path = msg->path();

   if (path == RTC_PUBLISH_PREFIX || path == RTC_PALY_PREFIX) {
    return rtc_sevice_->serve_http(std::move(writer), std::move(msg));
  }

  if (path == HTTP_TEST) {
    return file_sevice_->serve_http(std::move(writer), std::move(msg));
  }

  return flv_sevice_->serve_http(std::move(writer), std::move(msg));
}

srs_error_t MediaHttpServeMux::mount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {
  srs_error_t err = srs_success;
  if ((err = rtc_sevice_->mount_service(s, r)) != srs_success) {
    return srs_error_wrap(err, "rtc mount service");
  }

  return flv_sevice_->mount_service(std::move(s), std::move(r));
}

void MediaHttpServeMux::unmount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {
  rtc_sevice_->unmount_service(s, r);
  flv_sevice_->unmount_service(std::move(s), std::move(r));
}

void MediaHttpServeMux::conn_destroy(std::shared_ptr<IMediaConnection> conn) {
  flv_sevice_->conn_destroy(conn);
  file_sevice_->conn_destroy(conn);
}

std::unique_ptr<IMediaHttpHandler> ServerHandlerFactor::Create() {
  return std::make_unique<MediaHttpServeMux>();
}

}
