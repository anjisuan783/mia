//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_CONNECTION_MANAGER_H__
#define __MEDIA_CONNECTION_MANAGER_H__

#include <memory>
#include <map>
#include <mutex>
#include <vector>
#include <string>

#include "utils/sigslot.h"
#include "common/media_kernel_error.h"

namespace ma {

class IMediaConnection;
class IMediaIOBaseFactory;
class MediaListenerMgr;

class MediaConnMgr {

 public:
  enum ConnType {
    e_unknow,
    e_http,
    e_flv,
    e_rtmp
  };

  srs_error_t Init(uint32_t, const std::vector<std::string>& addr);

  void Close();
 
  std::shared_ptr<IMediaConnection>CreateConnection(ConnType, 
      std::unique_ptr<IMediaIOBaseFactory> factory);
      
  void RemoveConnection(std::shared_ptr<IMediaConnection> p);

 public:
  sigslot::signal1<std::shared_ptr<IMediaConnection>> signal_destroy_conn_;
 private: 
  std::mutex source_lock_;
  std::map<IMediaConnection*, std::shared_ptr<IMediaConnection>> connections_;

  std::unique_ptr<MediaListenerMgr> listener_;
};

extern MediaConnMgr g_conn_mgr_;

}

#endif //!__MEDIA_CONNECTION_MANAGER_H__

