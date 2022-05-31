#ifndef __MEDIA_IOafds_H__
#define __MEDIA_IOafds_H__

#include <memory>

#include "common/media_kernel_error.h"
#include "utils/media_msg_chain.h"
#include "utils/sigslot.h"
#include "rtc_base/async_packet_socket.h"

namespace ma {

class IMediaIO {
 public:
  virtual ~IMediaIO() = default;

  virtual void Open() = 0;
  virtual void Close() = 0;

  virtual srs_error_t Write(MessageChain* data, int* sent) = 0;

  sigslot::signal1<MessageChain*> SignalOnRead_;
  sigslot::signal0<> SignalOnWrite_;
  sigslot::signal1<int> SignalOnDisct_;
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
CreateTcpIOFactory(rtc::AsyncPacketSocket*s , rtc::AsyncPacketSocket* c);

} //namespace ma

#endif //!__MEDIA_IOafds_H__
