/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/rtp_frame_reference_finder.h"

#include <algorithm>
#include <limits>

#include "absl/types/variant.h"
#include "video/frame_object.h"
#include "video/packet_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/fallthrough.h"

namespace webrtc {
namespace video_coding {

RtpFrameReferenceFinder::RtpFrameReferenceFinder(
    OnCompleteFrameCallback* frame_callback)
    : RtpFrameReferenceFinder(frame_callback, 0) {}

RtpFrameReferenceFinder::RtpFrameReferenceFinder(
    OnCompleteFrameCallback* frame_callback,
    int64_t picture_id_offset)
    : last_picture_id_(-1),
      current_ss_idx_(0),
      cleared_to_seq_num_(-1),
      frame_callback_(frame_callback),
      picture_id_offset_(picture_id_offset) {}

RtpFrameReferenceFinder::~RtpFrameReferenceFinder() = default;

void RtpFrameReferenceFinder::ManageFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  // If we have cleared past this frame, drop it.
  if (cleared_to_seq_num_ != -1 &&
      AheadOf<uint16_t>(cleared_to_seq_num_, frame->first_seq_num())) {
    return;
  }

  FrameDecision decision = ManageFrameInternal(frame.get());

  switch (decision) {
    case kStash:
      if (stashed_frames_.size() > kMaxStashedFrames)
        stashed_frames_.pop_back();
      stashed_frames_.push_front(std::move(frame));
      break;
    case kHandOff:
      HandOffFrame(std::move(frame));
      RetryStashedFrames();
      break;
    case kDrop:
      break;
  }
}

void RtpFrameReferenceFinder::RetryStashedFrames() {
  bool complete_frame = false;
  do {
    complete_frame = false;
    for (auto frame_it = stashed_frames_.begin();
         frame_it != stashed_frames_.end();) {
      FrameDecision decision = ManageFrameInternal(frame_it->get());

      switch (decision) {
        case kStash:
          ++frame_it;
          break;
        case kHandOff:
          complete_frame = true;
          HandOffFrame(std::move(*frame_it));
          RTC_FALLTHROUGH();
        case kDrop:
          frame_it = stashed_frames_.erase(frame_it);
      }
    }
  } while (complete_frame);
}

void RtpFrameReferenceFinder::HandOffFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  frame->id.picture_id += picture_id_offset_;
  for (size_t i = 0; i < frame->num_references; ++i) {
    frame->references[i] += picture_id_offset_;
  }

  frame_callback_->OnCompleteFrame(std::move(frame));
}

RtpFrameReferenceFinder::FrameDecision
RtpFrameReferenceFinder::ManageFrameInternal(RtpFrameObject* frame) {
  std::optional<RtpGenericFrameDescriptor> generic_descriptor =
      frame->GetGenericFrameDescriptor();
  if (generic_descriptor) {
    return ManageFrameGeneric(frame, *generic_descriptor);
  }

  switch (frame->codec_type()) {
    case kVideoCodecH264:
      return ManageFrameH264(frame);
    default: {
      // Use 15 first bits of frame ID as picture ID if available.
      const RTPVideoHeader& video_header = frame->GetRtpVideoHeader();
      int picture_id = kNoPictureId;
      if (video_header.generic)
        picture_id = video_header.generic->frame_id & 0x7fff;

      return ManageFramePidOrSeqNum(frame, picture_id);
    }
  }
}

void RtpFrameReferenceFinder::PaddingReceived(uint16_t seq_num) {
  auto clean_padding_to =
      stashed_padding_.lower_bound(seq_num - kMaxPaddingAge);
  stashed_padding_.erase(stashed_padding_.begin(), clean_padding_to);
  stashed_padding_.insert(seq_num);
  UpdateLastPictureIdWithPadding(seq_num);
  RetryStashedFrames();
}

void RtpFrameReferenceFinder::ClearTo(uint16_t seq_num) {
  cleared_to_seq_num_ = seq_num;

  auto it = stashed_frames_.begin();
  while (it != stashed_frames_.end()) {
    if (AheadOf<uint16_t>(cleared_to_seq_num_, (*it)->first_seq_num())) {
      it = stashed_frames_.erase(it);
    } else {
      ++it;
    }
  }
}

