#ifndef __MEDIA_IOafds_H__
#define __MEDIA_IOafds_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "utils/media_msg_chain.h"
#include "utils/media_transport.h"

namespace ma {

class IMediaIOSink;

class IMediaIO {
 public:
  virtual ~IMediaIO() = default;

  virtual void Open(IMediaIOSink*) = 0;
  virtual void Close() = 0;
  virtual std::string GetLocalAddress() const = 0;
  virtual std::string GetRemoteAddress() const = 0;
  virtual srs_error_t Write(MessageChain* data, int* sent) = 0;
};

class IMediaIOSink {
 public:
  virtual srs_error_t OnRead(MessageChain*) = 0;
  virtual srs_error_t OnWrite() = 0;
  virtual void OnClose(srs_error_t) = 0;

  virtual ~IMediaIOSink() = default;
};

class IMediaIOBaseFactory {
 public:
  virtual ~IMediaIOBaseFactory() = default;
};

class IMediaIOFactory : public IMediaIOBaseFactory {
 public:
  ~IMediaIOFactory() override = default;

  virtual std::shared_ptr<IMediaIO> CreateIO() = 0;
};

std::unique_ptr<IMediaIOBaseFactory>  
CreateTcpIOFactory(std::shared_ptr<Transport> t);

} //namespace ma

#endif //!__MEDIA_IOafds_H__
