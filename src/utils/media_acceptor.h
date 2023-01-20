#ifndef __MEDIA_ACCEPTOR_H__
#define __MEDIA_ACCEPTOR_H__

#include "common/media_kernel_error.h"

#include "utils/media_transport.h"
#include "utils/media_addr.h"

namespace ma {

class Transport;

class AcceptorSink  {
 public:
	virtual void OnAccept(std::shared_ptr<Transport>) = 0;

 protected:
	virtual ~AcceptorSink() = default;
};

class Acceptor {
 public:
	virtual srs_error_t StartListen(AcceptorSink* sink,
		                              const MediaAddress &addr) = 0;

	virtual srs_error_t StopListen() = 0;

 protected:
	virtual ~Acceptor() = default;
};

} //namespace ma

#endif //!__MEDIA_ACCEPTOR_H__
