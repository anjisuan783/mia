#include "media_source_mgr.h"
#include "media_server.h"
#include "rtmp/media_req.h"

namespace ma {

static std::shared_ptr<wa::ThreadPool>  workers_;

int MediaSourceMgr::Init(unsigned int num) {
  workers_ = std::make_shared<wa::ThreadPool>(num);
  workers_->start();
  rtc_api_ = std::move(wa::AgentFactory().create_agent());
  return rtc_api_->initiate(g_server_.config_.rtc_workers_,
                            g_server_.config_.candidates_,
                            g_server_.config_.stun_addr_);
}

std::shared_ptr<MediaSource>
MediaSourceMgr::FetchOrCreateSource(MediaSource::Config& cfg,
                                    std::shared_ptr<MediaRequest> req) {
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    auto found = sources_.find(req->get_stream_url());
    if(found != sources_.end()){
      return found->second;
    }
  }

  if (!cfg.rtc_api) {
    cfg.rtc_api = rtc_api_.get();
  }
  
  auto ms = std::make_shared<MediaSource>(req);

  ms->Initialize(cfg);
  
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    sources_[req->get_stream_url()] = ms;
  }

  return std::move(ms);
}

std::optional<std::shared_ptr<MediaSource>>
MediaSourceMgr::FetchSource(std::shared_ptr<MediaRequest> req) {

  std::lock_guard<std::mutex> guard(source_lock_);  
  auto found = sources_.find(req->get_stream_url());
  if (found != sources_.end()) {
    return found->second;
  }

  return std::nullopt;
}

void MediaSourceMgr::RemoveSource(std::shared_ptr<MediaRequest> req) {
  auto found = sources_.end();
  
  std::lock_guard<std::mutex> guard(source_lock_);
  found = sources_.find(req->get_stream_url());
  if (found == sources_.end()) {
    assert(false);
    return;
  }
  sources_.erase(found);
}

std::shared_ptr<wa::Worker> MediaSourceMgr::GetWorker() {
  return std::move(workers_->getLessUsedWorker());
}

MediaSourceMgr g_source_mgr_;

} //namespace ma

