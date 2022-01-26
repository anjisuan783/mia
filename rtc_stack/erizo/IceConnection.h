// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.

#ifndef ERIZO_SRC_ERIZO_ICECONNECTION_H_
#define ERIZO_SRC_ERIZO_ICECONNECTION_H_

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <atomic>

#include "erizo/MediaDefinitions.h"
#include "erizo/SdpInfo.h"
#include "erizo/logger.h"
#include "erizo/lib/LibNiceInterface.h"

typedef struct _NiceAgent NiceAgent;
typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;

typedef unsigned int uint;

namespace erizo {

// forward declarations
class CandidateInfo;
class WebRtcConnection;
class IceConnection;

struct CandidatePair{
  std::string erizoCandidateIp;
  int erizoCandidatePort;
  std::string clientCandidateIp;
  int clientCandidatePort;
  std::string erizoHostType;
  std::string clientHostType;
};

struct IceConfig {
    MediaType media_type{MediaType::OTHER};
    std::string transport_name;
    std::string connection_id;
    unsigned int ice_components{0};
    std::string username;
    std::string password;
    std::string turn_server;
    std::string turn_username;
    std::string turn_pass;
    std::string stun_server;
    std::string network_interface;
    std::vector<std::string> ip_addresses;
    uint16_t stun_port{0};
    uint16_t turn_port{0};
    uint16_t min_port{0};
    uint16_t max_port{0};
    bool should_trickle{false};
    bool use_nicer{false};
};

/**
 * States of ICE
 */
enum IceState {
  INITIAL, 
  CANDIDATES_RECEIVED, 
  READY, 
  FINISHED, 
  FAILED
};

class IceConnectionListener {
 public:
    virtual void onPacketReceived(std::shared_ptr<DataPacket> packet) = 0;
    virtual void onCandidate(const CandidateInfo &candidate, IceConnection *conn) = 0;
    virtual void updateIceState(IceState state, IceConnection *conn) = 0;
};

class IceConnection : public LogContext {
  DECLARE_LOGGER();

 public:
  explicit IceConnection(const IceConfig& ice_config);
  virtual ~IceConnection();

  virtual void start() = 0;
  virtual bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) = 0;
  virtual void setRemoteCredentials(const std::string& username, const std::string& password) = 0;
  virtual int sendData(unsigned int component_id, const void* buf, int len) = 0;

  virtual void onData(unsigned int component_id, char* buf, int len) = 0;
  virtual CandidatePair getSelectedPair() = 0;
  virtual void setReceivedLastCandidate(bool hasReceived) = 0;
  virtual void close() = 0;

  void updateIceState(IceState state);
  inline IceState checkIceState() {
    return ice_state_;
  }
  void setIceListener(std::weak_ptr<IceConnectionListener> listener);
  std::weak_ptr<IceConnectionListener> getIceListener();
  
  inline const std::string& getLocalUsername() const {
    return ufrag_;
  }
  inline const std::string& getLocalPassword() const {
    return upass_;
  }

  inline const std::string& getRemoteUsername() const {
    return ice_config_.username;
  }
  inline const std::string& getRemotePassword() const {
    return ice_config_.password;
  }

  virtual bool removeRemoteCandidates();
 private:
  virtual std::string iceStateToString(IceState state) const;

 protected:  
  inline std::string toLog() const {
    return "id: " + ice_config_.connection_id + ", " + printLogContext();
  }
 private:
  std::atomic<IceState> ice_state_;

 protected:
  std::weak_ptr<IceConnectionListener> listener_;
  IceConfig ice_config_;
  std::string ufrag_;
  std::string upass_;
  std::map <unsigned int, IceState> comp_state_list_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_ICECONNECTION_H_
