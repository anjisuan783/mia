#ifndef __MEDIA_CONNECTION_MANAGER_H__
#define __MEDIA_CONNECTION_MANAGER_H__

#include <memory>
#include <map>
#include <mutex>

#include "utils/sigslot.h"

namespace ma {

class IMediaConnection;
class IHttpProtocalFactory;

class MediaConnMgr {
 public:
  enum ConnType {
    e_unknow,
    e_http,
    e_flv,
    e_rtmp
  };

  void Init(unsigned int num);
 
  std::shared_ptr<IMediaConnection>CreateConnection(ConnType, 
      std::unique_ptr<IHttpProtocalFactory> factory);
      
  void RemoveConnection(std::shared_ptr<IMediaConnection> p);

 public:
  sigslot::signal1<std::shared_ptr<IMediaConnection>> signal_destroy_conn_;
 private: 
  std::mutex source_lock_;
  std::map<IMediaConnection*, std::shared_ptr<IMediaConnection>> connections_;
};

extern MediaConnMgr g_conn_mgr_;

}

#endif //!__MEDIA_CONNECTION_MANAGER_H__

