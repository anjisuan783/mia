/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_FRAME_BUFFER2_H_
#define MODULES_VIDEO_CODING_FRAME_BUFFER2_H_

#include <array>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "api/encoded_frame.h"
#include "video/video_coding_defines.h"
#include "video/inter_frame_delay.h"
#include "video/jitter_estimator.h"
#include "video/decoded_frames_history.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/rtt_mult_experiment.h"
#include "rtc_base/sequence_number_util.h"
#include "rtc_base/task_queue.h"
#include "rtc_base/repeating_task.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class Clock;
class VCMReceiveStatisticsCallback;
class VCMJitterEstimator;
class VCMTiming;

namespace video_coding {

class FrameBuffer final {
 public:
  enum ReturnReason { kFrameFound, kTimeout, kStopped };

  FrameBuffer(Clock* clock,
              VCMTiming* timing,
              VCMReceiveStatisticsCallback* stats_callback,
              rtc::TaskQueue*);

  ~FrameBuffer();

  // Insert a frame into the frame buffer. Returns the picture id
  // of the last continuous frame or -1 if there is no continuous frame.
  // TODO(philipel): Return a VideoLayerFrameId and not only the picture id.
  int64_t InsertFrame(std::unique_ptr<EncodedFrame> frame);

  void NextFrame(
      int64_t max_wait_time_ms,
      bool keyframe_required,
      std::function<void(std::unique_ptr<EncodedFrame>, ReturnReason)> handler);

  // Tells the FrameBuffer which protection mode that is in use. Affects
  // the frame timing.
  // TODO(philipel): Remove this when new timing calculations has been
  //                 implemented.
  void SetProtectionMode(VCMVideoProtection mode);

  // Start the frame buffer, has no effect if the frame buffer is started.
  // The frame buffer is started upon construction.
  void Start();

  // Stop the frame buffer, causing any sleeping thread in NextFrame to
  // return immediately.
  void Stop();

  // Updates the RTT for jitter buffer estimation.
  void UpdateRtt(int64_t rtt_ms);

  // Clears the FrameBuffer, removing all the buffered frames.
  void Clear();

 private:
  struct FrameInfo {
    FrameInfo();
    FrameInfo(FrameInfo&&);
    ~FrameInfo();

    // Which other frames that have direct unfulfilled dependencies
    // on this frame.
    absl::InlinedVector<VideoLayerFrameId, 8> dependent_frames;

    // A frame is continiuous if it has all its referenced/indirectly
    // referenced frames.
    //
    // How many unfulfilled frames this frame have until it becomes continuous.
    size_t num_missing_continuous = 0;

    // A frame is decodable if all its referenced frames have been decoded.
    //
    // How many unfulfilled frames this frame have until it becomes decodable.
    size_t num_missing_decodable = 0;

    // If this frame is continuous or not.
    bool continuous = false;

    // The actual EncodedFrame.
    std::unique_ptr<EncodedFrame> frame;
  };

  using FrameMap = std::map<VideoLayerFrameId, FrameInfo>;

  // Check that the references of |frame| are valid.
  bool ValidReferences(const EncodedFrame& frame) const;

  int64_t FindNextFrame(int64_t now_ms);
  EncodedFrame* GetNextFrame();

  void StartWaitForNextFrameOnQueue();
  void CancelCallback();

  // Update all directly dependent and indirectly dependent frames and mark
  // them as continuous if all their references has been fulfilled.
  void PropagateContinuity(FrameMap::iterator start);

  // Marks the frame as decoded and updates all directly dependent frames.
  void PropagateDecodability(const FrameInfo& info);

  // Update the corresponding FrameInfo of |frame| and all FrameInfos that
  // |frame| references.
  // Return false if |frame| will never be decodable, true otherwise.
  bool UpdateFrameInfoWithIncomingFrame(const EncodedFrame& frame,
                                        FrameMap::iterator info);

  void UpdateJitterDelay();

  void UpdateTimingFrameInfo();

  void ClearFramesAndHistory();

  // Checks if the superframe, which current frame belongs to, is complete.
  bool IsCompleteSuperFrame(const EncodedFrame& frame);

  bool HasBadRenderTiming(const EncodedFrame& frame, int64_t now_ms);

  // The cleaner solution would be to have the NextFrame function return a
  // vector of frames, but until the decoding pipeline can support decoding
  // multiple frames at the same time we combine all frames to one frame and
  // return it. See bugs.webrtc.org/10064
  EncodedFrame* CombineAndDeleteFrames(
      const std::vector<EncodedFrame*>& frames) const;

  // Stores only undecoded frames.
  FrameMap frames_;
  DecodedFramesHistory decoded_frames_history_;
  Clock* const clock_;

  rtc::TaskQueue* callback_queue_;
  RepeatingTaskHandle callback_task_;
  std::function<void(std::unique_ptr<EncodedFrame>, ReturnReason)>
      frame_handler_;
  int64_t latest_return_time_ms_;
  bool keyframe_required_;

  VCMJitterEstimator jitter_estimator_;
  VCMTiming* const timing_;
  VCMInterFrameDelay inter_frame_delay_;
  std::optional<VideoLayerFrameId> last_continuous_frame_;
  std::vector<FrameMap::iterator> frames_to_decode_;
  bool stopped_;
  VCMVideoProtection protection_mode_;
  VCMReceiveStatisticsCallback* const stats_callback_;
  int64_t last_log_non_decoded_ms_;

  const bool add_rtt_to_playout_delay_;

  // rtt_mult experiment settings.
  const std::optional<RttMultExperiment::Settings> rtt_mult_settings_;

  int64_t  last_log_frames_ms_ = -1;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(FrameBuffer);
};

}  // namespace video_coding
}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_FRAME_BUFFER2_H_
