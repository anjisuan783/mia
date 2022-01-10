// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_MEDIASTREAM_H_
#define ERIZO_SRC_ERIZO_MEDIASTREAM_H_

#include <atomic>
#include <string>
#include <map>
#include <vector>

#include "erizo/logger.h"
#include "erizo/SdpInfo.h"
#include "erizo/MediaDefinitions.h"
#include "erizo/Stats.h"
#include "erizo/Transport.h"
#include "erizo/WebRtcConnection.h"
#include "erizo/pipeline/Pipeline.h"
#include "utils/Worker.h"
#include "erizo/rtp/RtcpProcessor.h"
#include "erizo/rtp/RtpExtensionProcessor.h"
#include "utils/Clock.h"
#include "erizo/pipeline/Handler.h"
#include "erizo/pipeline/HandlerManager.h"
#include "erizo/pipeline/Service.h"
#include "erizo/rtp/QualityManager.h"
#include "erizo/rtp/PacketBufferService.h"

namespace erizo {

class MediaStreamStatsListener {
 public:
    virtual ~MediaStreamStatsListener() { }
    virtual void notifyStats(const std::string& message) = 0;
};


class MediaStreamEventListener {
 public:
    virtual ~MediaStreamEventListener() { }
    virtual void notifyMediaStreamEvent(const std::string& type, 
                                        const std::string& message) = 0;
};

/**
 * A MediaStream. This class represents a Media Stream that 
 * can be established with other peers via a SDP negotiation
 */
class MediaStream final : public MediaSink, 
                          public MediaSource, 
                          public FeedbackSink,
                          public FeedbackSource, 
                          public LogContext, 
                          public HandlerManagerListener,
                          public Service,
                          public std::enable_shared_from_this<MediaStream> {
  DECLARE_LOGGER();
  static log4cxx::LoggerPtr statsLogger;

 public:
  bool audio_enabled_{false};
  bool video_enabled_{false};

  MediaStream(wa::Worker* worker, 
              std::shared_ptr<WebRtcConnection> connection,
              const std::string& media_stream_id, 
              const std::string& media_stream_label,
              bool is_publisher);
  ~MediaStream() override;
  
  bool init();
  void close() override;
  uint32_t getMaxVideoBW();
  bool setRemoteSdp(std::shared_ptr<SdpInfo> sdp);

  void setSimulcast(bool) { }

  /**
   * Sends a PLI Packet
   * @return the size of the data sent
   */
  int sendPLI() override;
  void sendPLIToFeedback();

  WebRTCEvent getCurrentState();

  /**
   * Sets the Event Listener for this MediaStream
   */
  void setMediaStreamEventListener(MediaStreamEventListener* listener);

  void notifyMediaStreamEvent(const std::string& type, const std::string& message);

  /**
   * Sets the Stats Listener for this MediaStream
   */
  inline void setMediaStreamStatsListener(MediaStreamStatsListener* listener) {
    stats_->setStatsListener(listener);
  }

  void getJSONStats(std::function<void(std::string)> callback);

  void onTransportData(std::shared_ptr<DataPacket> packet, Transport *transport);

  void setTransportInfo(std::string audio_info, std::string video_info);

  //void setFeedbackReports(bool will_send_feedback, uint32_t target_bitrate = 0);
  void muteStream(bool mute_video, bool mute_audio);
  //void setVideoConstraints(int max_video_width, int max_video_height, int max_video_frame_rate);

  void setMetadata(std::map<std::string, std::string> metadata);

  void read(std::shared_ptr<DataPacket> packet);
  void write(std::shared_ptr<DataPacket> packet);

  void enableHandler(const std::string &name);
  void disableHandler(const std::string &name);
  void notifyUpdateToHandlers() override;

  void notifyToEventSink(MediaEventPtr event);

  void initializeStats();
  void printStats();

  bool isAudioMuted() { return audio_muted_; }
  bool isVideoMuted() { return video_muted_; }

  std::shared_ptr<SdpInfo> getRemoteSdpInfo() { return remote_sdp_; }

  RtpExtensionProcessor& getRtpExtensionProcessor() { return connection_->getRtpExtensionProcessor(); }
  wa::Worker* getWorker() { return worker_; }

  std::string getId() { return stream_id_; }
  std::string getLabel() { return mslabel_; }

  bool isSourceSSRC(uint32_t ssrc);
  bool isSinkSSRC(uint32_t ssrc);
  void parseIncomingPayloadType(char *buf, int len, packetType type);

  bool isPipelineInitialized() { return pipeline_initialized_; }
  bool isRunning() { return pipeline_initialized_ && sending_; }
  Pipeline::Ptr getPipeline() { return pipeline_; }
  bool isPublisher() { return is_publisher_; }
  void setBitrateFromMaxQualityLayer(uint64_t bitrate) { }

  inline std::string toLog() {
    return "id:" + stream_id_ + ", role:" + 
        (is_publisher_ ? "publisher" : "subscriber") + ", " + printLogContext();
  }

 private:
  void sendPacket(std::shared_ptr<DataPacket> packet);
  void sendPacket_i(std::shared_ptr<DataPacket> packet);
  int deliverAudioData_(std::shared_ptr<DataPacket> audio_packet) override;
  int deliverVideoData_(std::shared_ptr<DataPacket> video_packet) override;
  int deliverFeedback_(std::shared_ptr<DataPacket> fb_packet) override;
  int deliverEvent_(MediaEventPtr event) override;
  void initializePipeline();
  void transferLayerStats(std::string spatial, std::string temporal);
  void transferMediaStats(std::string target_node, std::string source_parent, std::string source_node);

  void changeDeliverPayloadType(DataPacket *dp, packetType type);
  // parses incoming payload type, replaces occurence in buf

 private:
  MediaStreamEventListener* media_stream_event_listener_{nullptr};
  std::shared_ptr<WebRtcConnection> connection_;
  bool should_send_feedback_{true};
  bool sending_{true};
  int bundle_{false};

  std::string stream_id_;
  std::string mslabel_;

  uint32_t rate_control_{0};  // Target bitrate for hacky rate control in BPS

  wa::time_point now_, mark_;

  std::shared_ptr<RtcpProcessor> rtcp_processor_;
  std::shared_ptr<Stats> stats_;
  std::shared_ptr<Stats> log_stats_;

  Pipeline::Ptr pipeline_;

  wa::Worker* worker_;

  bool audio_muted_{false};
  bool video_muted_{false};
  bool pipeline_initialized_{false};
  bool is_publisher_;

 protected:
  std::shared_ptr<SdpInfo> remote_sdp_;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_MEDIASTREAM_H_