void RtpFrameReferenceFinder::UpdateLastPictureIdWithPadding(uint16_t seq_num) {
  auto gop_seq_num_it = last_seq_num_gop_.upper_bound(seq_num);

  // If this padding packet "belongs" to a group of pictures that we don't track
  // anymore, do nothing.
  if (gop_seq_num_it == last_seq_num_gop_.begin())
    return;
  --gop_seq_num_it;

  // Calculate the next contiuous sequence number and search for it in
  // the padding packets we have stashed.
  uint16_t next_seq_num_with_padding = gop_seq_num_it->second.second + 1;
  auto padding_seq_num_it =
      stashed_padding_.lower_bound(next_seq_num_with_padding);

  // While there still are padding packets and those padding packets are
  // continuous, then advance the "last-picture-id-with-padding" and remove
  // the stashed padding packet.
  while (padding_seq_num_it != stashed_padding_.end() &&
         *padding_seq_num_it == next_seq_num_with_padding) {
    gop_seq_num_it->second.second = next_seq_num_with_padding;
    ++next_seq_num_with_padding;
    padding_seq_num_it = stashed_padding_.erase(padding_seq_num_it);
  }

  // In the case where the stream has been continuous without any new keyframes
  // for a while there is a risk that new frames will appear to be older than
  // the keyframe they belong to due to wrapping sequence number. In order
  // to prevent this we advance the picture id of the keyframe every so often.
  if (ForwardDiff(gop_seq_num_it->first, seq_num) > 10000) {
    RTC_DCHECK_EQ(1ul, last_seq_num_gop_.size());
    last_seq_num_gop_[seq_num] = gop_seq_num_it->second;
    last_seq_num_gop_.erase(gop_seq_num_it);
  }
}

RtpFrameReferenceFinder::FrameDecision
RtpFrameReferenceFinder::ManageFrameGeneric(
    RtpFrameObject* frame,
    const RtpGenericFrameDescriptor& descriptor) {
  int64_t frame_id = generic_frame_id_unwrapper_.Unwrap(descriptor.FrameId());
  frame->id.picture_id = frame_id;
  frame->id.spatial_layer = descriptor.SpatialLayer();

  rtc::ArrayView<const uint16_t> diffs = descriptor.FrameDependenciesDiffs();
  if (EncodedFrame::kMaxFrameReferences < diffs.size()) {
    RTC_LOG(LS_WARNING) << "Too many dependencies in generic descriptor.";
    return kDrop;
  }

  frame->num_references = diffs.size();
  for (size_t i = 0; i < diffs.size(); ++i)
    frame->references[i] = frame_id - diffs[i];

  return kHandOff;
}

RtpFrameReferenceFinder::FrameDecision
RtpFrameReferenceFinder::ManageFramePidOrSeqNum(RtpFrameObject* frame,
                                                int picture_id) {
  // If |picture_id| is specified then we use that to set the frame references,
  // otherwise we use sequence number.
  if (picture_id != kNoPictureId) {
    frame->id.picture_id = unwrapper_.Unwrap(picture_id);
    frame->num_references =
        frame->frame_type() == VideoFrameType::kVideoFrameKey ? 0 : 1;
    frame->references[0] = frame->id.picture_id - 1;
    return kHandOff;
  }

  if (frame->frame_type() == VideoFrameType::kVideoFrameKey) {
    last_seq_num_gop_.insert(std::make_pair(
        frame->last_seq_num(),
        std::make_pair(frame->last_seq_num(), frame->last_seq_num())));
  }

  // We have received a frame but not yet a keyframe, stash this frame.
  if (last_seq_num_gop_.empty())
    return kStash;

  // Clean up info for old keyframes but make sure to keep info
  // for the last keyframe.
  auto clean_to = last_seq_num_gop_.lower_bound(frame->last_seq_num() - 100);
  for (auto it = last_seq_num_gop_.begin();
       it != clean_to && last_seq_num_gop_.size() > 1;) {
    it = last_seq_num_gop_.erase(it);
  }

  // Find the last sequence number of the last frame for the keyframe
  // that this frame indirectly references.
  auto seq_num_it = last_seq_num_gop_.upper_bound(frame->last_seq_num());
  if (seq_num_it == last_seq_num_gop_.begin()) {
    RTC_LOG(LS_WARNING) << "Generic frame with packet range ["
                        << frame->first_seq_num() << ", "
                        << frame->last_seq_num()
                        << "] has no GoP, dropping frame.";
    return kDrop;
  }
  seq_num_it--;

  // Make sure the packet sequence numbers are continuous, otherwise stash
  // this frame.
  uint16_t last_picture_id_gop = seq_num_it->second.first;
  uint16_t last_picture_id_with_padding_gop = seq_num_it->second.second;
  if (frame->frame_type() == VideoFrameType::kVideoFrameDelta) {
    uint16_t prev_seq_num = frame->first_seq_num() - 1;

    if (prev_seq_num != last_picture_id_with_padding_gop)
      return kStash;
  }

  RTC_DCHECK(AheadOrAt(frame->last_seq_num(), seq_num_it->first));

  // Since keyframes can cause reordering we can't simply assign the
  // picture id according to some incrementing counter.
  frame->id.picture_id = frame->last_seq_num();
  frame->num_references =
      frame->frame_type() == VideoFrameType::kVideoFrameDelta;
  frame->references[0] = rtp_seq_num_unwrapper_.Unwrap(last_picture_id_gop);
  if (AheadOf<uint16_t>(frame->id.picture_id, last_picture_id_gop)) {
    seq_num_it->second.first = frame->id.picture_id;
    seq_num_it->second.second = frame->id.picture_id;
  }

  last_picture_id_ = frame->id.picture_id;
  UpdateLastPictureIdWithPadding(frame->id.picture_id);
  frame->id.picture_id = rtp_seq_num_unwrapper_.Unwrap(frame->id.picture_id);
  return kHandOff;
}

