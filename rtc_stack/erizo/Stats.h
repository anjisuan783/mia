// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.

#ifndef ERIZO_SRC_ERIZO_STATS_H_
#define ERIZO_SRC_ERIZO_STATS_H_

#include <string>
#include <map>

#include "erizo//logger.h"
#include "erizo/pipeline/Service.h"
#include "erizo/rtp/RtpHeaders.h"
#include "utils/Clock.h"

#include "erizo/stats/StatNode.h"

namespace erizo {

class MediaStreamStatsListener;

class Stats : public Service {
  DECLARE_LOGGER();

 public:
  Stats();
  virtual ~Stats();

  StatNode& getNode();

  std::string getStats();

  void setStatsListener(MediaStreamStatsListener* listener);
  void sendStats();

 private:
  MediaStreamStatsListener* listener_;
  StatNode root_;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_STATS_H_
