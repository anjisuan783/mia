/*
 * LibNiceConnection.cpp
 */

#include <nice/nice.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
#include <mutex>
#include <iostream>

#include "rtc_base/object_pool.h"
#include "erizo/LibNiceConnection.h"
#include "erizo/SdpInfo.h"
#include "utils/Clock.h"

#define USER_PACKET_POOL

using namespace wa;

namespace erizo {

DEFINE_LOGGER(LibNiceConnection, "LibNiceConnection")

void cb_nice_recv(NiceAgent *, guint stream_id, guint component_id,
    guint len, gchar* buf, gpointer user_data) {
  if (user_data == NULL || len == 0) {
    return;
  }
  LibNiceConnection* nicecon = reinterpret_cast<LibNiceConnection*>(user_data);
  nicecon->onData(component_id, reinterpret_cast<char*> (buf), static_cast<unsigned int> (len));
}

void cb_new_candidate(NiceAgent     *,
                      NiceCandidate *candidate,
                      gpointer       user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  conn->gotCandidate(candidate);
}

void cb_candidate_gathering_done(NiceAgent *, guint stream_id, gpointer user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  conn->gatheringDone(stream_id);
}

void cb_component_state_changed(NiceAgent *, guint stream_id,
    guint component_id, guint state, gpointer user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  
  if (state == NICE_COMPONENT_STATE_DISCONNECTED) { 
  } else if (state == NICE_COMPONENT_STATE_GATHERING) {
  } else if (state == NICE_COMPONENT_STATE_CONNECTING) {
  } else if (state == NICE_COMPONENT_STATE_CONNECTED) {
  } else if (state == NICE_COMPONENT_STATE_READY) {
    conn->updateComponentState(component_id, IceState::READY);
  } else if (state == NICE_COMPONENT_STATE_LAST) {
  } else if (state == NICE_COMPONENT_STATE_FAILED) {
    conn->updateComponentState(component_id, IceState::FAILED);
  }
}

void cb_new_selected_pair(NiceAgent     *agent,
                          guint          stream_id,
                          guint          component_id,
                          NiceCandidate *lcandidate,
                          NiceCandidate *rcandidate,
                          gpointer       user_data) {
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);
  conn->updateComponentState(component_id, IceState::READY);
}

void cb_first_binding_request(NiceAgent *agent,
                              guint      stream_id,
                              gpointer   user_data){
  //std::cout << "cb_first_binding_request" << "stream id:" << stream_id << std::endl;
}

void cb_new_remote_candidate(NiceAgent     *,
                             NiceCandidate *candidate,
                             gpointer       user_data){
  assert(candidate);
  
  LibNiceConnection *conn = reinterpret_cast<LibNiceConnection*>(user_data);

  conn->onRemoteNewCandidate(candidate);
}

LibNiceConnection::LibNiceConnection(std::unique_ptr<LibNiceInterface> libnice, 
                                     const IceConfig& ice_config,
                                     wa::IOWorker* worker)
  : IceConnection{ice_config},
    lib_nice_{std::move(libnice)}, 
    agent_{NULL}, 
    context_{NULL}, 
    loop_{NULL}, 
    candsDelivered_{0}, 
    receivedLastCandidate_{false} {
    
#if !GLIB_CHECK_VERSION(2, 35, 0)
  g_type_init();
#endif
  context_ = worker->getMainContext();
  loop_ = worker->getMainLoop();
}

LibNiceConnection::~LibNiceConnection() {
  this->close();
}

void LibNiceConnection::close() {
  if (checkIceState() == IceState::FINISHED) {
    return;
  }
  updateIceState(IceState::FINISHED);
  ELOG_DEBUG("%s message:closing", toLog());

  {
    std::lock_guard<std::mutex> guard(close_mutex_);
    listener_.reset();
    
    if (agent_ != NULL) {
      g_object_unref(agent_);
      agent_ = NULL;
    }
  }
  ELOG_DEBUG("%s message: closed, this: %p", toLog(), this);
}

