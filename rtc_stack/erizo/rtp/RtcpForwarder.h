// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_RTP_RTCPFORWARDER_H_
#define ERIZO_SRC_ERIZO_RTP_RTCPFORWARDER_H_

#include <map>
#include <list>
#include <set>
#include <memory>

#include "erizo/logger.h"
#include "erizo/MediaDefinitions.h"
#include "erizo/SdpInfo.h"
#include "erizo/rtp/RtpHeaders.h"
#include "erizo/rtp/RtcpProcessor.h"

namespace erizo {

class RtcpForwarder: public RtcpProcessor{
  DECLARE_LOGGER();

 public:
  RtcpForwarder(MediaSink* msink, 
                MediaSource* msource, 
                uint32_t max_video_bw = 300000);
  virtual ~RtcpForwarder() {}
  void addSourceSsrc(uint32_t ssrc) override;
  void analyzeSr(RtcpHeader* chead) override;
  int analyzeFeedback(char* buf, int len) override;

 private:
  static const int RR_AUDIO_PERIOD = 2000;
  static const int RR_VIDEO_BASE = 800;
  static const int REMB_TIMEOUT = 1000;
  std::map<uint32_t, std::shared_ptr<RtcpData>> rtcpData_;
  int addREMB(char* buf, int len, uint32_t bitrate);
  int addNACK(char* buf, int len, uint16_t seqNum, 
              uint16_t blp, uint32_t sourceSsrc, uint32_t sinkSsrc);
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_RTCPFORWARDER_H_
