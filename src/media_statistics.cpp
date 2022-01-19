#include "media_statistics.h"

#include <time.h>

#include "rtmp/media_req.h"

namespace ma {

bool ClientTypeIsPublish(ClientType t) {
  return t == TRtcPublish;
}

std::string ClienType2String(ClientType t) {
  switch (t) {
    case TRtcPublish: 
      return "rtmp-play";
    case TRtcPlay: 
      return "rtc-play";
    case TRtmpPlay: 
      return "flash-publish";
    default: 
      return "Unknown";
  }
}

void MediaStatistics::StatisticsClient::Dump(json::Object& obj) {
  obj["id"] = id;
  obj["ip"] = req->ip;
  obj["pageUrl"] = req->pageUrl.c_str();
  obj["swfUrl"] = req->swfUrl.c_str();
  obj["tcUrl"] = req->tcUrl.c_str();
  obj["url"] = req->get_stream_url();
  obj["type"] = ClienType2String(type);
  obj["publish"] = ClientTypeIsPublish(type);
  obj["alive"] = (int)(time(nullptr) - created);
}

void MediaStatistics::OnClient(const std::string& id, 
                               std::shared_ptr<MediaRequest> req, 
                               ClientType t) {
  StatisticsClient* pclient = nullptr;

  std::lock_guard<std::mutex> guard(client_lock_);
  auto found = clients_.find(id);
  if (found == clients_.end()) {
    auto client = std::make_unique<StatisticsClient>();
    pclient = client.get();
    client->id = id;
    clients_.emplace(id, std::move(client));
  } else {
    pclient = clients_[id].get();
  }

  pclient->type = t;
  pclient->req = std::move(req);
  pclient->created = time(nullptr);
}

void MediaStatistics::OnDisconnect(const std::string& id) {
  std::lock_guard<std::mutex> guard(client_lock_);
  clients_.erase(id);
}

void MediaStatistics::DumpClient(json::Object& obj, int start, int count) {
  json::Array cli_jsons;
  obj["clients"] = cli_jsons;
  std::lock_guard<std::mutex> guard(client_lock_);

  auto it = clients_.begin();
  for (int i = 0; i < start + count && it != clients_.end(); ++it++, ++i) {
    if (i < start) {
      continue;
    }
    
    StatisticsClient* client = it->second.get();
    
    json::Object cli_json;
    client->Dump(cli_json);
    cli_jsons.push_back(cli_json);
  }
}

size_t MediaStatistics::Clients() {
  std::lock_guard<std::mutex> guard(client_lock_);
  return clients_.size();
}

MediaStatistics g_statistics;

MediaStatistics& Stat() {
  return g_statistics;
}

}

