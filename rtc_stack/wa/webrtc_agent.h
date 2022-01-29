//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WA_WEBRTC_AGENT_H__
#define __WA_WEBRTC_AGENT_H__

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

#include "h/rtc_stack_api.h"

#include "utils/IOWorker.h"
#include "utils/Worker.h"

#include "./wa_log.h"

namespace wa {

class WrtcAgentPcBase;

class WebrtcAgent final : public RtcApi {
  DECLARE_LOGGER();
public:
  WebrtcAgent();
  
  ~WebrtcAgent() override;

  int initiate(uint32_t num_workers, 
               const std::vector<std::string>& ip_addresses, 
               const std::string& service_addr);

  int CreatePeer(TOption&, const std::string& offer) override;

  int DestroyPeer(const std::string& connectId) override;

  int Subscribe(const std::string& publisher, 
                const std::string& player) override;

  int Unsubscribe(const std::string& publisher, 
                  const std::string& player) override;

  const std::vector<std::string>& getAddresses(){
    return network_addresses_;
  }

private:
  using connection_id = std::string;
  using track_id = std::string;

  std::mutex pcLock_;
  std::unordered_map<connection_id, std::shared_ptr<WrtcAgentPcBase>> 
      peerConnections_;

  static std::shared_ptr<ThreadPool>  workers_;
  static std::shared_ptr<IOThreadPool> io_workers_;
  static bool global_init_;

  std::vector<std::string> network_addresses_;
  std::string stun_address_;
};

} //!wa
#endif //__WA_WEBRTC_AGENT_H__

