#include "media_source_mgr.h"
#include "rtmp/media_req.h"
#include "media_statistics.h"
#include "media_server.h"

namespace ma {

static std::shared_ptr<wa::ThreadPool>  workers_;

int MediaSourceMgr::Init(unsigned int num, 
    const std::vector<std::string>& candidates) {
  workers_ = std::make_shared<wa::ThreadPool>(num);
  workers_->start("live");
  rtc_api_ = std::move(wa::AgentFactory().create_agent());
  return rtc_api_->Open(num, candidates, "");
}

void MediaSourceMgr::Close() {
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    for(auto& i : sources_)
      i.second->Close();
  }

  rtc_api_->Close();
  workers_->close();
  workers_ = nullptr;
}

std::shared_ptr<MediaSource>
MediaSourceMgr::FetchOrCreateSource(MediaSource::Config& cfg,
                                    std::shared_ptr<MediaRequest> req) {
  std::shared_ptr<MediaSource> ms;

  {
    std::lock_guard<std::mutex> guard(source_lock_);
    auto found = sources_.find(req->get_stream_url());
    if(found != sources_.end()) {
      return found->second;
    }

    std::string streamName = req->get_stream_url();
    ms = std::make_shared<MediaSource>(req);
    sources_[streamName] = ms;
  }
  
  cfg.worker = GetWorker();
  cfg.gop = g_server_.config_.enable_gop_;
  cfg.jitter_algorithm = g_server_.config_.jitter_algo_;
  if (!cfg.rtc_api) {
    cfg.rtc_api = rtc_api_.get();
  }
  cfg.enable_rtc2rtmp_ = g_server_.config_.enable_rtc2rtmp_;
  cfg.enable_rtc2rtmp_debug_ = g_server_.config_.enable_rtc2rtmp_debug_;
  cfg.enable_rtmp2rtc_ = g_server_.config_.enable_rtmp2rtc_;
  cfg.enable_rtmp2rtc_debug_ = g_server_.config_.enable_rtmp2rtc_debug_;
  cfg.consumer_queue_size_ = g_server_.config_.consumer_queue_size_;
  cfg.mix_correct_ = g_server_.config_.mix_correct_;

  ms->Open(cfg);
  Stat().OnStream(std::move(req));
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
  std::shared_ptr<MediaSource> source;
  {
    std::lock_guard<std::mutex> guard(source_lock_);
    auto found = sources_.find(req->get_stream_url());
    if (found == sources_.end()) {
      assert(false);
      return;
    }
    source = found->second;
    sources_.erase(found);
  }

  source->Close();
  Stat().OnStreamClose(std::move(req));
}

std::shared_ptr<wa::Worker> MediaSourceMgr::GetWorker() {
  return std::move(workers_->getLessUsedWorker());
}

MediaSourceMgr g_source_mgr_;

} //namespace ma

