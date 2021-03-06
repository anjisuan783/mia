/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_EXPERIMENTS_RATE_CONTROL_SETTINGS_H_
#define RTC_BASE_EXPERIMENTS_RATE_CONTROL_SETTINGS_H_

#include "optional"
#include "api/webrtc_key_value_config.h"
#include "api/video_codec.h"
#include "api/video_encoder_config.h"
#include "rtc_base/struct_parameters_parser.h"

namespace webrtc {

struct CongestionWindowConfig {
  static constexpr char kKey[] = "WebRTC-CongestionWindow";
  std::optional<int> queue_size_ms;
  std::optional<int> min_bitrate_bps;
  std::unique_ptr<StructParametersParser> Parser();
  static CongestionWindowConfig Parse(std::string_view config);
};

struct VideoRateControlConfig {
  static constexpr char kKey[] = "WebRTC-VideoRateControl";
  std::optional<double> pacing_factor;
  bool alr_probing = false;
  std::optional<int> vp8_qp_max;
  std::optional<int> vp8_min_pixels;
  bool trust_vp8 = false;
  bool trust_vp9 = false;
  double video_hysteresis = 1.0;
  // Default to 35% hysteresis for simulcast screenshare.
  double screenshare_hysteresis = 1.35;
  bool probe_max_allocation = true;
  bool bitrate_adjuster = false;
  bool adjuster_use_headroom = false;
  bool vp8_s0_boost = true;
  bool vp8_dynamic_rate = false;
  bool vp9_dynamic_rate = false;

  std::unique_ptr<StructParametersParser> Parser();
};

class RateControlSettings final {
 public:
  ~RateControlSettings();
  RateControlSettings(RateControlSettings&&);

  static RateControlSettings ParseFromFieldTrials();
  static RateControlSettings ParseFromKeyValueConfig(
      const WebRtcKeyValueConfig* const key_value_config);

  // When CongestionWindowPushback is enabled, the pacer is oblivious to
  // the congestion window. The relation between outstanding data and
  // the congestion window affects encoder allocations directly.
  bool UseCongestionWindow() const;
  int64_t GetCongestionWindowAdditionalTimeMs() const;
  bool UseCongestionWindowPushback() const;
  uint32_t CongestionWindowMinPushbackTargetBitrateBps() const;

  std::optional<double> GetPacingFactor() const;
  bool UseAlrProbing() const;

  std::optional<int> LibvpxVp8QpMax() const;
  std::optional<int> LibvpxVp8MinPixels() const;
  bool LibvpxVp8TrustedRateController() const;
  bool Vp8BoostBaseLayerQuality() const;
  bool Vp8DynamicRateSettings() const;
  bool LibvpxVp9TrustedRateController() const;
  bool Vp9DynamicRateSettings() const;

  // TODO(bugs.webrtc.org/10272): Remove one of these when we have merged
  // VideoCodecMode and VideoEncoderConfig::ContentType.
  double GetSimulcastHysteresisFactor(VideoCodecMode mode) const;
  double GetSimulcastHysteresisFactor(
      VideoEncoderConfig::ContentType content_type) const;

  bool TriggerProbeOnMaxAllocatedBitrateChange() const;
  bool UseEncoderBitrateAdjuster() const;
  bool BitrateAdjusterCanUseNetworkHeadroom() const;

 private:
  explicit RateControlSettings(
      const WebRtcKeyValueConfig* const key_value_config);

  const CongestionWindowConfig congestion_window_config_;
  VideoRateControlConfig video_config_;
};

}  // namespace webrtc

#endif  // RTC_BASE_EXPERIMENTS_RATE_CONTROL_SETTINGS_H_