static std::mutex s_memory_pool_lock_;
static webrtc::ObjectPoolT<DataPacket> s_memory_pool_{102400};

DataPacket* DataPacketMaker(int _comp, 
                            const char *_data, 
                            int _length, 
                            packetType _type, 
                            uint64_t _received_time_ms) {
  DataPacket* pkt = nullptr;
  {
    std::lock_guard<std::mutex> guard(s_memory_pool_lock_);
    pkt = s_memory_pool_.New(); 
  }
  pkt->Init(_comp, _data, _length, _type, _received_time_ms);
  return pkt;
}

void DataPacketDeleter(DataPacket* pkt) {
    std::lock_guard<std::mutex> guard(s_memory_pool_lock_);
    s_memory_pool_.Delete(pkt); 
}

void LibNiceConnection::onData(unsigned int component_id, char* buf, int len) {
  if (checkIceState() == IceState::READY) {
    if (auto listener = getIceListener().lock()) {
#ifdef USER_PACKET_POOL
      std::shared_ptr<DataPacket> packet(
          DataPacketMaker(component_id, buf, len, VIDEO_PACKET, 0), 
          DataPacketDeleter);
#else
      auto packet = std::make_shared<DataPacket>(
          component_id, buf, len, VIDEO_PACKET, 0);
#endif
      listener->onPacketReceived(std::move(packet));
    }
  }
}

int LibNiceConnection::sendData(unsigned int component_id, const void* buf, int len) {
  int val = -1;
  if (this->checkIceState() == IceState::READY) {
    val = lib_nice_->NiceAgentSend(agent_, stream_id_, 
        component_id, len, reinterpret_cast<const gchar*>(buf));
  }
  if (val != len) {
    ELOG_DEBUG("%s message: Sending less data than expected,"
               " sent: %d, to_send: %d", toLog(), val, len);
  }
  return val;
}

