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

#include "rtc_base/thread.h"
#include "api/packet_socket_factory.h"
#include "rtc_base/async_packet_socket.h"

namespace ma {

class MediaListenerMgr {
 public:
  //interface declaration
  class IMediaListener : public sigslot::has_slots<>{
   public:
    virtual ~IMediaListener() = default;
    virtual int Listen(const rtc::SocketAddress&, 
                       rtc::PacketSocketFactory*) = 0;
    virtual void OnNewConnectionEvent(
      rtc::AsyncPacketSocket*, rtc::AsyncPacketSocket*);
   protected:
    virtual int GetSocketType();  
   protected:
    std::unique_ptr<rtc::AsyncPacketSocket> listen_socket_;
  };
 
  MediaListenerMgr();
  int Init(const std::vector<std::string>& addr);

 private:
  std::unique_ptr<MediaListenerMgr::IMediaListener> 
      CreateListener(std::string_view);
 private:
  std::unique_ptr<rtc::Thread> worker_;
  std::unique_ptr<rtc::PacketSocketFactory> socket_factory_;
  std::vector<std::unique_ptr<IMediaListener>> listeners_;
};

} //namespace ma

#endif //__MEDIA_LISTENER_H__

