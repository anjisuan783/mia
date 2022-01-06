//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_FILE_HANDLER_H__
#define __MEDIA_FILE_HANDLER_H__

#include <memory>
#include <unordered_map>
#include <mutex>

#include "utils/Worker.h"
#include "handler/h/media_handler.h"
#include "utils/sigslot.h"
#include "common/media_io.h"

namespace ma {

class IHttpResponseWriter;
class SrsFileReader;
class ISrsHttpMessage;

class MediaFileHandler : public IMediaHttpHandler {
 public:
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter>, 
                                 std::shared_ptr<ISrsHttpMessage>) override;

  srs_error_t mount_service(std::shared_ptr<MediaSource> s, 
                                    std::shared_ptr<MediaRequest> r) override {
    return nullptr;
  }
  
  void unmount_service(std::shared_ptr<MediaSource> s, 
                               std::shared_ptr<MediaRequest> r) override {}

  void conn_destroy(std::shared_ptr<IMediaConnection>) override;

  void on_write_event(IHttpResponseWriter*);
 private:
  srs_error_t serve_file(std::shared_ptr<IHttpResponseWriter> w, 
                         std::shared_ptr<ISrsHttpMessage> r, 
                         const std::string&  fullpath);
  
  srs_error_t copy(std::shared_ptr<IHttpResponseWriter> w, 
    std::unique_ptr<SrsFileReader> fs, 
    std::shared_ptr<ISrsHttpMessage> r, int size);

 private:
  struct doing_job : public sigslot::has_slots<>,
                     public std::enable_shared_from_this<doing_job> {
    doing_job() = default;
    std::unique_ptr<SrsFileReader> fs_;
    std::unique_ptr<char> buf_;
    std::shared_ptr<IHttpResponseWriter> w_;
    int buf_left_{0};
    int file_left_{0};
    std::shared_ptr<wa::Worker> worker_;
    void on_write_event(IHttpResponseWriter*);
  };

  std::mutex lock_;
  std::unordered_map<IMediaConnection*, std::shared_ptr<doing_job>> s_list_;

  // for test
  std::shared_ptr<wa::Worker> worker_;
};

} //namespace ma

#endif //!__MEDIA_FILE_HANDLER_H__
