// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_WEBRTCCONNECTION_H_
#define ERIZO_SRC_ERIZO_WEBRTCCONNECTION_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "erizo/logger.h"

#include "erizo/MediaDefinitions.h"
#include "erizo/Transport.h"
#include "erizo/Stats.h"

namespace erizo {

constexpr std::chrono::milliseconds kBitrateControlPeriod(100);
constexpr uint32_t kDefaultVideoSinkSSRC = 55555;
constexpr uint32_t kDefaultAudioSinkSSRC = 44444;


class MediaStream;
class SdpInfo;
class RtpExtensionProcessor;

/**
 * WebRTC Events
 */
enum WebRTCEvent {
  CONN_INITIAL = 101,       // call init()   msg=""  stream_id=""
  CONN_STARTED = 102,       // TRANSPORT_STARTED msg="" stream_id=""
  CONN_GATHERED = 103,      // TRANSPORT_GATHERED msg="localsdp" stream_id=""
  CONN_READY = 104,         // TRANSPORT_READY msg="" stream_id=""
  CONN_FINISHED = 105,      // do not callback 
  CONN_CANDIDATE = 201,     // onCandidate msg="Candidate" stream_id=""
  //CONN_SDP = 202,         // no use
  CONN_SDP_PROCESSED = 203, // each setRemoteSdpsToMediaStreams msg="localsdp" stream_id="mid"
  CONN_FAILED = 500         // TRANSPORT_FAILED msg="remotesdp" stream_id=""
};

class WebRtcConnectionEventListener {
 public:
  virtual ~WebRtcConnectionEventListener() { }
  virtual void notifyEvent(WebRTCEvent newEvent,
                           const std::string& message, 
                           const std::string &stream_id = "") = 0;
};


/**
 * A WebRTC Connection. This class represents a WebRTC Connection that
 * can be established with other peers via a SDP negotiation
 * it comprises all the necessary Transport components.
 */
class WebRtcConnection: public TransportListener, 
                        public LogContext,
                        public std::enable_shared_from_this<WebRtcConnection> {
  DECLARE_LOGGER();

 public:
  WebRtcConnection(wa::Worker* worker, 
                   wa::IOWorker* io_worker,
                   const std::string& connection_id, 
                   const IceConfig& ice_config,
                   const std::vector<RtpMap>& rtp_mappings, 
                   const std::vector<erizo::ExtMap>& ext_mappings,
                   WebRtcConnectionEventListener* listener);
 
  ~WebRtcConnection() override;
  /**
   * Inits the WebConnection by starting ICE Candidate Gathering.
   * @return True if the candidates are gathered.
   */
  bool init();
  void close();

  bool setRemoteSdpInfo(std::shared_ptr<SdpInfo> sdp, std::string stream_id);
  /**
   * Sets the SDP of the remote peer.
   * @param sdp The SDP.
   * @return true if the SDP was received correctly.
   */
  bool setRemoteSdp(const std::string &sdp, const std::string& stream_id);

  bool createOffer(bool video_enabled, bool audio_enabled, bool bundle);
  /**
   * Add new remote candidate (from remote peer).
   * @param sdp The candidate in SDP format.
   * @return true if the SDP was received correctly.
   */
  bool addRemoteCandidate(const std::string &mid, int mLineIndex, const std::string &sdp);
  /**
   * Remove remote candidate (from remote peer).
   * @param sdp The candidate in SDP format.
   * @return true if the SDP was received correctly.
   */
  bool removeRemoteCandidate(const std::string &mid, int mLineIndex, const std::string &sdp);

  /**
   * Obtains the local SDP.
   * @return The SDP as a SdpInfo.
   */
  std::shared_ptr<SdpInfo> getLocalSdpInfo();
  /**
   * Obtains the local SDP.
   * @return The SDP as a string.
   */
  std::string getLocalSdp();

  /**
   * Gets the current state of the Ice Connection
   * @return
   */
  WebRTCEvent getCurrentState();

  void onTransportData(std::shared_ptr<DataPacket> packet, Transport *transport) override;

  void updateState(TransportState state, Transport * transport) override;

  void onCandidate(const CandidateInfo& cand, Transport *transport) override;

  void setMetadata(std::map<std::string, std::string> metadata);

  void write(std::shared_ptr<DataPacket> packet);

  bool isAudioMuted() { return audio_muted_; }
  bool isVideoMuted() { return video_muted_; }

  void addMediaStream(std::shared_ptr<MediaStream> media_stream);
  void removeMediaStream(const std::string& stream_id);
  void forEachMediaStream(std::function<void(const std::shared_ptr<MediaStream>&)> func);

  void setTransport(std::shared_ptr<Transport> transport);  // Only for Testing purposes

  std::shared_ptr<Stats> getStatsService() { return stats_; }

  RtpExtensionProcessor& getRtpExtensionProcessor() { return *extension_processor_; }

  inline std::string toLog() {
    return "id: " + connection_id_ + ", " + printLogContext();
  }

 private:
  bool processRemoteSdp(const std::string& stream_id);
  void setRemoteSdpsToMediaStreams(const std::string& stream_id);
  void onRemoteSdpsSetToMediaStreams(const std::string& stream_id);
  std::string getJSONCandidate(const std::string& mid, const std::string& sdp);
  void trackTransportInfo();
  void onRtcpFromTransport(DataPacket* packet, Transport *transport);
  void onREMBFromTransport(RtcpHeader *chead, Transport *transport);
  void maybeNotifyWebRtcConnectionEvent(const WebRTCEvent& event, 
      const std::string& message, const std::string& stream_id = "");
 private:
  std::string connection_id_;
  bool audio_enabled_{false};
  bool video_enabled_{false};
  bool trickle_enabled_; // candidates exclude in answer
  bool sending_{true};
  int bundle_{false};
  WebRtcConnectionEventListener* conn_event_listener_;
  IceConfig ice_config_;
  std::vector<RtpMap> rtp_mappings_;
  std::unique_ptr<RtpExtensionProcessor> extension_processor_;

  std::shared_ptr<Transport> video_transport_, audio_transport_;
  WebRTCEvent global_state_{CONN_INITIAL};

  wa::Worker* worker_;
  wa::IOWorker* io_worker_;
  std::vector<std::shared_ptr<MediaStream>> media_streams_;
  std::shared_ptr<SdpInfo> remote_sdp_;
  std::shared_ptr<SdpInfo> local_sdp_;
  bool audio_muted_{false};
  bool video_muted_{false};
  bool first_remote_sdp_processed_{false};
  std::unordered_map<std::string, uint32_t> mapping_ssrcs_;
  std::shared_ptr<Stats> stats_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_WEBRTCCONNECTION_H_

