#ifndef __MEDIA_TRANSPORT_H__
#define __MEDIA_TRANSPORT_H__

#include "common/media_kernel_error.h"

namespace ma {

class MessageChain;

class TransportSink {
 public:
	virtual void OnRecv(MessageChain &aData) = 0;
	virtual void OnSend() = 0;
	virtual void OnDisconnect(srs_error_t reason) = 0;

protected:
	virtual ~TransportSink() = default;
};

class Transport {
 public:
  virtual srs_error_t Open(TransportSink *sink) = 0;
  virtual TransportSink* GetSink() = 0;

  virtual int Send(MessageChain& msg, bool destroy = false) = 0;
  virtual int SetOpt(int cmd, void* args) = 0;
  virtual int GetOpt(int cmd, void* args) const = 0;
  virtual int Disconnect(int reason) = 0;
 protected:
  virtual ~Transport() = default;
};

} //namespace ma

#endif //!__MEDIA_TRANSPORT_H__
