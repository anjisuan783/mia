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
    virtual srs_error_t Listen(const MediaAddress& addr) = 0;
    virtual srs_error_t Stop() = 0;
  };
 
  MediaListenerMgr();
  srs_error_t Init(const std::vector<std::string>& addr);
  void Close();

 private:
  IMediaListener* CreateListener(std::string_view);
 private:
  MediaThread* worker_ = nullptr;
  std::vector<std::unique_ptr<IMediaListener>> listeners_;
};

} //namespace ma

#endif //__MEDIA_LISTENER_H__

