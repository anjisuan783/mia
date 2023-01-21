//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_LISTENER_H__
#define __MEDIA_LISTENER_H__

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "utils/sigslot.h"
#include "rtc_base/thread.h"
#include "api/packet_socket_factory.h"
#include "rtc_base/async_packet_socket.h"
#include "common/media_kernel_error.h"

namespace ma {

class MediaAddress;
class MediaThread;

class MediaListenerMgr {
 public:
  //interface declaration
  class IMediaListener {
   public:
    virtual ~IMediaListener() = default;
    virtual int Listen(const MediaAddress& addr, 
                       rtc::PacketSocketFactory*) = 0;
    virtual void Stop() = 0;
    //virtual void OnNewConnectionEvent(
    //  rtc::AsyncPacketSocket*, rtc::AsyncPacketSocket*);
  };
 
  MediaListenerMgr();
  srs_error_t Init(const std::vector<std::string>& addr);
  void Close();

 private:
  std::unique_ptr<MediaListenerMgr::IMediaListener> 
      CreateListener(std::string_view);
 private:
  std::unique_ptr<rtc::Thread> worker_;
  MediaThread* worker1_ = nullptr;
  std::unique_ptr<rtc::PacketSocketFactory> socket_factory_;
  std::vector<std::unique_ptr<IMediaListener>> listeners_;
};

} //namespace ma

#endif //__MEDIA_LISTENER_H__

