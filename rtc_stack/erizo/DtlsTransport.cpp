/*
 * DtlsConnection.cpp
 */

#include "erizo/DtlsTransport.h"

#include <string>
#include <cstring>
#include <memory>

#include "erizo/SrtpChannel.h"
#include "erizo/rtp/RtpHeaders.h"
#include "erizo/LibNiceConnection.h"

using erizo::TimeoutChecker;
using erizo::DtlsTransport;
using dtls::DtlsSocketContext;

class TimeoutChecker {
  DECLARE_LOGGER();

  const unsigned int kMaxTimeoutChecks = 15;
  const unsigned int kInitialSecsPerTimeoutCheck = 1;

 public:
  TimeoutChecker(DtlsTransport* transport, dtls::DtlsSocketContext* ctx);
  virtual ~TimeoutChecker();
  void scheduleCheck();
  void cancel();

 private:
  void scheduleNext();
  void resend();

 private:
  DtlsTransport* transport_;
  dtls::DtlsSocketContext* socket_context_;
  unsigned int check_seconds_;
  unsigned int max_checks_;
  std::shared_ptr<wa::ScheduledTaskReference> scheduled_task_;
};


DEFINE_LOGGER(DtlsTransport, "DtlsTransport");
DEFINE_LOGGER(TimeoutChecker, "TimeoutChecker");

using std::memcpy;

TimeoutChecker::TimeoutChecker(DtlsTransport* transport, dtls::DtlsSocketContext* ctx)
    : transport_(transport), socket_context_(ctx),
      check_seconds_(kInitialSecsPerTimeoutCheck), max_checks_(kMaxTimeoutChecks),
      scheduled_task_{std::make_shared<wa::ScheduledTaskReference>()} {
}

TimeoutChecker::~TimeoutChecker() {
  cancel();
}

void TimeoutChecker::cancel() {
  transport_->getWorker()->unschedule(scheduled_task_);
}

void TimeoutChecker::scheduleCheck() {
  ELOG_TRACE("message: Scheduling a new TimeoutChecker");
  transport_->getWorker()->unschedule(scheduled_task_);
  check_seconds_ = kInitialSecsPerTimeoutCheck;
  if (transport_->getTransportState() != TRANSPORT_READY) {
    scheduleNext();
  }
}

//TODO
//may cause crash
void TimeoutChecker::scheduleNext() {
  scheduled_task_ = transport_->getWorker()->scheduleFromNow([this]() {
    if (transport_->getTransportState() == TRANSPORT_READY) {
      return;
    }
    if (transport_ != nullptr) {
      if (max_checks_-- > 0) {
        ELOG_DEBUG("Handling dtls timeout, checks left: %d", max_checks_);
        if (socket_context_) {
          //std::lock_guard<std::mutex> guard(dtls_mutex);
          socket_context_->handleTimeout();
        }
        scheduleNext();
      } else {
        ELOG_DEBUG("%s message: DTLS timeout", transport_->toLog());
        transport_->onHandshakeFailed(socket_context_, "Dtls Timeout on TimeoutChecker");
      }
    }
  }, std::chrono::seconds(check_seconds_));
}

