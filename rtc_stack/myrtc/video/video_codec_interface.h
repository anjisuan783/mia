/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_INCLUDE_VIDEO_CODEC_INTERFACE_H_
#define MODULES_VIDEO_CODING_INCLUDE_VIDEO_CODEC_INTERFACE_H_

#include <vector>

#include "optional"
#include "api/video_frame.h"
#include "api/video_decoder.h"
#include "api/video_encoder.h"
#include "video/generic_frame_info.h"
#include "module/module_common_types.h"
#include "video/h264_globals.h"
#include "video/video_error_codes.h"
#include "rtc_base/rtc_export.h"

namespace webrtc {

// Note: If any pointers are added to this struct, it must be fitted
// with a copy-constructor. See below.
// Hack alert - the code assumes that thisstruct is memset when constructed.

// Hack alert - the code assumes that thisstruct is memset when constructed.
struct CodecSpecificInfoH264 {
  H264PacketizationMode packetization_mode;
  uint8_t temporal_idx;
  bool base_layer_sync;
  bool idr_frame;
};

static_assert(std::is_pod<CodecSpecificInfoH264>::value, "");

union CodecSpecificInfoUnion {
  CodecSpecificInfoH264 H264;
};
static_assert(std::is_pod<CodecSpecificInfoUnion>::value, "");

// Note: if any pointers are added to this struct or its sub-structs, it
// must be fitted with a copy-constructor. This is because it is copied
// in the copy-constructor of VCMEncodedFrame.
struct RTC_EXPORT CodecSpecificInfo {
  CodecSpecificInfo();
  CodecSpecificInfo(const CodecSpecificInfo&);
  ~CodecSpecificInfo();

  VideoCodecType codecType;
  CodecSpecificInfoUnion codecSpecific;
  std::optional<GenericFrameInfo> generic_frame_info;
  std::optional<FrameDependencyStructure> template_structure;
};

}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_INCLUDE_VIDEO_CODEC_INTERFACE_H_
