#ifndef __GS_HTTP_CONN_H__
#define __GS_HTTP_CONN_H__

#include<memory>

#include "common/srs_kernel_error.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"

namespace ma {

class IGsHttpHandler;
class GsHttpCorsMux;

class GsHttpConn : public IMediaConnection,
                   public IHttpRequestReader::CallBack{
 public:
  GsHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, IGsHttpHandler* m);
  GsHttpConn() = default;
  virtual ~GsHttpConn();

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
  IGsHttpHandler* http_mux_;
  std::unique_ptr<GsHttpCorsMux> cors_;
};

class GsResponseOnlyHttpConn : public GsHttpConn {
 public:
  GsResponseOnlyHttpConn(std::unique_ptr<IHttpProtocalFactory> fac, IGsHttpHandler* m);
  ~GsResponseOnlyHttpConn();
};

}
#endif //!__GS_HTTP_CONN_H__