DtlsTransport::DtlsTransport(
    MediaType med, 
    const std::string &transport_name, 
    const std::string& connection_id,
    bool bundle, 
    bool rtcp_mux, 
    std::weak_ptr<TransportListener> transport_listener,
    const IceConfig& iceConfig, 
    std::string username, 
    std::string password,
    bool isServer, 
    wa::Worker* worker, 
    wa::IOWorker* io_worker)
      : Transport(med, 
                  transport_name, 
                  connection_id, 
                  bundle,
                  rtcp_mux, 
                  transport_listener, 
                  iceConfig, 
                  worker),
        isServer_(isServer) {
  ELOG_DEBUG("%s message: constructor, transportName: %s, isBundle: %d", 
    toLog(), transport_name.c_str(), bundle);
    
  dtlsRtp.reset(new DtlsSocketContext());
  dtlsRtp->setDtlsReceiver(this);

  int comps = 1;
  if (!rtcp_mux){
    rtcp_mux = 2;
    dtlsRtcp.reset(new DtlsSocketContext());
    dtlsRtcp->setDtlsReceiver(this);
  }
  
  if (isServer_) {
    ELOG_DEBUG("%s message: creating  passive-server", toLog());
    dtlsRtp->createServer();
    
    if (dtlsRtcp) {
      dtlsRtcp->createServer();
    }
  } else {
    ELOG_DEBUG("%s message: creating active-client", toLog());
    dtlsRtp->createClient();
    
    if (dtlsRtcp) {
      dtlsRtcp->createClient();
    }
  }
  
  iceConfig_.connection_id = connection_id_;
  iceConfig_.transport_name = transport_name;
  iceConfig_.media_type = med;
  iceConfig_.ice_components = comps;
  iceConfig_.username = username;
  iceConfig_.password = password;
  
  // We only use libnice connection
  ice_.reset(LibNiceConnection::create(iceConfig_, io_worker));
  rtp_timeout_checker_ = std::move(std::make_unique<TimeoutChecker>(this, dtlsRtp.get()));
  if (!rtcp_mux) {
    rtcp_timeout_checker_ = std::move(std::make_unique<TimeoutChecker>(this, dtlsRtcp.get()));
  }
  ELOG_DEBUG("%s message: created", toLog());
}

DtlsTransport::~DtlsTransport() {
  if (this->state_ != TRANSPORT_FINISHED) {
    this->close();
  }
}

void DtlsTransport::start() {
  ice_->setIceListener(weak_from_this());
  ice_->copyLogContextFrom(*this);
  ELOG_DEBUG("%s message: starting ice", toLog());
  ice_->start();
}

void DtlsTransport::close() {
  ELOG_DEBUG("%s message: closing", toLog());
  running_ = false;
  if (rtp_timeout_checker_) {
    rtp_timeout_checker_->cancel();
  }
  if (rtcp_timeout_checker_) {
    rtcp_timeout_checker_->cancel();
  }
  ice_->close();
  if (dtlsRtp) {
    dtlsRtp->close();
  }
  if (dtlsRtcp) {
    dtlsRtcp->close();
  }
  this->state_ = TRANSPORT_FINISHED;
  ELOG_DEBUG("%s message: closed", toLog());
}

void DtlsTransport::onIceData(DataPacket* packet) {
  if (!running_) {
    return;
  }

  int len = packet->length;
  char *data = packet->data;
  unsigned int component_id = packet->comp;

  int length = len;
  SrtpChannel *srtp = srtp_.get();
  if (DtlsTransport::isDtlsPacket(data, len)) {
    ELOG_DEBUG("%s message: Received DTLS message, transportName: %s, componentId: %u",
               toLog(), transport_name.c_str(), component_id);
               
    if (component_id == 1) {
      dtlsRtp->read(reinterpret_cast<unsigned char*>(data), len);
    } else {
      dtlsRtcp->read(reinterpret_cast<unsigned char*>(data), len);
    }
    return;
  }
  
  if (this->getTransportState() != TRANSPORT_READY) {
    return;
  }
 
  if (dtlsRtcp != NULL && component_id == 2) {
    srtp = srtcp_.get();
  }
  
  if (srtp == NULL) {
    return;
  }
  auto unprotect_packet = std::make_shared<DataPacket>(
      component_id, data, len, VIDEO_PACKET, packet->received_time_ms);
  
  RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(unprotect_packet->data);
  if (!chead->isRtcp()) {
    if (srtp->unprotectRtp(unprotect_packet->data, &unprotect_packet->length) < 0) {
      return;
    }
  } else {
    if (srtp->unprotectRtcp(unprotect_packet->data, &unprotect_packet->length) < 0) {
      return;
    }
  }
  
  if (length <= 0) {
    return;
  }
  
  if (auto listener = getTransportListener().lock()) {
    listener->onTransportData(std::move(unprotect_packet), this);
  }
}