void RtpFrameReferenceFinder::UnwrapPictureIds(RtpFrameObject* frame) {
  for (size_t i = 0; i < frame->num_references; ++i)
    frame->references[i] = unwrapper_.Unwrap(frame->references[i]);
  frame->id.picture_id = unwrapper_.Unwrap(frame->id.picture_id);
}

RtpFrameReferenceFinder::FrameDecision RtpFrameReferenceFinder::ManageFrameH264(
    RtpFrameObject* frame) {
  const FrameMarking& rtp_frame_marking = frame->GetFrameMarking();

  uint8_t tid = rtp_frame_marking.temporal_id;
  bool blSync = rtp_frame_marking.base_layer_sync;

  if (tid == kNoTemporalIdx)
    return ManageFramePidOrSeqNum(std::move(frame), kNoPictureId);

  frame->id.picture_id = frame->last_seq_num();

  if (frame->frame_type() == VideoFrameType::kVideoFrameKey) {
    // For H264, use last_seq_num_gop_ to simply store last picture id
    // as a pair of unpadded and padded sequence numbers.
    if (last_seq_num_gop_.empty()) {
      last_seq_num_gop_.insert(std::make_pair(
          0, std::make_pair(frame->id.picture_id, frame->id.picture_id)));
    }
  }

  // Stash if we have no keyframe yet.
  if (last_seq_num_gop_.empty())
    return kStash;

  // Check for gap in sequence numbers. Store in |not_yet_received_seq_num_|.
  if (frame->frame_type() == VideoFrameType::kVideoFrameDelta) {
    uint16_t last_pic_id_padded = last_seq_num_gop_.begin()->second.second;
    if (AheadOf<uint16_t>(frame->id.picture_id, last_pic_id_padded)) {
      do {
        last_pic_id_padded = last_pic_id_padded + 1;
        not_yet_received_seq_num_.insert(last_pic_id_padded);
      } while (last_pic_id_padded != frame->id.picture_id);
    }
  }

  int64_t unwrapped_tl0 = tl0_unwrapper_.Unwrap(rtp_frame_marking.tl0_pic_idx);

  // Clean up info for base layers that are too old.
  int64_t old_tl0_pic_idx = unwrapped_tl0 - kMaxLayerInfo;
  auto clean_layer_info_to = layer_info_.lower_bound(old_tl0_pic_idx);
  layer_info_.erase(layer_info_.begin(), clean_layer_info_to);

  // Clean up info about not yet received frames that are too old.
  uint16_t old_picture_id = frame->id.picture_id - kMaxNotYetReceivedFrames * 2;
  auto clean_frames_to = not_yet_received_seq_num_.lower_bound(old_picture_id);
  not_yet_received_seq_num_.erase(not_yet_received_seq_num_.begin(),
                                  clean_frames_to);

  if (frame->frame_type() == VideoFrameType::kVideoFrameKey) {
    frame->num_references = 0;
    layer_info_[unwrapped_tl0].fill(-1);
    UpdateDataH264(frame, unwrapped_tl0, tid);
    return kHandOff;
  }

  auto layer_info_it =
      layer_info_.find(tid == 0 ? unwrapped_tl0 - 1 : unwrapped_tl0);

  // Stash if we have no base layer frame yet.
  if (layer_info_it == layer_info_.end())
    return kStash;

  // Base layer frame. Copy layer info from previous base layer frame.
  if (tid == 0) {
    layer_info_it =
        layer_info_.insert(std::make_pair(unwrapped_tl0, layer_info_it->second))
            .first;
    frame->num_references = 1;
    frame->references[0] = layer_info_it->second[0];
    UpdateDataH264(frame, unwrapped_tl0, tid);
    return kHandOff;
  }

  // This frame only references its base layer frame.
  if (blSync) {
    frame->num_references = 1;
    frame->references[0] = layer_info_it->second[0];
    UpdateDataH264(frame, unwrapped_tl0, tid);
    return kHandOff;
  }

  // Find all references for general frame.
  frame->num_references = 0;
  for (uint8_t layer = 0; layer <= tid; ++layer) {
    // Stash if we have not yet received frames on this temporal layer.
    if (layer_info_it->second[layer] == -1)
      return kStash;

    // Drop if the last frame on this layer is ahead of this frame. A layer sync
    // frame was received after this frame for the same base layer frame.
    uint16_t last_frame_in_layer = layer_info_it->second[layer];
    if (AheadOf<uint16_t>(last_frame_in_layer, frame->id.picture_id))
      return kDrop;

    // Stash and wait for missing frame between this frame and the reference
    auto not_received_seq_num_it =
        not_yet_received_seq_num_.upper_bound(last_frame_in_layer);
    if (not_received_seq_num_it != not_yet_received_seq_num_.end() &&
        AheadOf<uint16_t>(frame->id.picture_id, *not_received_seq_num_it)) {
      return kStash;
    }

    if (!(AheadOf<uint16_t>(frame->id.picture_id, last_frame_in_layer))) {
      RTC_LOG(LS_WARNING) << "Frame with picture id " << frame->id.picture_id
                          << " and packet range [" << frame->first_seq_num()
                          << ", " << frame->last_seq_num()
                          << "] already received, "
                          << " dropping frame.";
      return kDrop;
    }

    ++frame->num_references;
    frame->references[layer] = last_frame_in_layer;
  }

  UpdateDataH264(frame, unwrapped_tl0, tid);
  return kHandOff;
}

