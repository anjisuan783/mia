#include "media_statistics.h"

#include <time.h>
#include <assert.h>

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
  struct tm now_time;
  localtime_r(&tm_sec, &now_time);
  char buf[256];
  snprintf(buf, 256, "%dD:%dH:%dM:%dS", 
      now_time.tm_mday, now_time.tm_hour, now_time.tm_min, now_time.tm_sec);
  obj["alive"] = std::string(buf);
}

void MediaStatistics::StreamInfo::Dump(json::Object& out_obj) {
  out_obj["id"] = req->get_stream_url();
  time_t tm_sec = created;
  struct tm now_time;
  localtime_r(&tm_sec, &now_time);
  char buf[256];
  snprintf(buf, 256, "%dD:%dH:%dM:%dS", 
      now_time.tm_mday, now_time.tm_hour, now_time.tm_min, now_time.tm_sec);
  out_obj["created"] = std::string(buf);

  if (!players.empty()) {
    json::Array array;
    for(auto& i : players) {
      json::Object client_info;
      i->Dump(client_info);
      array.push_back(client_info);
    }

    out_obj["player"] = array;
  }

  if (publisher) {
    json::Object client_info;
    publisher->Dump(client_info);
    out_obj["publisher"] = client_info;
  }
}

void  MediaStatistics::StreamInfo::OnClient(std::shared_ptr<ClientInfo> c) {
  bool isPublisher = false;
  if (TRtmpPublish == c->type || TRtcPublish == c->type)
    isPublisher = true;
  
  if (isPublisher) {
    publisher = std::move(c);
  } else {
    players.emplace_back(std::move(c));
  }
}

void MediaStatistics::OnStream(std::shared_ptr<MediaRequest> req) {
  StreamInfo* pStream = nullptr;
  std::string id = req->get_stream_url();
  std::lock_guard<std::mutex> guard(client_lock_);
  auto found = streams_.find(id);
  if (found == streams_.end()) {
    auto stream = std::make_unique<StreamInfo>();
    pStream = stream.get();
    streams_.emplace(id, std::move(stream));
  } else {
    pStream = found->second.get();
  }

  pStream->req = std::move(req);
  pStream->created = time(nullptr);
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
  pclient->req = req;
  pclient->created = time(nullptr);

  if (new_client) {
    auto stream_found = streams_.find(req->get_stream_url());
    if (stream_found != streams_.end()) {
      stream_found->second->OnClient(*new_client);
    } else {
      assert(false);
    }
  }
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
    
    ClientInfo* client = it->second.get();
    
    json::Object cli_info;
    client->Dump(cli_info);
    cli_jsons.push_back(cli_info);
  }
}

void MediaStatistics::DumpStream(json::Object& obj, int start, int count) {
  json::Array stream_jsons;
  obj["streams"] = stream_jsons;
  std::lock_guard<std::mutex> guard(client_lock_);

  auto it = streams_.begin();
  for (int i = 0; i < start + count && it != streams_.end(); ++it++, ++i) {
    if (i < start) {
      continue;
    }
    
    StreamInfo* stream = it->second.get();
    
    json::Object stream_info;
    stream->Dump(stream_info);
    stream_jsons.push_back(stream_info);
  }  
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

}

