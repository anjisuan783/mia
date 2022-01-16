// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_TRANSPORT_H_
#define ERIZO_SRC_ERIZO_TRANSPORT_H_

#include <string>
#include <vector>
#include <cstdio>
#include "erizo/IceConnection.h"
#include "utils/Worker.h"
#include "utils/IOWorker.h"
#include "erizo/logger.h"

namespace erizo {

/**
 * States of Transport
 */
enum TransportState {
  TRANSPORT_INITIAL, 
  TRANSPORT_STARTED, 
  TRANSPORT_GATHERED, 
  TRANSPORT_READY, 
  TRANSPORT_FINISHED, 
  TRANSPORT_FAILED
};

enum {
  TRANSPORT_ERROR_OK,
  TRANSPORT_ERROR_SRTP_HANDSHARK_FAILED,
  TRANSPORT_ERROR_SRTP_HANDSHARK_KEY,
  TRANSPORT_ERROR_ICE_FAILED,
};

class Transport;

class TransportListener {
 public:
  virtual ~TransportListener() = default;
  virtual void onTransportData(std::shared_ptr<DataPacket> packet, Transport *transport) = 0;
  virtual void updateState(TransportState state, Transport *transport) = 0;
  virtual void onCandidate(const CandidateInfo& cand, Transport *transport) = 0;
};

class Transport : public std::enable_shared_from_this<Transport>, 
                  public IceConnectionListener,
                  public LogContext {
 public:
  Transport(MediaType med, 
            const std::string& transport_name, 
            const std::string& connection_id, 
            bool bundle,
            bool rtcp_mux, 
            std::weak_ptr<TransportListener> transport_listener, 
            const IceConfig& iceConfig,
            wa::Worker* worker) 
            : mediaType(med), 
              transport_name(transport_name), 
              rtcp_mux_(rtcp_mux), 
              transport_listener_(transport_listener),
              connection_id_(connection_id),
              state_(TRANSPORT_INITIAL), 
              iceConfig_(iceConfig), 
              bundle_(bundle),
              running_{true}, 
              worker_{worker} {}
  virtual ~Transport() {}
  virtual void updateIceState(IceState state, IceConnection *conn) = 0;
  virtual void onIceData(DataPacket* packet) = 0;
  virtual void write(char* data, int len) = 0;
  virtual void processLocalSdp(SdpInfo *localSdp_) = 0;
  virtual void start() = 0;
  virtual void close() = 0;
  virtual IceConnection* getIceConnection() { return ice_.get(); }
  
  void setTransportListener(std::weak_ptr<TransportListener> listener) {
    transport_listener_ = listener;
  }
  std::weak_ptr<TransportListener> getTransportListener() {
    return transport_listener_;
  }
  
  TransportState getTransportState() {
    return state_;
  }
  void updateTransportState(TransportState state) {
    if (state == state_) {
      return;
    }
    state_ = state;
    if (auto listener = getTransportListener().lock()) {
      listener->updateState(state, this);
    }
  }
  void writeOnIce(int comp, void* buf, int len) {
    if (!running_) {
      return;
    }
    ice_->sendData(comp, buf, len);
  }
  bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool isBundle) {
    return ice_->setRemoteCandidates(candidates, isBundle);
  }
  bool removeRemoteCandidates() {
    return ice_->removeRemoteCandidates();
  }

  //IceConnectionListener implement
  void onPacketReceived(DataPacket* packet) override{
    worker_->task([weak_transport = weak_from_this(), packet]() {
      if (auto this_ptr = weak_transport.lock()) {
        if (packet->length > 0) {
          this_ptr->onIceData(packet);
        } else if (packet->length == -1) {
          this_ptr->running_ = false;
        }
      }
      delete packet;
    });
  }

  inline std::string toLog() {
    return "id: " + connection_id_ + ", " + printLogContext();
  }

  wa::Worker* getWorker() {
    return worker_;
  }

  inline int getErrorCode() {
    return error_code_;
  }

public:
  std::unique_ptr<IceConnection> ice_;
  MediaType mediaType;
  std::string transport_name;
  bool rtcp_mux_;
private:
  std::weak_ptr<TransportListener> transport_listener_;

protected:
  std::string connection_id_;
  TransportState state_;
  IceConfig iceConfig_;
  bool bundle_;
  bool running_;
  wa::Worker* worker_;

  int error_code_{TRANSPORT_ERROR_OK};
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_TRANSPORT_H_

