// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_
#define ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_

#include <string>
#include "erizo/dtls/DtlsSocket.h"
#include "erizo/IceConnection.h"
#include "erizo/Transport.h"
#include "erizo/logger.h"

namespace erizo {

class TimeoutChecker;
class SrtpChannel;

class DtlsTransport : dtls::DtlsReceiver, public Transport {
  DECLARE_LOGGER();

 public:
  DtlsTransport(MediaType med, 
                const std::string& transport_name, 
                const std::string& connection_id, bool bundle,
                bool rtcp_mux, 
                std::weak_ptr<TransportListener> transport_listener, 
                const IceConfig& iceConfig,
                std::string username, 
                std::string password, 
                bool isServer, 
                wa::Worker* worker,
                wa::IOWorker* io_worker);
  virtual ~DtlsTransport();
  void connectionStateChanged(IceState newState);
  std::string getMyFingerprint() const;

  void start() override;
  void close() override;

  //woker thread
  void onIceData(packetPtr packet) override;

  //IceConnectionListener implement
  void onCandidate(const CandidateInfo &candidate, IceConnection *conn) override;
  void updateIceState(IceState state, IceConnection *conn) override;
  
  void write(char* data, int len) override;

  //DtlsReceiver implement
  void onDtlsPacket(dtls::DtlsSocketContext *ctx, 
                    const unsigned char* data, 
                    unsigned int len) override;
  void onHandshakeCompleted(dtls::DtlsSocketContext *ctx, 
                            std::string clientKey, 
                            std::string serverKey,
                            std::string srtp_profile) override;
  void onHandshakeFailed(dtls::DtlsSocketContext *ctx, const std::string& error) override;

  void writeDtlsPacket(dtls::DtlsSocketContext *ctx, packetPtr packet);
  
  void processLocalSdp(SdpInfo *localSdp_) override;

  void updateIceStateSync(IceState state, IceConnection *conn);
  
  static bool isDtlsPacket(const char* buf, int len);
 private:
  char protectBuf_[5000];
  std::unique_ptr<dtls::DtlsSocketContext> dtlsRtp, dtlsRtcp;
  std::unique_ptr<SrtpChannel> srtp_, srtcp_;
  bool readyRtp{false}, readyRtcp{false};
  bool isServer_;
  std::unique_ptr<TimeoutChecker> rtcp_timeout_checker_, rtp_timeout_checker_;
  packetPtr p_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_DTLSTRANSPORT_H_