void LibNiceConnection::start() {
    std::lock_guard<std::mutex> guard(close_mutex_);
    if (this->checkIceState() != INITIAL) {
      return;
    }
    ELOG_TRACE("%s message: creating Nice Agent", toLog());

    // Create a nice agent
    agent_ = lib_nice_->NiceAgentNewFull(context_, 
        NICE_AGENT_OPTION_LITE_MODE|NICE_AGENT_OPTION_SUPPORT_RENOMINATION);

    if (ice_config_.stun_server.compare("") != 0 && ice_config_.stun_port != 0) {
      g_object_set(agent_, "stun-server", (gchar*)ice_config_.stun_server.c_str(), nullptr);
      g_object_set(agent_, "stun-server-port", ice_config_.stun_port, nullptr);

      ELOG_DEBUG("%s message: setting stun, stun_server: %s, stun_port: %d",
                 toLog(), ice_config_.stun_server.c_str(), ice_config_.stun_port);
    }
    
    gboolean controllingMode = {FALSE};
    g_object_set(agent_, "controlling-mode", controllingMode, nullptr);
    
    g_object_set(agent_, "max-connectivity-checks", 100, nullptr);

    gboolean keep_alive = {TRUE};
    g_object_set(agent_, "keepalive-conncheck", keep_alive, nullptr);

    // Connect the signals
    g_signal_connect(agent_, "candidate-gathering-done",
        G_CALLBACK(cb_candidate_gathering_done), this);

    g_signal_connect(agent_, "new-selected-pair-full",
        G_CALLBACK(cb_new_selected_pair), this);
    
    g_signal_connect(agent_, "component-state-changed",
        G_CALLBACK(cb_component_state_changed), this);
    
    g_signal_connect(agent_, "new-candidate-full",
        G_CALLBACK(cb_new_candidate), this);

    g_signal_connect(agent_, "initial-binding-request-received",
        G_CALLBACK(cb_first_binding_request), this);

    g_signal_connect(agent_, "new-remote-candidate-full",
            G_CALLBACK(cb_new_remote_candidate), this);
     
    // Create a new stream and start gathering candidates
    ELOG_DEBUG("%s message: adding stream, iceComponents: %d", toLog(), ice_config_.ice_components);
    stream_id_ = lib_nice_->NiceAgentAddStream(agent_, ice_config_.ice_components);
    assert(stream_id_ != 0);
      
    gchar *ufrag = NULL, *upass = NULL;
    lib_nice_->NiceAgentGetLocalCredentials(agent_, stream_id_, &ufrag, &upass);
    ufrag_ = std::string(ufrag); 
    g_free(ufrag);
    upass_ = std::string(upass); 
    g_free(upass);

    // Set our remote credentials.  This must be done *after* we add a stream.
    if (ice_config_.username.compare("") != 0 && ice_config_.password.compare("") != 0) {
      ELOG_DEBUG("%s message: setting remote credentials in constructor, ufrag:%s, pass:%s",
                 toLog(), ice_config_.username.c_str(), ice_config_.password.c_str());
      this->setRemoteCredentials(ice_config_.username, ice_config_.password);
    }
    // Set Port Range: If this doesn't work when linking the file libnice.sym has to be modified to include this call
    if (ice_config_.min_port != 0 && ice_config_.max_port != 0) {
      ELOG_DEBUG("%s message: setting port range, min_port: %d, max_port: %d",
                 toLog(), ice_config_.min_port, ice_config_.max_port);
      lib_nice_->NiceAgentSetPortRange(agent_, stream_id_, (guint)ice_config_.ice_components, 
          (guint)ice_config_.min_port, (guint)ice_config_.max_port);
    }

    if (!ice_config_.network_interface.empty()) {
      const char* public_ip = lib_nice_->NiceInterfacesGetIpForInterface(ice_config_.network_interface.c_str());
      if (public_ip) {
        lib_nice_->NiceAgentAddLocalAddress(agent_, public_ip);
      }
    }

    if (ice_config_.ip_addresses.size() > 0) {
      for (const auto& ipAddress : ice_config_.ip_addresses) {
        lib_nice_->NiceAgentAddLocalAddress(agent_, ipAddress.c_str());
      }
    }

    if (ice_config_.turn_server.compare("") != 0 && ice_config_.turn_port != 0) {
      ELOG_DEBUG("%s message: configuring TURN, turn_server: %s , turn_port: %d, turn_username: %s, turn_pass: %s",
                 toLog(), ice_config_.turn_server.c_str(),
          ice_config_.turn_port, ice_config_.turn_username.c_str(), ice_config_.turn_pass.c_str());

      for (unsigned int i = 1; i <= ice_config_.ice_components ; i++) {
        lib_nice_->NiceAgentSetRelayInfo(agent_,
            stream_id_,
            i,
            ice_config_.turn_server.c_str(),     // TURN Server IP
            ice_config_.turn_port,               // TURN Server PORT
            ice_config_.turn_username.c_str(),   // Username
            ice_config_.turn_pass.c_str());       // Pass
      }
    }

    if (agent_) {
      for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
        lib_nice_->NiceAgentAttachRecv(
            agent_, stream_id_, i, context_, reinterpret_cast<void*>(cb_nice_recv), this);
      }
    }
    ELOG_DEBUG("%s message: gathering, this: %p", toLog(), this);
    lib_nice_->NiceAgentGatherCandidates(agent_, stream_id_);
}

void LibNiceConnection::mainLoop() {
  // Start gathering candidates and fire event loop
  ELOG_DEBUG("%s message: starting g_main_loop, this: %p", toLog(), this);
  if (agent_ == NULL || loop_ == NULL) {
    return;
  }
  ELOG_DEBUG("%s message: finished g_main_loop, this: %p", toLog(), this);
}

