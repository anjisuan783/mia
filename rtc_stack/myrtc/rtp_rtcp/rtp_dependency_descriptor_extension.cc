/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtp_rtcp/rtp_dependency_descriptor_extension.h"

#include <cstdint>

#include "rtc_base/array_view.h"
#include "video/generic_frame_info.h"
#include "rtp_rtcp/rtp_rtcp_defines.h"
#include "rtp_rtcp/rtp_dependency_descriptor_reader.h"
#include "rtp_rtcp/rtp_dependency_descriptor_writer.h"
#include "rtc_base/divide_round.h"

namespace webrtc {

constexpr RTPExtensionType RtpDependencyDescriptorExtension::kId;
constexpr char RtpDependencyDescriptorExtension::kUri[];

bool RtpDependencyDescriptorExtension::Parse(
    rtc::ArrayView<const uint8_t> data,
    const FrameDependencyStructure* structure,
    DependencyDescriptor* descriptor) {
  RtpDependencyDescriptorReader reader(data, structure, descriptor);
  return reader.ParseSuccessful();
}

size_t RtpDependencyDescriptorExtension::ValueSize(
    const FrameDependencyStructure& structure,
    const DependencyDescriptor& descriptor) {
  RtpDependencyDescriptorWriter writer(/*data=*/{}, structure, descriptor);
  return DivideRoundUp(writer.ValueSizeBits(), 8);
}

bool RtpDependencyDescriptorExtension::Write(
    rtc::ArrayView<uint8_t> data,
    const FrameDependencyStructure& structure,
    const DependencyDescriptor& descriptor) {
  RtpDependencyDescriptorWriter writer(data, structure, descriptor);
  return writer.Write();
}

}  // namespace webrtc
