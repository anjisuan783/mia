// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_RTP_LAYERDETECTORHANDLER_H_
#define ERIZO_SRC_ERIZO_RTP_LAYERDETECTORHANDLER_H_

#include <memory>
#include <string>
#include <random>
#include <map>

#include "erizo/logger.h"
#include "erizo/pipeline/Handler.h"
#include "erizo/Stats.h"

#define MAX_DELAY 450000

namespace erizo {

class LayerInfoChangedEvent : public MediaEvent {
 public:
  LayerInfoChangedEvent(std::vector<uint32_t> video_frame_width_list_, std::vector<uint32_t> video_frame_height_list_,
                        std::vector<uint64_t> video_frame_rate_list_)
    : video_frame_width_list{video_frame_width_list_},
      video_frame_height_list{video_frame_height_list_},
      video_frame_rate_list{video_frame_rate_list_} {}

  std::string getType() const override {
    return "LayerInfoChangedEvent";
  }
  std::vector<uint32_t> video_frame_width_list;
  std::vector<uint32_t> video_frame_height_list;
  std::vector<uint64_t> video_frame_rate_list;
};

}  // namespace erizo

#endif  // ERIZO_SRC_ERIZO_RTP_LAYERDETECTORHANDLER_H_
