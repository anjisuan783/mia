#include "media_source_mgr.h"
#include "media_server.h"
#include "rtmp/media_req.h"

namespace ma {

static std::shared_ptr<wa::ThreadPool>  workers_;

void MediaSourceMgr::Init(unsigned int num) {
  workers_ = std::make_shared<wa::ThreadPool>(num);
  workers_->start();
}

std::shared_ptr<MediaSource>
MediaSourceMgr::FetchOrCreateSource(MediaSource::Config& cfg,
                                    std::shared_ptr<MediaRequest> req) {
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    auto found = sources_.find(req->stream);
    if(found != sources_.end()){
      return found->second;
    }
  }
  
  auto ms = std::make_shared<MediaSource>(req);

  ms->Initialize(cfg);
  
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    sources_[req->stream] = ms;
  }

  g_server_.on_publish(ms, req);

  return std::move(ms);
}

std::optional<std::shared_ptr<MediaSource>> 
MediaSourceMgr::FetchSource(const std::string& stream_id) {

  std::lock_guard<std::mutex> guard(source_lock_);  
  auto found = sources_.find(stream_id);
  if (found != sources_.end()) {
    return found->second;
  }

  return std::nullopt;
}

void MediaSourceMgr::RemoveSource(const std::string& stream_id) {
  // lock twice optimize ?
  auto found = sources_.end();
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    found = sources_.find(stream_id);
    if (found == sources_.end()) {
      assert(false);
      return;
    }
  }
  
  g_server_.on_unpublish(found->second, found->second->GetRequest());
  
  std::lock_guard<std::mutex> guard(source_lock_);
  sources_.erase(stream_id);
}

std::shared_ptr<wa::Worker> MediaSourceMgr::GetWorker() {
  return std::move(workers_->getLessUsedWorker());
}

MediaSourceMgr g_source_mgr_;

} //namespace ma

