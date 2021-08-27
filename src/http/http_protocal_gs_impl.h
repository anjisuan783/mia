#ifdef __GS__

#ifndef __HTTP_PROTOCAL_GS_IMPL_H__
#define __HTTP_PROTOCAL_GS_IMPL_H__

#include <string>

#include "http/h/http_protocal.h"

#include "tpapi.h"
#include "httpapi.h"
#include "refctl.h"
#include "datapackage.h"

#include "common/media_log.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"

namespace ma {

class GsHttpMessageParser final : public IHttpMessageParser {
 public:
  GsHttpMessageParser(IHttpServer* p);
  
  ~GsHttpMessageParser() = default;

  std::optional<ISrsHttpMessage*> parse_message(const std::string& body) override;

 private:
  CSmartPointer<IHttpServer> conn_;
};

class GsHttpRequestReader final : public IHttpRequestReader,
                                  public ITransportSink {
 public:
  GsHttpRequestReader(IHttpServer* conn, 
      CDataPackage* msg, CallBack* callback);

  ~GsHttpRequestReader();

  //IHttpRequestReader implement
  void open() override;
  void disconnect() override;

  //ITransportSink implement
  void OnReceive(CDataPackage& inData, ITransport* inTransport) override;
  void OnSend(ITransport* inTransport)  override;
  void OnDisconnect(OsResult inReason, ITransport* inTransport)  override;

 private:
  CSmartPointer<IHttpServer> conn_;
  CDataPackageSmartPoint req_;

  CallBack* callback_;
};

class SrsFileWriter;

class GsHttpResponseWriter final : public IHttpResponseWriter
{
 public:
  GsHttpResponseWriter(IHttpServer* p, bool flag_stream);
  
  virtual ~GsHttpResponseWriter() = default;

  srs_error_t final_request() override;
  
  SrsHttpHeader* header() override;

  srs_error_t write(const char* data, int size) override;

  srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) override;

  void write_header(int code) override;

 private:
  srs_error_t send_header(const char*);
private:
  CSmartPointer<IHttpServer> conn_;

  SrsHttpHeader header_;

  bool header_sent_{false};

  bool header_wrote_{false};

  // The number of bytes written in body
  int64_t written_{0};

  // The explicitly-declared Content-Length; or -1
  int64_t content_length_{-1};

  int status_;

  bool json_{true};

  static constexpr int SRS_HTTP_HEADER_CACHE_SIZE{64};
  char header_cache_[SRS_HTTP_HEADER_CACHE_SIZE];

#ifdef __DUMP_PEER_STREAM__
  std::unique_ptr<SrsFileWriter> file_dump_;
#endif
};

}
#endif //!__HTTP_PROTOCAL_GS_IMPL_H__

#endif //__GS__

