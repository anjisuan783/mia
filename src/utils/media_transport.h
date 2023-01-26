#ifndef __MEDIA_TRANSPORT_H__
#define __MEDIA_TRANSPORT_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "common/media_define.h"
#include "utils/media_addr.h"

namespace ma {

class MessageChain;
class MediaThread;

class TransportSink {
 public:
	virtual int OnRead(MessageChain &msg) = 0;
	virtual int OnWrite() = 0;
	virtual int OnClose(srs_error_t reason) = 0;

protected:
	virtual ~TransportSink() = default;
};

class Transport {
 public:
  virtual srs_error_t Open(TransportSink *sink) = 0;
  virtual int Close() = 0;
  virtual TransportSink* GetSink() = 0;

  virtual int Write(MessageChain& msg, bool destroy = false) = 0;
  virtual int SetOpt(int cmd, void* args) = 0;
  virtual int GetOpt(int cmd, void* args) const = 0;
  
  virtual void SetSocketHandler(MEDIA_HANDLE handler) = 0;
  virtual MediaAddress GetLocalAddr() = 0;
  virtual MediaAddress GetPeerAddr() = 0;
 protected:
  virtual ~Transport() = default;
};

class TransportFactory {
 public:
  static std::shared_ptr<Transport> CreateTransport(MediaThread*, bool tcp);
};

std::string GetSystemErrorInfo(int inErrno);

} //namespace ma

#endif //!__MEDIA_TRANSPORT_H__
