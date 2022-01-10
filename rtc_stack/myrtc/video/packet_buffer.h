/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_PACKET_BUFFER_H_
#define MODULES_VIDEO_CODING_PACKET_BUFFER_H_

#include <memory>
#include <queue>
#include <set>
#include <vector>

#include "api/encoded_image.h"
#include "video/packet.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/sequence_number_util.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class Clock;

namespace video_coding {

class RtpFrameObject;

// A frame is assembled when all of its packets have been received.
class OnAssembledFrameCallback {
 public:
  virtual ~OnAssembledFrameCallback() {}
  virtual void OnAssembledFrame(std::unique_ptr<RtpFrameObject> frame) = 0;
};

class PacketBuffer final {
 public:
  // Both |start_buffer_size| and |max_buffer_size| must be a power of 2.
  PacketBuffer(Clock* clock,
               size_t start_buffer_size,
               size_t max_buffer_size,
               OnAssembledFrameCallback* frame_callback);
  ~PacketBuffer();

  // Returns true unless the packet buffer is cleared, which means that a key
  // frame request should be sent. The PacketBuffer will always take ownership
  // of the |packet.dataPtr| when this function is called. 
  bool InsertPacket(VCMPacket* packet);
  void ClearTo(uint16_t seq_num);
  void Clear();
  void PaddingReceived(uint16_t seq_num);

  // Timestamp (not RTP timestamp) of the last received packet/keyframe packet.
  std::optional<int64_t> LastReceivedPacketMs() const;
  std::optional<int64_t> LastReceivedKeyframePacketMs() const;

  // Returns number of different frames seen in the packet buffer
  int GetUniqueFramesSeen() const;

 private:
  friend RtpFrameObject;
  // Since we want the packet buffer to be as packet type agnostic
  // as possible we extract only the information needed in order
  // to determine whether a sequence of packets is continuous or not.
  struct ContinuityInfo {
    // The sequence number of the packet.
    uint16_t seq_num = 0;

    // If this is the first packet of the frame.
    bool frame_begin = false;

    // If this is the last packet of the frame.
    bool frame_end = false;

    // If this slot is currently used.
    bool used = false;

    // If all its previous packets have been inserted into the packet buffer.
    bool continuous = false;

    // If this packet has been used to create a frame already.
    bool frame_created = false;
  };

  Clock* const clock_;

  // Tries to expand the buffer.
  bool ExpandBufferSize();

  // Test if all previous packets has arrived for the given sequence number.
  bool PotentialNewFrame(uint16_t seq_num) const;

  // Test if all packets of a frame has arrived, and if so, creates a frame.
  // Returns a vector of received frames.
  std::vector<std::unique_ptr<RtpFrameObject>> FindFrames(uint16_t seq_num);

  rtc::scoped_refptr<EncodedImageBuffer> GetEncodedImageBuffer(
      size_t frame_size,
      uint16_t first_seq_num,
      uint16_t last_seq_num);

  // Get the packet with sequence number |seq_num|.
  VCMPacket* GetPacket(uint16_t seq_num);

  // Clears the packet buffer from |start_seq_num| to |stop_seq_num| where the
  // endpoints are inclusive.
  void ClearInterval(uint16_t start_seq_num, uint16_t stop_seq_num);

  void UpdateMissingPackets(uint16_t seq_num);

  // Counts unique received timestamps and updates |unique_frames_seen_|.
  void OnTimestampReceived(uint32_t rtp_timestamp);

  // Buffer size_ and max_size_ must always be a power of two.
  size_t size_;
  const size_t max_size_;

  // The fist sequence number currently in the buffer.
  uint16_t first_seq_num_;

  // If the packet buffer has received its first packet.
  bool first_packet_received_;

  // If the buffer is cleared to |first_seq_num_|.
  bool is_cleared_to_first_seq_num_;

  // Buffer that holds the inserted packets.
  std::vector<VCMPacket> data_buffer_;

  // Buffer that holds the information about which slot that is currently in use
  // and information needed to determine the continuity between packets.
  std::vector<ContinuityInfo> sequence_buffer_;

  // Called when all packets in a frame are received, allowing the frame
  // to be assembled.
  OnAssembledFrameCallback* const assembled_frame_callback_;

  // Timestamp (not RTP timestamp) of the last received packet/keyframe packet.
  std::optional<int64_t> last_received_packet_ms_;
  std::optional<int64_t> last_received_keyframe_packet_ms_;

  int unique_frames_seen_;

  std::optional<uint16_t> newest_inserted_seq_num_;
  std::set<uint16_t, DescendingSeqNumComp<uint16_t>> missing_packets_;

  // Indicates if we should require SPS, PPS, and IDR for a particular
  // RTP timestamp to treat the corresponding frame as a keyframe.
  const bool sps_pps_idr_is_h264_keyframe_;

  // Stores several last seen unique timestamps for quick search.
  std::set<uint32_t> rtp_timestamps_history_set_;
  // Stores the same unique timestamps in the order of insertion.
  std::queue<uint32_t> rtp_timestamps_history_queue_;
};

}  // namespace video_coding
}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_PACKET_BUFFER_H_
