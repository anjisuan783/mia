#ifndef __HTTP_PROTOCAL_INTERFACE__H__
#define __HTTP_PROTOCAL_INTERFACE__H__

#include <sys/uio.h>
#include <optional>
#include <memory>
#include "common/srs_kernel_error.h"

namespace ma {

class SrsHttpHeader;
class ISrsHttpMessage;

class IHttpRequestReader
{
public:
  class CallBack {
   public:
    virtual ~CallBack() = default;

    virtual srs_error_t process_request(const std::string&) = 0;
    virtual void on_disconnect() = 0;
  };

  virtual ~IHttpRequestReader() = default;

  virtual void open() = 0;
  virtual void disconnect() = 0;
};

class IHttpResponseWriter
{
public:
  virtual ~IHttpResponseWriter() = default;
public:
  virtual srs_error_t final_request() = 0;
  
  virtual SrsHttpHeader* header() = 0;

  virtual srs_error_t write(const char* data, int size) = 0;

  virtual srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) = 0;

  virtual void write_header(int code) = 0;
};

class IHttpMessageParser {
 public:
  virtual ~IHttpMessageParser() = default;
  // always parse a http message,
  // that is, the *ppmsg always NOT-NULL when return success.
  // or error and *ppmsg must be NULL.
  // @remark, if success, *ppmsg always NOT-NULL, *ppmsg always is_complete().
  // @remark user must free the ppmsg if not NULL.
  virtual std::optional<ISrsHttpMessage*> parse_message(const std::string&) = 0;
};


class IHttpProtocalFactory {
  public:
   virtual ~IHttpProtocalFactory() = default;
   
   virtual std::unique_ptr<IHttpRequestReader> 
      CreateReader(IHttpRequestReader::CallBack*) = 0;

   virtual std::unique_ptr<IHttpResponseWriter> CreateWriter(bool flag_stream) = 0;

   virtual std::unique_ptr<IHttpMessageParser> CreateParser() = 0;
};

std::unique_ptr<IHttpProtocalFactory> CreateDefaultHttpProtocalFactory(void*, void*);

}

#endif //!__HTTP_PROTOCAL_INTERFACE__H__
