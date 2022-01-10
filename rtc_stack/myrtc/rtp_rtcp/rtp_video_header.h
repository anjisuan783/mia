/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_HEADER_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_HEADER_H_

#include <cstdint>

#include "absl/container/inlined_vector.h"
#include "optional"
#include "absl/types/variant.h"
#include "api/color_space.h"
#include "api/video_codec_type.h"
#include "api/video_content_type.h"
#include "api/video_frame_marking.h"
#include "api/video_frame_type.h"
#include "api/video_rotation.h"
#include "api/video_timing.h"
#include "common_types.h"  // NOLINT(build/include)
#include "video/h264_globals.h"

namespace webrtc {

using RTPVideoTypeHeader = absl::variant<absl::monostate,
                                         RTPVideoHeaderH264>;

struct RTPVideoHeader {
  struct GenericDescriptorInfo {
    GenericDescriptorInfo();
    GenericDescriptorInfo(const GenericDescriptorInfo& other);
    ~GenericDescriptorInfo();

    int64_t frame_id = 0;
    int spatial_index = 0;
    int temporal_index = 0;
    absl::InlinedVector<int64_t, 5> dependencies;
    absl::InlinedVector<int, 5> higher_spatial_layers;
    bool discardable = false;
  };

  RTPVideoHeader();
  RTPVideoHeader(const RTPVideoHeader& other);

  ~RTPVideoHeader();

  std::optional<GenericDescriptorInfo> generic;

  VideoFrameType frame_type = VideoFrameType::kEmptyFrame;
  uint16_t width = 0;
  uint16_t height = 0;
  VideoRotation rotation = VideoRotation::kVideoRotation_0;
  VideoContentType content_type = VideoContentType::UNSPECIFIED;
  bool is_first_packet_in_frame = false;
  bool is_last_packet_in_frame = false;
  uint8_t simulcastIdx = 0;
  VideoCodecType codec = VideoCodecType::kVideoCodecGeneric;

  PlayoutDelay playout_delay{-1, -1};
  VideoSendTiming video_timing;
  FrameMarking frame_marking{false, false, false, false, false, 0xFF, 0, 0};
  std::optional<ColorSpace> color_space;
  RTPVideoTypeHeader video_type_header;
};

}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTP_VIDEO_HEADER_H_
