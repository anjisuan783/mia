#include "media_source_mgr.h"

#include "media_source.h"
#include "media_server.h"

namespace ma {

static std::shared_ptr<wa::ThreadPool>  workers_;

void MediaSourceMgr::Init(unsigned int num) {
  workers_ = std::make_shared<wa::ThreadPool>(num);
  workers_->start();
}

std::optional<std::shared_ptr<MediaSource>> 
    MediaSourceMgr::FetchOrCreateSource(const std::string& url) {
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    auto found = sources_.find(url);
    if(found != sources_.end()){
      return found->second;
    }
  }
  
  auto ms = std::make_shared<MediaSource>(url);

  if (!ms->initialize(std::move(this->GetWorker()),
                      g_server_.config_.enable_gop_,
                      g_server_.config_.enable_atc_)) {
    return std::nullopt;
  }
  
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    sources_[url] = ms;
  }

  return std::move(ms);
}

void MediaSourceMgr::RemoveSource(const std::string& id) {
  std::lock_guard<std::mutex> guard(source_lock_);
  sources_.erase(id);
}

std::shared_ptr<wa::Worker> MediaSourceMgr::GetWorker() {
  return std::move(workers_->getLessUsedWorker());
}

MediaSourceMgr g_source_mgr_;

}