void DtlsTransport::updateIceState(IceState state, IceConnection *conn) {
  auto weak_transport = Transport::weak_from_this();
  worker_->task([weak_transport, state, conn, this]() {
    if (auto transport = weak_transport.lock()) {
      updateIceStateSync(state, conn);
    }
  });
}

void DtlsTransport::onCandidate(const CandidateInfo &candidate, IceConnection *conn) {
  std::weak_ptr<Transport> weak_transport = Transport::weak_from_this();
  worker_->task([candidate, weak_transport, conn]() {
    if (auto transport = weak_transport.lock()) {
      if (auto listener = transport->getTransportListener().lock()) {
        listener->onCandidate(candidate, transport.get());
      }
    }
  });
}

void DtlsTransport::write(char* data, int len) {
  if (ice_ == nullptr || !running_) {
    return;
  }
  int length = len;
  SrtpChannel *srtp = srtp_.get();

  if (this->getTransportState() == TRANSPORT_READY) {
    memcpy(protectBuf_, data, len);
    int comp = 1;
    RtcpHeader *chead = reinterpret_cast<RtcpHeader*>(protectBuf_);
    if (chead->isRtcp()) {
      if (!rtcp_mux_) {
        comp = 2;
      }
      if (dtlsRtcp != NULL) {
        srtp = srtcp_.get();
      }
      if (srtp && ice_->checkIceState() == IceState::READY) {
        if (srtp->protectRtcp(protectBuf_, &length) < 0) {
          return;
        }
      }
    } else {
      comp = 1;

      if (srtp && ice_->checkIceState() == IceState::READY) {
        if (srtp->protectRtp(protectBuf_, &length) < 0) {
          return;
        }
      }
    }
    if (length <= 10) {
      return;
    }
    if (ice_->checkIceState() == IceState::READY) {
      writeOnIce(comp, protectBuf_, length);
    }
  }
}

void DtlsTransport::onDtlsPacket(DtlsSocketContext *ctx, const unsigned char* data, unsigned int len) {
  bool is_rtcp = ctx == dtlsRtcp.get();
  int component_id = is_rtcp ? 2 : 1;

  packetPtr packet = std::make_shared<DataPacket>(component_id, data, len);

  if (is_rtcp) {
    writeDtlsPacket(dtlsRtcp.get(), packet);
  } else {
    writeDtlsPacket(dtlsRtp.get(), packet);
  }

  ELOG_DEBUG("%s message: Sending DTLS message, transportName: %s, componentId: %d",
             toLog(), transport_name.c_str(), packet->comp);
}

void DtlsTransport::writeDtlsPacket(DtlsSocketContext *ctx, packetPtr packet) {
  char data[1500];
  unsigned int len = packet->length;
  memcpy(data, packet->data, len);
  writeOnIce(packet->comp, data, len);
}

void DtlsTransport::onHandshakeCompleted(
    DtlsSocketContext *ctx, 
    std::string clientKey, 
    std::string serverKey, 
    std::string srtp_profile) {
  std::string temp;

  if (rtp_timeout_checker_) {
    rtp_timeout_checker_->cancel();
  }
  
  if (rtcp_timeout_checker_) {
    rtcp_timeout_checker_->cancel();
  }

  if (isServer_) {  // If we are server, we swap the keys
    ELOG_DEBUG("%s message: swapping keys, isServer: %d", toLog(), isServer_);
    clientKey.swap(serverKey);
  }
  
  if (ctx == dtlsRtp.get()) {
    srtp_.reset(new SrtpChannel());
    
    if (srtp_->setRtpParams(clientKey, serverKey)) {
      readyRtp = true;
    } else {
      error_code_ = TRANSPORT_ERROR_SRTP_HANDSHARK_KEY;
      updateTransportState(TRANSPORT_FAILED);
    }
    
    if (dtlsRtcp == NULL) {
      readyRtcp = true;
    }
  }
  
  if (ctx == dtlsRtcp.get()) {
    srtcp_.reset(new SrtpChannel());
    
    if (srtcp_->setRtpParams(clientKey, serverKey)) {
      readyRtcp = true;
    } else {
      error_code_ = TRANSPORT_ERROR_SRTP_HANDSHARK_KEY;
      updateTransportState(TRANSPORT_FAILED);
    }
  }
  
  ELOG_DEBUG("%s message:HandShakeCompleted, transportName:%s, readyRtp:%d, readyRtcp:%d",
             toLog(), transport_name.c_str(), readyRtp, readyRtcp);
             
  if (readyRtp && readyRtcp) {
    updateTransportState(TRANSPORT_READY);
  }
}

