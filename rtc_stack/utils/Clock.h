// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef __WA_SRC_CLOCK_H___
#define __WA_SRC_CLOCK_H___

#include <chrono>  // NOLINT

namespace wa {

using clock = std::chrono::steady_clock;
using time_point = std::chrono::steady_clock::time_point;
using duration = std::chrono::steady_clock::duration;

class Clock {
 public:
  virtual time_point now() = 0;
};

class SteadyClock : public Clock {
 public:
  time_point now() override {
    return clock::now();
  }
};

class ClockUtils {
 public:
  static inline int64_t durationToMs(duration duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  }

  static inline uint64_t timePointToMs(time_point time_point) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        time_point.time_since_epoch()).count();
  }

  static inline uint64_t timePointToS(time_point time_point) {
    return std::chrono::duration_cast<std::chrono::seconds>(
        time_point.time_since_epoch()).count();
  }
};

}  // namespace wa
#endif  // __WA_SRC_CLOCK_H___