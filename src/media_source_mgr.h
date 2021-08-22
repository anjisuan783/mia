#ifndef __NEW_MEDIA_SOURCE_MGR_H__
#define __NEW_MEDIA_SOURCE_MGR_H__

#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <optional>

namespace ma {

class MediaSource;
class IMediaConfBridge;

class MediaSourceMgr
{
 public:
  void Init(unsigned int);
  
  std::optional<std::shared_ptr<MediaSource>> 
      FetchOrCreateSource(const std::string& id);
  
  void removeSource(const std::string& id);
 private:
  std::mutex source_lock_;
  std::map<std::string, std::shared_ptr<MediaSource>> sources_;
};

extern MediaSourceMgr g_source_mgr_;
}
#endif //!__NEW_MEDIA_SOURCE_MGR_H__