bool LibNiceConnection::setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) {
  if (agent_ == NULL) {
    this->close();
    return false;
  }
  GSList* candList = NULL;
  ELOG_DEBUG("%s message: setting remote candidates, candidateSize: %lu, mediaType: %d",
             toLog(), candidates.size(), ice_config_.media_type);

  for (unsigned int it = 0; it < candidates.size(); it++) {
    NiceCandidateType nice_cand_type;
    CandidateInfo cinfo = candidates[it];
    // If bundle we will add the candidates regardless the mediaType
    if (cinfo.componentId != 1 || (!is_bundle && cinfo.mediaType != ice_config_.media_type ))
      continue;

    if (strstr(cinfo.hostAddress.c_str(), ":") != NULL) // We ignore IPv6 candidates at this point
      continue;

    switch (cinfo.hostType) {
      case HOST:
        nice_cand_type = NICE_CANDIDATE_TYPE_HOST;
        break;
      case SRFLX:
        nice_cand_type = NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
        break;
      case PRFLX:
        nice_cand_type = NICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
        break;
      case RELAY:
        nice_cand_type = NICE_CANDIDATE_TYPE_RELAYED;
        break;
      default:
        nice_cand_type = NICE_CANDIDATE_TYPE_HOST;
        break;
    }
    if (cinfo.hostPort == 0) {
      continue;
    }
    NiceCandidate* thecandidate = nice_candidate_new(nice_cand_type);
    g_strlcpy(thecandidate->foundation, cinfo.foundation.c_str(), NICE_CANDIDATE_MAX_FOUNDATION);
    thecandidate->username = strdup(cinfo.username.c_str());
    thecandidate->password = strdup(cinfo.password.c_str());
    thecandidate->stream_id = stream_id_;
    thecandidate->component_id = cinfo.componentId;
    thecandidate->priority = cinfo.priority;
    thecandidate->transport = NICE_CANDIDATE_TRANSPORT_UDP;
    nice_address_set_from_string(&thecandidate->addr, cinfo.hostAddress.c_str());
    nice_address_set_port(&thecandidate->addr, cinfo.hostPort);

    std::ostringstream host_info;
    host_info << "hostType: " << cinfo.hostType
         << ", hostAddress: " << cinfo.hostAddress
         << ", hostPort: " << cinfo.hostPort;

    if (cinfo.hostType == RELAY || cinfo.hostType == SRFLX) {
      nice_address_set_from_string(&thecandidate->base_addr, cinfo.rAddress.c_str());
      nice_address_set_port(&thecandidate->base_addr, cinfo.rPort);
      ELOG_DEBUG("%s message: adding relay or srflx remote candidate, %s, rAddress: %s, rPort: %d",
                 toLog(), host_info.str().c_str(),
                 cinfo.rAddress.c_str(), cinfo.rPort);
    } else {
      ELOG_DEBUG("%s message: adding remote candidate, %s, priority: %d, componentId: %d, ufrag: %s, pass: %s",
          toLog(), host_info.str().c_str(), cinfo.priority, cinfo.componentId, cinfo.username.c_str(),
          cinfo.password.c_str());
    }
    candList = g_slist_prepend(candList, thecandidate);
  }
  // TODO(pedro): Set Component Id properly, now fixed at 1
  lib_nice_->NiceAgentSetRemoteCandidates(agent_, stream_id_, 1, candList);
  g_slist_free_full(candList, (GDestroyNotify)&nice_candidate_free);

  return true;
}

void LibNiceConnection::gatheringDone(uint stream_id) {
  ELOG_DEBUG("%s message: gathering done, stream_id: %u", toLog(), stream_id);
  updateIceState(IceState::CANDIDATES_RECEIVED);
}