void RtpFrameReferenceFinder::UpdateLastPictureIdWithPaddingH264() {
  auto seq_num_it = last_seq_num_gop_.begin();

  // Check if next sequence number is in a stashed padding packet.
  uint16_t next_padded_seq_num = seq_num_it->second.second + 1;
  auto padding_seq_num_it = stashed_padding_.lower_bound(next_padded_seq_num);

  // Check for more consecutive padding packets to increment
  // the "last-picture-id-with-padding" and remove the stashed packets.
  while (padding_seq_num_it != stashed_padding_.end() &&
         *padding_seq_num_it == next_padded_seq_num) {
    seq_num_it->second.second = next_padded_seq_num;
    ++next_padded_seq_num;
    padding_seq_num_it = stashed_padding_.erase(padding_seq_num_it);
  }
}

void RtpFrameReferenceFinder::UpdateLayerInfoH264(RtpFrameObject* frame,
                                                  int64_t unwrapped_tl0,
                                                  uint8_t temporal_idx) {
  auto layer_info_it = layer_info_.find(unwrapped_tl0);

  // Update this layer info and newer.
  while (layer_info_it != layer_info_.end()) {
    if (layer_info_it->second[temporal_idx] != -1 &&
        AheadOf<uint16_t>(layer_info_it->second[temporal_idx],
                          frame->id.picture_id)) {
      // Not a newer frame. No subsequent layer info needs update.
      break;
    }

    layer_info_it->second[temporal_idx] = frame->id.picture_id;
    ++unwrapped_tl0;
    layer_info_it = layer_info_.find(unwrapped_tl0);
  }

  for (size_t i = 0; i < frame->num_references; ++i)
    frame->references[i] = rtp_seq_num_unwrapper_.Unwrap(frame->references[i]);
  frame->id.picture_id = rtp_seq_num_unwrapper_.Unwrap(frame->id.picture_id);
}

void RtpFrameReferenceFinder::UpdateDataH264(RtpFrameObject* frame,
                                             int64_t unwrapped_tl0,
                                             uint8_t temporal_idx) {
  // Update last_seq_num_gop_ entry for last picture id.
  auto seq_num_it = last_seq_num_gop_.begin();
  uint16_t last_pic_id = seq_num_it->second.first;
  if (AheadOf<uint16_t>(frame->id.picture_id, last_pic_id)) {
    seq_num_it->second.first = frame->id.picture_id;
    seq_num_it->second.second = frame->id.picture_id;
  }
  UpdateLastPictureIdWithPaddingH264();

  UpdateLayerInfoH264(frame, unwrapped_tl0, temporal_idx);

  // Remove any current packets from |not_yet_received_seq_num_|.
  uint16_t last_seq_num_padded = seq_num_it->second.second;
  for (uint16_t n = frame->first_seq_num(); AheadOrAt(last_seq_num_padded, n);
       ++n) {
    not_yet_received_seq_num_.erase(n);
  }
}

}  // namespace video_coding
}  // namespace webrtc
