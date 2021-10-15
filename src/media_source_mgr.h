//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __NEW_MEDIA_SOURCE_MGR_H__
#define __NEW_MEDIA_SOURCE_MGR_H__

#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <optional>

#include "h/rtc_stack_api.h"
#include "utils/Worker.h"
#include "media_source.h"

namespace ma {

class MediaSourceMgr {
 public:
  int Init(unsigned int);
  
  std::shared_ptr<MediaSource> 
      FetchOrCreateSource(MediaSource::Config& cfg,
                          std::shared_ptr<MediaRequest> req);

  std::optional<std::shared_ptr<MediaSource>> 
      FetchSource(const std::string& stream_id);

  void RemoveSource(const std::string& stream_id);

  std::shared_ptr<wa::Worker> GetWorker();
 private:
  std::mutex source_lock_;
  std::map<std::string, std::shared_ptr<MediaSource>> sources_;
  std::unique_ptr<wa::rtc_api> rtc_api_;
};

extern MediaSourceMgr g_source_mgr_;

} //namespace ma

#endif //!__NEW_MEDIA_SOURCE_MGR_H__

