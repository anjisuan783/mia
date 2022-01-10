/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_VIDEO_STREAM_DECODER_H_
#define VIDEO_VIDEO_STREAM_DECODER_H_

#include <list>
#include <map>
#include <memory>
#include <vector>

#include "rtc_base/scoped_refptr.h"
#include "api/video_sink_interface.h"
#include "remote_bitrate_estimator/remote_bitrate_estimator.h"
#include "video/video_coding_defines.h"
#include "rtc_base/platform_thread.h"

namespace webrtc {

class ReceiveStatisticsProxy;
class VideoReceiver2;

class VideoStreamDecoder : public VCMReceiveCallback {
 public:
  VideoStreamDecoder(
      VideoReceiver2* video_receiver,
      ReceiveStatisticsProxy* receive_statistics_proxy,
      rtc::VideoSinkInterface<VideoFrame>* incoming_video_stream);
  ~VideoStreamDecoder() override;

  // Implements VCMReceiveCallback.
  int32_t FrameToRender(VideoFrame& video_frame,
                        std::optional<uint8_t> qp,
                        int32_t decode_time_ms,
                        VideoContentType content_type) override;
  void OnDroppedFrames(uint32_t frames_dropped) override;
  void OnIncomingPayloadType(int payload_type) override;
  void OnDecoderImplementationName(const char* implementation_name) override;

  void RegisterReceiveStatisticsProxy(
      ReceiveStatisticsProxy* receive_statistics_proxy);

 private:
  VideoReceiver2* const video_receiver_;

  ReceiveStatisticsProxy* const receive_stats_callback_;
  rtc::VideoSinkInterface<VideoFrame>* const incoming_video_stream_;
};

}  // namespace webrtc

#endif  // VIDEO_VIDEO_STREAM_DECODER_H_