CandidateInfo LibNiceConnection::transformCandidate(NiceCandidate* cand, bool bLocal) {
  char address[NICE_ADDRESS_STRING_LEN], baseAddress[NICE_ADDRESS_STRING_LEN];

  nice_address_to_string(&cand->addr, address);
  nice_address_to_string(&cand->base_addr, baseAddress);

  CandidateInfo cand_info;
  cand_info.isBundle = true;
  cand_info.priority = cand->priority;
  cand_info.componentId = cand->component_id;
  cand_info.foundation = cand->foundation;
  cand_info.hostAddress = std::string(address);
  cand_info.hostPort = nice_address_get_port(&cand->addr);
  cand_info.mediaType = ice_config_.media_type;
  cand_info.netProtocol = "udp";
  cand_info.transProtocol = ice_config_.transport_name;

  cand_info.username = bLocal ? getLocalUsername() : getRemoteUsername();
  cand_info.password = bLocal ? getLocalPassword() : getRemotePassword();

  /*
   *    NICE_CANDIDATE_TYPE_HOST,
   *    NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE,
   *    NICE_CANDIDATE_TYPE_PEER_REFLEXIVE,
   *    NICE_CANDIDATE_TYPE_RELAYED,
   */
  switch (cand->type) {
    case NICE_CANDIDATE_TYPE_HOST:
      cand_info.hostType = HOST;
      break;
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
      cand_info.hostType = SRFLX;
      cand_info.rAddress = std::string(baseAddress);
      cand_info.rPort = nice_address_get_port(&cand->base_addr);
      break;
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
      cand_info.hostType = PRFLX;
      break;
    case NICE_CANDIDATE_TYPE_RELAYED:
      cand_info.hostType = RELAY;
      cand_info.rAddress = std::string(baseAddress);
      cand_info.rPort = nice_address_get_port(&cand->base_addr);
      break;
    default:
      break;
  }
  
  return cand_info;
}

void LibNiceConnection::gotCandidate(NiceCandidate* cand) {

  CandidateInfo cand_info = transformCandidate(cand, true);
  
  candsDelivered_++;
  if ( strstr(cand_info.hostAddress.c_str(), ":") != NULL) {  // We ignore IPv6 candidates at this point
    return;
  }
  
  if (cand_info.hostPort == 0) {
    return;
  }

  cand_info.username = ufrag_;
  cand_info.password = upass_;

  if (auto listener = this->getIceListener().lock()) {
    listener->onCandidate(cand_info, this);
  }
}

void LibNiceConnection::setRemoteCredentials(const std::string& username, const std::string& password) {
  ELOG_DEBUG("%s message: setting remote credentials, ufrag: %s, pass: %s",
             toLog(), username.c_str(), password.c_str());

  ice_config_.username = username;
  ice_config_.password = password;
  lib_nice_->NiceAgentSetRemoteCredentials(agent_, stream_id_, username.c_str(), password.c_str());
}

void LibNiceConnection::updateComponentState(unsigned int component_id, IceState state) {
  ELOG_TRACE("%s message: new ice component state, newState: %u, "
             "transportName: %s, componentId %u, iceComponents: %u",
             toLog(), state, ice_config_.transport_name.c_str(), 
             component_id, ice_config_.ice_components);
             
  comp_state_list_[component_id] = state;
  if (state == IceState::READY) {
    for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
      if (comp_state_list_[i] != IceState::READY) {
        return;
      }
    }
  } else if (state == IceState::FAILED) {
    if (receivedLastCandidate_ || this->checkIceState() == IceState::READY) {
      ELOG_WARN("%s message: component failed, ice_config_.transport_name: %s, componentId: %u",
                toLog(), ice_config_.transport_name.c_str(), component_id);
      for (unsigned int i = 1; i <= ice_config_.ice_components; i++) {
        if (comp_state_list_[i] != IceState::FAILED) {
          return;
        }
      }
    } else {
      ELOG_WARN("%s message: failed and not received all candidates, newComponentState:%u", toLog(), state);
      return;
    }
  }
  this->updateIceState(state);
}

