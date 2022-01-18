//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_STATISTICS_H__
#define __MEDIA_STATISTICS_H__

#include <mutex>
#include <unordered_map>
#include <memory>

#include "utils/json.h"

namespace ma {

enum ClientType {
  TRtcPublish,
  TRtcPlay,
  TRtmpPlay
};

class MediaRequest;

class MediaStatistics final {
 public:
  MediaStatistics() = default;
  ~MediaStatistics() = default;

  void OnClient(const std::string& client_id, 
                std::shared_ptr<MediaRequest>, 
                ClientType);
  void OnDisconnect(const std::string& client_id);
  void DumpClient(json::Object& arr, int start, int count);

  size_t Clients();
 private:
  struct StatisticsClient {
    std::string id;
    ClientType type;
    std::shared_ptr<MediaRequest> req;
    time_t created;
    void Dump(json::Object&);
  };

  std::mutex client_lock_;
  std::unordered_map<std::string, std::unique_ptr<StatisticsClient>> clients_;
};

MediaStatistics& Stat();

};

#endif //!__MEDIA_STATISTICS_H__

