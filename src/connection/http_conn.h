#ifndef __GS_HTTP_CONN_H__
#define __GS_HTTP_CONN_H__

#include<memory>

#include "common/media_log.h"
#include "common/srs_kernel_error.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"

namespace ma {

class IMediaHttpHandler;
class MediaHttpCorsMux;

class MediaHttpConn : public IMediaConnection,
                   public IHttpRequestReader::CallBack{
  MDECLARE_LOGGER();
                   
 public:
  MediaHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, IMediaHttpHandler* m);
  MediaHttpConn() = default;
  virtual ~MediaHttpConn();

  void Start() override;

  void Disconnect() override;

  srs_error_t set_crossdomain_enabled(bool v);
 private:
  srs_error_t process_request(const std::string& body) override;
  void on_disconnect() override;
 
 protected:
  std::unique_ptr<IHttpRequestReader>  reader_;
  std::unique_ptr<IHttpResponseWriter> writer_;
  std::unique_ptr<IHttpMessageParser>  parser_;
  IMediaHttpHandler* http_mux_;
  std::unique_ptr<MediaHttpCorsMux> cors_;
};

class MediaResponseOnlyHttpConn : public MediaHttpConn {
  MDECLARE_LOGGER();
 public:
  MediaResponseOnlyHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, IMediaHttpHandler* m);
  ~MediaResponseOnlyHttpConn();
};

}
#endif //!__GS_HTTP_CONN_H__