void DtlsTransport::onHandshakeFailed(DtlsSocketContext *ctx, const std::string& error) {
  ELOG_WARN("%s message: Handshake failed, transportName:%s, openSSLerror: %s",
            toLog(), transport_name.c_str(), error.c_str());
  running_ = false;
  error_code_ = TRANSPORT_ERROR_SRTP_HANDSHARK_FAILED;
  updateTransportState(TRANSPORT_FAILED);
}

std::string DtlsTransport::getMyFingerprint() const {
  return dtlsRtp->getFingerprint();
}

void DtlsTransport::updateIceStateSync(IceState state, IceConnection *conn) {
  if (!running_) {
    return;
  }
  ELOG_TRACE("%s message:IceState, transportName: %s, state: %d, isBundle: %d",
             toLog(), transport_name.c_str(), state, bundle_);
             
  if (state == IceState::INITIAL && this->getTransportState() != TRANSPORT_STARTED) {
    updateTransportState(TRANSPORT_STARTED);
    return;
  }

  if (state == IceState::CANDIDATES_RECEIVED && this->getTransportState() != TRANSPORT_GATHERED) {
    updateTransportState(TRANSPORT_GATHERED);
    return;
  } 

  if (state == IceState::FAILED) {
    ELOG_INFO("%s message: Ice Failed", toLog());
    running_ = false;
    error_code_ = TRANSPORT_ERROR_ICE_FAILED;
    updateTransportState(TRANSPORT_FAILED);
    return;
  }
  
  if (state == IceState::READY) {
    if (!isServer_ && dtlsRtp && !dtlsRtp->started) {
      ELOG_DEBUG("%s message: DTLSRTP Start, transportName: %s", toLog(), transport_name.c_str());
      dtlsRtp->start();
      rtp_timeout_checker_->scheduleCheck();
    }
    if (!isServer_ && dtlsRtcp != NULL && !dtlsRtcp->started) {
      ELOG_DEBUG("%s message: DTLSRTCP Start, transportName: %s", toLog(), transport_name.c_str());
      dtlsRtcp->start();
      rtcp_timeout_checker_->scheduleCheck();
    }
  }
}

void DtlsTransport::processLocalSdp(SdpInfo *localSdp) {
  ELOG_DEBUG("%s message: processing local sdp, transportName: %s", toLog(), transport_name.c_str());
  localSdp->isFingerprint = true;
  localSdp->fingerprint = getMyFingerprint();
  std::string username(ice_->getLocalUsername());
  std::string password(ice_->getLocalPassword());
  if (bundle_) {
    localSdp->setCredentials(username, password, VIDEO_TYPE);
    localSdp->setCredentials(username, password, AUDIO_TYPE);
  } else {
    localSdp->setCredentials(username, password, this->mediaType);
  }
  ELOG_DEBUG("%s message: processed local sdp, transportName: %s, ufrag: %s, pass: %s",
             toLog(), transport_name.c_str(), username.c_str(), password.c_str());
}

bool DtlsTransport::isDtlsPacket(const char* buf, int len) {
  int data = DtlsSocketContext::demuxPacket(reinterpret_cast<const unsigned char*>(buf), len);
  switch (data) {
    case DtlsSocketContext::dtls:
      return true;
    default:
      return false;
  }
}

