/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/encoded_frame.h"

#include <string.h>

#include "absl/types/variant.h"
#include "api/video_timing.h"
#include "video/common_constants.h"

namespace webrtc {

VCMEncodedFrame::VCMEncodedFrame()
    : webrtc::EncodedImage(),
      _renderTimeMs(-1),
      _payloadType(0),
      _missingFrame(false),
      _codec(kVideoCodecGeneric) {
  _codecSpecificInfo.codecType = kVideoCodecGeneric;
}

VCMEncodedFrame::VCMEncodedFrame(const VCMEncodedFrame&) = default;

VCMEncodedFrame::~VCMEncodedFrame() {
  Reset();
}

void VCMEncodedFrame::Reset() {
  SetTimestamp(0);
  SetSpatialIndex(std::nullopt);
  _renderTimeMs = -1;
  _payloadType = 0;
  _frameType = VideoFrameType::kVideoFrameDelta;
  _encodedWidth = 0;
  _encodedHeight = 0;
  _completeFrame = false;
  _missingFrame = false;
  set_size(0);
  _codecSpecificInfo.codecType = kVideoCodecGeneric;
  _codec = kVideoCodecGeneric;
  rotation_ = kVideoRotation_0;
  content_type_ = VideoContentType::UNSPECIFIED;
  timing_.flags = VideoSendTiming::kInvalid;
}

void VCMEncodedFrame::CopyCodecSpecific(const RTPVideoHeader* header) {
  if (header) {
    switch (header->codec) {
      case kVideoCodecH264: {
        _codecSpecificInfo.codecType = kVideoCodecH264;

        // The following H264 codec specific data are not used elsewhere.
        // Instead they are read directly from the frame marking extension.
        // These codec specific data structures should be removed
        // when frame marking is used.
        _codecSpecificInfo.codecSpecific.H264.temporal_idx = kNoTemporalIdx;
        if (header->frame_marking.temporal_id != kNoTemporalIdx) {
          _codecSpecificInfo.codecSpecific.H264.temporal_idx =
              header->frame_marking.temporal_id;
          _codecSpecificInfo.codecSpecific.H264.base_layer_sync =
              header->frame_marking.base_layer_sync;
          _codecSpecificInfo.codecSpecific.H264.idr_frame =
              header->frame_marking.independent_frame;
        }
        break;
      }
      default: {
        _codecSpecificInfo.codecType = kVideoCodecGeneric;
        break;
      }
    }
  }
}

}  // namespace webrtc
