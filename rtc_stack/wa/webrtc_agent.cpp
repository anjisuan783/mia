//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "webrtc_agent.h"

#include "h/rtc_return_value.h"
#include "webrtc_agent_pc.h"

#include "erizo/global_init.h"
#include "event.h"

using namespace erizo;

namespace wa {

void lib_evnet_log(int severity, const char *msg);

DEFINE_LOGGER(WebrtcAgent, "wa.agent");

std::shared_ptr<ThreadPool> WebrtcAgent::workers_;
std::shared_ptr<IOThreadPool> WebrtcAgent::io_workers_;

bool WebrtcAgent::global_init_ = false;

WebrtcAgent::WebrtcAgent() = default;

WebrtcAgent::~WebrtcAgent() = default;

int WebrtcAgent::initiate(uint32_t num_workers, 
                          const std::vector<std::string>& ip_addresses, 
                          const std::string& service_addr) {
  if(ip_addresses.empty()){
    return wa_e_invalid_param;
  }

  if(!global_init_) {
    erizo::erizo_global_init();
    workers_ = std::make_shared<ThreadPool>(num_workers);
    workers_->start("wa");
    io_workers_ = std::make_shared<IOThreadPool>(num_workers);
    io_workers_->start();

    event_set_log_callback(lib_evnet_log);
    global_init_ = true;
  }

  if(0 == num_workers) {
    num_workers = 1;
  }

  ELOG_INFO("WebrtcAgent initiate %d workers, ip:%s", 
            num_workers, ip_addresses[0].c_str());
  if(!network_addresses_.empty()){
    return wa_e_already_initialized;
  }

  network_addresses_ = ip_addresses;
  stun_address_ = service_addr;

  return wa_ok;
}

int WebrtcAgent::CreatePeer(TOption& options, const std::string& offer) {
  auto pc = std::make_shared<WrtcAgentPc>(options, *this);
  
  {
    std::lock_guard<std::mutex> guard(pcLock_);
    bool not_found = peerConnections_.emplace(options.connectId_, pc).second;
    if(!not_found) {
      return wa_e_found;
    }
  }
  
  std::shared_ptr<Worker> worker = workers_->getLessUsedWorker();
  std::shared_ptr<IOWorker> ioworker = io_workers_->getLessUsedIOWorker();
  pc->init(worker, ioworker, network_addresses_, stun_address_);
  pc->signalling("offer", offer);
  return wa_ok;
}

int WebrtcAgent::DestroyPeer(const std::string& connectId) {
  std::shared_ptr<WrtcAgentPc> pc;
  {
    std::lock_guard<std::mutex> guard(pcLock_);
    auto found = peerConnections_.find(connectId);
    if(found == peerConnections_.end()){
      return wa_e_not_found;
    }

    pc = std::move(found->second);
  
    peerConnections_.erase(found);
  }

  pc->close();
  return wa_ok;
}

int WebrtcAgent::Subscribe(const std::string& publisher, 
                           const std::string& player) {
  OLOG_TRACE(player << " subscribe " << publisher);
  std::shared_ptr<WrtcAgentPc> pc_publisher;
  std::shared_ptr<WrtcAgentPc> pc_player;
  WrtcAgentPc::WEBRTC_TRACK_TYPE player_tracks;

  {
    std::lock_guard<std::mutex> guard(pcLock_);
    auto found = peerConnections_.find(publisher);
    if(found == peerConnections_.end()){
      return wa_e_not_found;
    }
    pc_publisher = found->second;

    found = peerConnections_.find(player);
    if(found == peerConnections_.end()){
      return wa_e_not_found;
    }
    pc_player = found->second;

    player_tracks = pc_player->getTracks();
  }
  
  if (player_tracks.empty()) {
    OLOG_ERROR("player tracks empty!");
    return wa_failed;
  }
  pc_publisher->Subscribe(player_tracks);

  return wa_ok;
}

int WebrtcAgent::Unsubscribe(const std::string& publisher, 
                             const std::string& player) {
  OLOG_TRACE(player << " unsubscribe " << publisher);
  std::shared_ptr<WrtcAgentPc> pc_publisher;
  std::shared_ptr<WrtcAgentPc> pc_player;
  WrtcAgentPc::WEBRTC_TRACK_TYPE player_tracks;

  {
    std::lock_guard<std::mutex> guard(pcLock_);

    auto found = peerConnections_.find(publisher);
    if(found == peerConnections_.end()){
      return wa_e_not_found;
    }
    pc_publisher = found->second;

    found = peerConnections_.find(player);
    if(found == peerConnections_.end()){
      return wa_e_not_found;
    }
    pc_player = found->second;

    player_tracks = pc_player->getTracks();
  }
  
  if (player_tracks.empty()) {
    OLOG_ERROR("player tracks empty!");
    return wa_failed;
  }
  pc_publisher->unSubscribe(player_tracks);

  return wa_ok;
}

std::unique_ptr<rtc_api> AgentFactory::create_agent() {
  return std::make_unique<WebrtcAgent>();
}

} //namespace wa

