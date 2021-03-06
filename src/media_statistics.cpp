#include "media_statistics.h"

#include <time.h>
#include <assert.h>
#include <algorithm>

#include "rtmp/media_req.h"
#include "utils/json.h"

namespace ma {

bool ClientTypeIsPublish(ClientType t) {

  return t == TRtcPublish;
}

std::string ClienType2String(ClientType t) {
  switch (t) {
    case TRtcPublish: 
      return "rtc-publish";
    case TRtcPlay: 
      return "rtc-play";
    case TRtmpPublish: 
      return "flash-publish";
    case TRtmpPlay: 
      return "flash-play";
    default:
      return "Unknown";
  }
}

//////////////////////////////////////////////////////////////////////////////////ClientInfo
////////////////////////////////////////////////////////////////////////////////
void MediaStatistics::ClientInfo::Dump(json::Object& obj) {
  obj["id"] = id;
  obj["ip"] = req->ip;
  obj["pageUrl"] = req->pageUrl.c_str();
  obj["swfUrl"] = req->swfUrl.c_str();
  obj["tcUrl"] = req->tcUrl.c_str();
  obj["url"] = req->get_stream_url();
  obj["type"] = ClienType2String(type);
  obj["publish"] = ClientTypeIsPublish(type);
  time_t tm_sec = time(nullptr) - created;

  int hours = tm_sec / 3600;
  int sec = tm_sec % 3600;
  int min = sec / 60;
  sec %= 60;
  
  char buf[256];
  snprintf(buf, 256, "%d:%d:%d", hours, min, sec);
  obj["alive"] = std::string(buf);
}

//////////////////////////////////////////////////////////////////////////////////StreamInfo
////////////////////////////////////////////////////////////////////////////////
void MediaStatistics::StreamInfo::Dump(json::Object& out_obj) {
  out_obj["id"] = req->get_stream_url();
  
  out_obj["created"] = created_string;

  if (publisher) {
    json::Object client_info;
    publisher->Dump(client_info);
    out_obj["publisher"] = client_info;
  }

  if (!players.empty()) {
    json::Array array;
    for(auto& i : players) {
      json::Object client_info;
      i.second->Dump(client_info);
      array.push_back(client_info);
    }
    out_obj["player"] = array;
  }
}

void  MediaStatistics::StreamInfo::OnClient(std::shared_ptr<ClientInfo> c) {
  bool isPublisher = false;
  if (TRtmpPublish == c->type || TRtcPublish == c->type)
    isPublisher = true;
  
  if (isPublisher) {
    publisher = std::move(c);
  } else {
    players.emplace(c->id, std::move(c));
  }
}

//////////////////////////////////////////////////////////////////////////////////MediaStatistics
////////////////////////////////////////////////////////////////////////////////
void MediaStatistics::OnStream(std::shared_ptr<MediaRequest> req) {
  StreamInfo* pStream = nullptr;
  std::string id = req->get_stream_url();
  std::lock_guard<std::mutex> guard(client_lock_);
  auto found = streams_.find(id);
  if (found == streams_.end()) {
    auto stream = std::make_shared<StreamInfo>();
    pStream = stream.get();
    streams_.emplace(id, std::move(stream));
  } else {
    pStream = found->second.get();
  }

  pStream->req = std::move(req);
  pStream->created = time(nullptr);
  
  struct tm now_time;
  localtime_r(&(pStream->created), &now_time);
  char buf[256];
  snprintf(buf, 256, "%d-%d %d:%d:%d", now_time.tm_mon, now_time.tm_mday
      , now_time.tm_hour, now_time.tm_min, now_time.tm_sec);

  pStream->created_string = buf;
}

void MediaStatistics::OnStreamClose(std::shared_ptr<MediaRequest> req) {
  std::string id = req->get_stream_url();
  std::lock_guard<std::mutex> guard(client_lock_);
  streams_.erase(id);
}

void MediaStatistics::OnClient(const std::string& id, 
                               std::shared_ptr<MediaRequest> req, 
                               ClientType t) {
  ClientInfo* pclient = nullptr;
  std::string stream_url = req->get_stream_url();

  std::optional<std::shared_ptr<ClientInfo>> new_client;
  std::lock_guard<std::mutex> guard(client_lock_);
  auto found = clients_.find(id);
  if (found == clients_.end()) {
    auto client = std::make_shared<ClientInfo>();
    pclient = client.get();
    client->id = id;
    clients_.emplace(id, client);
    new_client = client;
  } else {
    pclient = found->second.get();
  }

  pclient->type = t;
  pclient->req = std::move(req);
  pclient->created = time(nullptr);

  if (new_client) {
    auto stream_found = streams_.find(stream_url);
    if (stream_found != streams_.end()) {
      stream_found->second->OnClient(*new_client);
    } else {
      assert(false);
    }
  }
}

void MediaStatistics::OnDisconnect(const std::string& id) {
  std::lock_guard<std::mutex> guard(client_lock_);
  auto found = clients_.find(id);
  if (found == clients_.end())
    return ;

  auto pInfo = std::move(found->second);
  clients_.erase(id);
  auto stream = streams_.find(pInfo->req->get_stream_url());
  if (stream == streams_.end())
    return ;

  StreamInfo* pStream = stream->second.get();

  if (pStream->publisher && pStream->publisher->id == id) {
    pStream->publisher = nullptr;
    return;
  }

  if (pStream->players.empty())
    return ;

  pStream->players.erase(id);
}

bool MediaStatistics::DumpClients(json::Object& obj, int start, int count) {
  std::vector<std::shared_ptr<ClientInfo>> clients_copy;
  {
    std::lock_guard<std::mutex> guard(client_lock_);
    if (clients_.empty())
      return false;

    clients_copy.reserve(clients_.size());
    std::for_each(clients_.begin(), clients_.end(), [&clients_copy](auto& x) {
      clients_copy.emplace_back(x.second);
    });
  }

  json::Array cli_jsons;
  int total = (int)clients_copy.size();
  for (int i = 0; i < start + count && i < total; ++i) {
    if (i < start) {
      continue;
    }
    json::Object cli_info;
    clients_copy[i]->Dump(cli_info);
    cli_jsons.push_back(cli_info);
  }

  obj["clients"] = cli_jsons;

  return true;
}

bool MediaStatistics::DumpStreams(json::Object& obj, int start, int count) {
  std::vector<std::shared_ptr<StreamInfo>> streams_copy;
  {
    std::lock_guard<std::mutex> guard(client_lock_);
    if (streams_.empty())
      return false;

    streams_copy.reserve(streams_.size());
    std::for_each(streams_.begin(), streams_.end(), [&streams_copy](auto& x) {
      streams_copy.emplace_back(x.second);
    });
  }

  json::Array stream_jsons;
  int total = (int)streams_copy.size();
  for (int i = 0; i < start + count && i < total; ++i) {
    if (i < start) {
      continue;
    }
    
    StreamInfo* stream = streams_copy[i].get();
    json::Object stream_info;
    stream->Dump(stream_info);
    stream_jsons.push_back(stream_info);
  }
  obj["streams"] = stream_jsons;

  return true;
}

size_t MediaStatistics::Clients() {
  std::lock_guard<std::mutex> guard(client_lock_);
  return clients_.size();
}

size_t MediaStatistics::Streams() {
  std::lock_guard<std::mutex> guard(client_lock_);
  return streams_.size();
}

MediaStatistics g_statistics;

MediaStatistics& Stat() {
  return g_statistics;
}

} //namespace ma
