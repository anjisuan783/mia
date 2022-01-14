/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_payload_params.h"

#include <stddef.h>

#include <algorithm>

#include "absl/container/inlined_vector.h"
#include "absl/types/variant.h"
#include "api/video_timing.h"
#include "video/h264_globals.h"
#include "video/common_constants.h"
#include "rtc_base/arraysize.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/random.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/field_trial.h"

namespace webrtc {

namespace {
void PopulateRtpWithCodecSpecifics(const CodecSpecificInfo& info,
                                   std::optional<int> spatial_index,
                                   RTPVideoHeader* rtp) {
  rtp->codec = info.codecType;
  switch (info.codecType) {
    case kVideoCodecH264: {
      auto& h264_header = rtp->video_type_header.emplace<RTPVideoHeaderH264>();
      h264_header.packetization_mode =
          info.codecSpecific.H264.packetization_mode;
      rtp->simulcastIdx = spatial_index.value_or(0);
      rtp->frame_marking.temporal_id = kNoTemporalIdx;
      if (info.codecSpecific.H264.temporal_idx != kNoTemporalIdx) {
        rtp->frame_marking.temporal_id = info.codecSpecific.H264.temporal_idx;
        rtp->frame_marking.layer_id = 0;
        rtp->frame_marking.independent_frame =
            info.codecSpecific.H264.idr_frame;
        rtp->frame_marking.base_layer_sync =
            info.codecSpecific.H264.base_layer_sync;
      }
      return;
    }
    case kVideoCodecMultiplex:
    case kVideoCodecGeneric:
      rtp->codec = kVideoCodecGeneric;
      rtp->simulcastIdx = spatial_index.value_or(0);
      return;
    default:
      return;
  }
}

void SetVideoTiming(const EncodedImage& image, VideoSendTiming* timing) {
  if (image.timing_.flags == VideoSendTiming::TimingFrameFlags::kInvalid ||
      image.timing_.flags == VideoSendTiming::TimingFrameFlags::kNotTriggered) {
    timing->flags = VideoSendTiming::TimingFrameFlags::kInvalid;
    return;
  }

  timing->encode_start_delta_ms = VideoSendTiming::GetDeltaCappedMs(
      image.capture_time_ms_, image.timing_.encode_start_ms);
  timing->encode_finish_delta_ms = VideoSendTiming::GetDeltaCappedMs(
      image.capture_time_ms_, image.timing_.encode_finish_ms);
  timing->packetization_finish_delta_ms = 0;
  timing->pacer_exit_delta_ms = 0;
  timing->network_timestamp_delta_ms = 0;
  timing->network2_timestamp_delta_ms = 0;
  timing->flags = image.timing_.flags;
}
}  // namespace

RtpPayloadParams::RtpPayloadParams(const uint32_t ssrc,
                                   const RtpPayloadState* state)
    : ssrc_(ssrc),
      generic_picture_id_experiment_(
          field_trial::IsEnabled("WebRTC-GenericPictureId")),
      generic_descriptor_experiment_(
          field_trial::IsEnabled("WebRTC-GenericDescriptor")) {
  for (auto& spatial_layer : last_shared_frame_id_)
    spatial_layer.fill(-1);


  Random random(rtc::TimeMicros());
  state_.picture_id =
      state ? state->picture_id : (random.Rand<int16_t>() & 0x7FFF);
  state_.tl0_pic_idx = state ? state->tl0_pic_idx : (random.Rand<uint8_t>());
}

RtpPayloadParams::RtpPayloadParams(const RtpPayloadParams& other) = default;

RtpPayloadParams::~RtpPayloadParams() {}

RTPVideoHeader RtpPayloadParams::GetRtpVideoHeader(
    const EncodedImage& image,
    const CodecSpecificInfo* codec_specific_info,
    int64_t shared_frame_id) {
  RTPVideoHeader rtp_video_header;
  if (codec_specific_info) {
    PopulateRtpWithCodecSpecifics(*codec_specific_info, image.SpatialIndex(),
                                  &rtp_video_header);
  }
  rtp_video_header.frame_type = image._frameType,
  rtp_video_header.rotation = image.rotation_;
  rtp_video_header.content_type = image.content_type_;
  rtp_video_header.playout_delay = image.playout_delay_;
  rtp_video_header.width = image._encodedWidth;
  rtp_video_header.height = image._encodedHeight;
  rtp_video_header.color_space = image.ColorSpace()
                                     ? std::make_optional(*image.ColorSpace())
                                     : std::nullopt;
  SetVideoTiming(image, &rtp_video_header.video_timing);

  const bool is_keyframe = image._frameType == VideoFrameType::kVideoFrameKey;
  const bool first_frame_in_picture = true;
      //(codec_specific_info && codec_specific_info->codecType == kVideoCodecVP9)
       //   ? codec_specific_info->codecSpecific.VP9.first_frame_in_picture
       //   : true;

  SetCodecSpecific(&rtp_video_header, first_frame_in_picture);

  if (generic_descriptor_experiment_)
    SetGeneric(codec_specific_info, shared_frame_id, is_keyframe,
               &rtp_video_header);

  return rtp_video_header;
}

uint32_t RtpPayloadParams::ssrc() const {
  return ssrc_;
}

RtpPayloadState RtpPayloadParams::state() const {
  return state_;
}

void RtpPayloadParams::SetCodecSpecific(RTPVideoHeader* rtp_video_header,
                                        bool first_frame_in_picture) {
  // Always set picture id. Set tl0_pic_idx iff temporal index is set.
  if (first_frame_in_picture) {
    state_.picture_id = (static_cast<uint16_t>(state_.picture_id) + 1) & 0x7FFF;
  }
  
  if (rtp_video_header->codec == kVideoCodecH264) {
    if (rtp_video_header->frame_marking.temporal_id != kNoTemporalIdx) {
      if (rtp_video_header->frame_marking.temporal_id == 0) {
        ++state_.tl0_pic_idx;
      }
      rtp_video_header->frame_marking.tl0_pic_idx = state_.tl0_pic_idx;
    }
  }
  // There are currently two generic descriptors in WebRTC. The old descriptor
  // can not share a picture id space between simulcast streams, so we use the
  // |picture_id| in this case. We let the |picture_id| tag along in |frame_id|
  // until the old generic format can be removed.
  // TODO(philipel): Remove this when the new generic format has been fully
  //                 implemented.
  if (generic_picture_id_experiment_ &&
      rtp_video_header->codec == kVideoCodecGeneric) {
    rtp_video_header->generic.emplace().frame_id = state_.picture_id;
  }
}

void RtpPayloadParams::SetGeneric(const CodecSpecificInfo* codec_specific_info,
                                  int64_t frame_id,
                                  bool is_keyframe,
                                  RTPVideoHeader* rtp_video_header) {
  switch (rtp_video_header->codec) {
    case VideoCodecType::kVideoCodecGeneric:
      GenericToGeneric(frame_id, is_keyframe, rtp_video_header);
      return;
    case VideoCodecType::kVideoCodecH264:
      if (codec_specific_info) {
        H264ToGeneric(codec_specific_info->codecSpecific.H264, frame_id,
                      is_keyframe, rtp_video_header);
      }
      return;
    case VideoCodecType::kVideoCodecMultiplex:
      return;
    case kVideoCodecVP8:
    case kVideoCodecVP9:
      ;
  }
  RTC_NOTREACHED() << "Unsupported codec.";
}

void RtpPayloadParams::GenericToGeneric(int64_t shared_frame_id,
                                        bool is_keyframe,
                                        RTPVideoHeader* rtp_video_header) {
  RTPVideoHeader::GenericDescriptorInfo& generic =
      rtp_video_header->generic.emplace();

  generic.frame_id = shared_frame_id;

  if (is_keyframe) {
    last_shared_frame_id_[0].fill(-1);
  } else {
    int64_t frame_id = last_shared_frame_id_[0][0];
    RTC_DCHECK_NE(frame_id, -1);
    RTC_DCHECK_LT(frame_id, shared_frame_id);
    generic.dependencies.push_back(frame_id);
  }

  last_shared_frame_id_[0][0] = shared_frame_id;
}

void RtpPayloadParams::H264ToGeneric(const CodecSpecificInfoH264& h264_info,
                                     int64_t shared_frame_id,
                                     bool is_keyframe,
                                     RTPVideoHeader* rtp_video_header) {
  const int temporal_index =
      h264_info.temporal_idx != kNoTemporalIdx ? h264_info.temporal_idx : 0;

  if (temporal_index >= RtpGenericFrameDescriptor::kMaxTemporalLayers) {
    RTC_LOG(LS_WARNING) << "Temporal and/or spatial index is too high to be "
                           "used with generic frame descriptor.";
    return;
  }

  RTPVideoHeader::GenericDescriptorInfo& generic =
      rtp_video_header->generic.emplace();

  generic.frame_id = shared_frame_id;
  generic.temporal_index = temporal_index;

  if (is_keyframe) {
    RTC_DCHECK_EQ(temporal_index, 0);
    last_shared_frame_id_[/*spatial index*/ 0].fill(-1);
    last_shared_frame_id_[/*spatial index*/ 0][temporal_index] =
        shared_frame_id;
    return;
  }

  if (h264_info.base_layer_sync) {
    int64_t tl0_frame_id = last_shared_frame_id_[/*spatial index*/ 0][0];

    for (int i = 1; i < RtpGenericFrameDescriptor::kMaxTemporalLayers; ++i) {
      if (last_shared_frame_id_[/*spatial index*/ 0][i] < tl0_frame_id) {
        last_shared_frame_id_[/*spatial index*/ 0][i] = -1;
      }
    }

    RTC_DCHECK_GE(tl0_frame_id, 0);
    RTC_DCHECK_LT(tl0_frame_id, shared_frame_id);
    generic.dependencies.push_back(tl0_frame_id);
  } else {
    for (int i = 0; i <= temporal_index; ++i) {
      int64_t frame_id = last_shared_frame_id_[/*spatial index*/ 0][i];

      if (frame_id != -1) {
        RTC_DCHECK_LT(frame_id, shared_frame_id);
        generic.dependencies.push_back(frame_id);
      }
    }
  }

  last_shared_frame_id_[/*spatial_index*/ 0][temporal_index] = shared_frame_id;
}

}  // namespace webrtc