std::string getHostTypeFromCandidate(NiceCandidate *candidate) {
  switch (candidate->type) {
    case NICE_CANDIDATE_TYPE_HOST: return "host";
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE: return "serverReflexive";
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE: return "peerReflexive";
    case NICE_CANDIDATE_TYPE_RELAYED: return "relayed";
    default: return "unknown";
  }
}

CandidatePair LibNiceConnection::getSelectedPair() {
  char ipaddr[NICE_ADDRESS_STRING_LEN];
  CandidatePair selectedPair;
  NiceCandidate* local, *remote;
  lib_nice_->NiceAgentGetSelectedPair(agent_, stream_id_, 1, &local, &remote);
  nice_address_to_string(&local->addr, ipaddr);
  selectedPair.erizoCandidateIp = std::string(ipaddr);
  selectedPair.erizoCandidatePort = nice_address_get_port(&local->addr);
  selectedPair.erizoHostType = getHostTypeFromCandidate(local);
  ELOG_DEBUG("%s message: selected pair, local_addr: %s, local_port: %d, local_type: %s",
      toLog(), ipaddr, nice_address_get_port(&local->addr), selectedPair.erizoHostType.c_str());
      
  nice_address_to_string(&remote->addr, ipaddr);
  selectedPair.clientCandidateIp = std::string(ipaddr);
  selectedPair.clientCandidatePort = nice_address_get_port(&remote->addr);
  selectedPair.clientHostType = getHostTypeFromCandidate(local);
  ELOG_INFO("%s message: selected pair, remote_addr: %s, remote_port: %d, remote_type: %s",
      toLog(), ipaddr, nice_address_get_port(&remote->addr), selectedPair.clientHostType.c_str());
  return selectedPair;
}

void LibNiceConnection::setReceivedLastCandidate(bool hasReceived) {
  ELOG_DEBUG("%s message: setting hasReceivedLastCandidate, hasReceived: %u", toLog(), hasReceived);
  this->receivedLastCandidate_ = hasReceived;
}

bool LibNiceConnection::removeRemoteCandidates() {
  ELOG_DEBUG("remove remote candidates");
  //nice_agent_remove_remote_candidates(agent_, stream_id_, 1, NULL);
  return true;
}

void LibNiceConnection::onRemoteNewCandidate(NiceCandidate* candidate) {
  ELOG_DEBUG("%s remote new candidate: join, this: %p", toLog(), this);
  CandidateInfo can_info = transformCandidate(candidate, false);
  std::vector<CandidateInfo> t{std::move(can_info)};
  setRemoteCandidates(t, true);
}


LibNiceConnection* LibNiceConnection::create(
    const IceConfig& ice_config, wa::IOWorker* worker) {
  return new LibNiceConnection(std::make_unique<LibNiceInterfaceImpl>(), ice_config, worker);
}


void LibNiceConnection::libnice_log(const gchar *log_domain, 
                                    GLogLevelFlags log_level, 
                                    const gchar *message, 
                                    gpointer user_data){
  if (log_level == G_LOG_LEVEL_DEBUG) {
    ELOG_DEBUG("[agent:%d-%s]msg:%s", user_data, log_domain, message);
  } else if (log_level == G_LOG_LEVEL_ERROR || log_level == G_LOG_LEVEL_CRITICAL) {
    ELOG_ERROR("[agent:%d-%s]msg:%s", user_data, log_domain, message);
  } else if (log_level == G_LOG_LEVEL_INFO  || log_level == G_LOG_LEVEL_MASK){
    ELOG_INFO("[agent:%d-%s]msg:%s", user_data, log_domain, message);
  } else if (log_level == G_LOG_LEVEL_WARNING ){
    ELOG_WARN("[agent:%d-%s]msg:%s", user_data, log_domain, message);
  }
}

} /* namespace erizo */
