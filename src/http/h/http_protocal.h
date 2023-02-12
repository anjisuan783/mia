#ifndef __HTTP_PROTOCAL_INTERFACE__H__
#define __HTTP_PROTOCAL_INTERFACE__H__

#include <sys/uio.h>
#include <optional>
#include <memory>

#include "utils/sigslot.h"
#include "common/media_kernel_error.h"
#include "connection/h/media_io.h"

namespace ma {

class SrsHttpHeader;
class ISrsHttpMessage;
class MessageChain;

class IHttpRequestReader {
public:
  class CallBack {
   public:
    virtual ~CallBack() = default;

    virtual srs_error_t process_request(std::string_view) = 0;
    virtual void on_disconnect() = 0;
  };

  virtual ~IHttpRequestReader() = default;

  virtual void open() = 0;
  virtual void disconnect() = 0;
  virtual std::string Ip() = 0;
};

class IHttpResponseWriter {
public:
  virtual ~IHttpResponseWriter() = default;
public:
  virtual void open() = 0;

  virtual srs_error_t final_request() = 0;
  
  virtual SrsHttpHeader* header() = 0;

  virtual srs_error_t write(const char* data, int size) = 0;

  virtual srs_error_t write(MessageChain* data, ssize_t* pnwrite) = 0;

  virtual void write_header(int code) = 0;

  // Used only for OnWriteEvent.
  sigslot::signal1<IHttpResponseWriter*> SignalOnWrite_;
};

enum http_parser_type { 
  HTTP_REQUEST, 
  HTTP_RESPONSE, 
  HTTP_BOTH 
};  

class IHttpMessageParser {
 public: 
  virtual ~IHttpMessageParser() = default;

  virtual void initialize(enum http_parser_type type) = 0;
  
  // Whether allow jsonp parser, which indicates the method in query string.
  virtual void set_jsonp(bool allow_jsonp) = 0;
  
  // parse a http message one by one
  // give you a message by out_msg, when the http header is parsed.
  // It'll continue parse the body util the body end.
  virtual srs_error_t parse_message(
      std::string_view, std::shared_ptr<ISrsHttpMessage>&) = 0;
};

class IHttpResponseReaderSink {
 public:
  virtual ~IHttpResponseReaderSink() = default;
  virtual void OnRead(char* buf, size_t size) = 0;
};

class IHttpResponseReader {
 public:
  virtual ~IHttpResponseReader() = default;

  virtual void open(IHttpResponseReaderSink*) = 0;
};

class IHttpProtocalFactory : public IMediaIOBaseFactory {
  public:
   ~IHttpProtocalFactory() override = default;
   
   virtual IHttpRequestReader* 
      CreateRequestReader(IHttpRequestReader::CallBack*) = 0;

   virtual std::shared_ptr<IHttpResponseWriter> 
      CreateResponseWriter(bool flag_stream) = 0;

   virtual IHttpMessageParser* CreateMessageParser() = 0;

   virtual IHttpResponseReader* CreateResponseReader() = 0;
};

std::unique_ptr<IMediaIOBaseFactory>
CreateHttpProtocalFactory(std::shared_ptr<Transport> t, bool tls);

}

#endif //!__HTTP_PROTOCAL_INTERFACE__H__

