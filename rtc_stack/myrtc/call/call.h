/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef CALL_CALL_H_
#define CALL_CALL_H_

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "api/media_types.h"
#include "call/call_config.h"
#include "call/flexfec_receive_stream.h"
#include "call/packet_receiver.h"
#include "call/audio_receive_stream.h"
#include "call/video_receive_stream.h"
#include "utility/process_thread.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/sent_packet.h"
#include "rtc_base/network_route.h"

namespace webrtc {

// A Call instance can contain several send and/or receive streams. All streams
// are assumed to have the same remote endpoint and will share bitrate estimates
// etc.
class Call {
 public:
  using Config = CallConfig;

  struct Stats {
    std::string ToString(int64_t time_ms) const;

    int send_bandwidth_bps = 0;       // Estimated available send bandwidth.
    int max_padding_bitrate_bps = 0;  // Cumulative configured max padding.
    int recv_bandwidth_bps = 0;       // Estimated available receive bandwidth.
    int64_t pacer_delay_ms = 0;
    int64_t rtt_ms = -1;
  };

  static Call* Create(const Call::Config& config,
                      Clock* clock,
                      std::unique_ptr<ProcessThread> call_thread);

  // All received RTP and RTCP packets for the call should be inserted to this
  // PacketReceiver. The PacketReceiver pointer is valid as long as the
  // Call instance exists.
  virtual PacketReceiver* Receiver() = 0;

  virtual AudioReceiveStream* CreateAudioReceiveStream(
      const AudioReceiveStream::Config& config) = 0;
  virtual void DestroyAudioReceiveStream(
      AudioReceiveStream* receive_stream) = 0;

  virtual VideoReceiveStream* CreateVideoReceiveStream(
      VideoReceiveStream::Config configuration) = 0;
  virtual void DestroyVideoReceiveStream(
      VideoReceiveStream* receive_stream) = 0;

  // In order for a created VideoReceiveStream to be aware that it is
  // protected by a FlexfecReceiveStream, the latter should be created before
  // the former.
  virtual FlexfecReceiveStream* CreateFlexfecReceiveStream(
      const FlexfecReceiveStream::Config& config) = 0;
  virtual void DestroyFlexfecReceiveStream(
      FlexfecReceiveStream* receive_stream) = 0;

  // TODO(skvlad): When the unbundled case with multiple streams for the same
  // media type going over different networks is supported, track the state
  // for each stream separately. Right now it's global per media type.
  virtual void SignalChannelNetworkState(MediaType media,
                                         NetworkState state) = 0;
  virtual ~Call() {}
};

}  // namespace webrtc

#endif  // CALL_CALL_H_
