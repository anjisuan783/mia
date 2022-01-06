//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MA_HTTP_CONN_H__
#define __MA_HTTP_CONN_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"

namespace ma {

class IMediaHttpHandler;
class MediaHttpCorsMux;

class MediaHttpConn : public IMediaConnection,
                   public IHttpRequestReader::CallBack{
 public:
  MediaHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, IMediaHttpHandler* m);
  MediaHttpConn() = default;
  virtual ~MediaHttpConn();

  void Start() override;

  void Disconnect() override;

  srs_error_t set_crossdomain_enabled(bool v);
 private:
  srs_error_t process_request(std::string_view) override;
  void on_disconnect() override;
 
 protected:
  std::shared_ptr<IHttpRequestReader>  reader_;
  std::unique_ptr<IHttpMessageParser>  parser_;
  IMediaHttpHandler* http_mux_;
  std::unique_ptr<MediaHttpCorsMux> cors_;
  std::unique_ptr<IHttpProtocalFactory> factory_;
};

class MediaResponseOnlyHttpConn : public MediaHttpConn {
 public:
  MediaResponseOnlyHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, IMediaHttpHandler* m);
  ~MediaResponseOnlyHttpConn() override = default;
 private:
  srs_error_t process_request(std::string_view) override;
};

}
#endif //!__MA_HTTP_CONN_H__

