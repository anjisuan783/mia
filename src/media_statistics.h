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
  TRtmpPublish,
  TRtmpPlay
};

class MediaRequest;

class MediaStatistics final {

 public:
  MediaStatistics() = default;
  ~MediaStatistics() = default;

  void OnStream(std::shared_ptr<MediaRequest>);
  void OnStreamClose(std::shared_ptr<MediaRequest>);

  void OnClient(const std::string& client_id, 
                std::shared_ptr<MediaRequest>, 
                ClientType);
  void OnDisconnect(const std::string& client_id);
  void DumpClients(json::Object& objs, int start, int count);
  void DumpStreams(json::Object& objs, int start, int count);

  size_t Clients();
  size_t Streams();
 private:
  struct ClientInfo {
    std::string id;
    ClientType type;
    std::shared_ptr<MediaRequest> req;
    time_t created;
    void Dump(json::Object&);
  };

  struct StreamInfo {
    std::shared_ptr<MediaRequest> req;
    std::shared_ptr<ClientInfo> publisher;
    std::vector<std::shared_ptr<ClientInfo>> players;
    time_t created;
    void OnClient(std::shared_ptr<ClientInfo>);
    void Dump(json::Object&);
  };

  std::mutex client_lock_;
  std::unordered_map<std::string, std::shared_ptr<ClientInfo>> clients_;
  std::unordered_map<std::string, std::shared_ptr<StreamInfo>> streams_;
};

MediaStatistics& Stat();

};

#endif //!__MEDIA_STATISTICS_H__

