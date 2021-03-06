/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "cc/bbr/bbr_factory.h"

#include <memory>

#include "cc/bbr/bbr_network_controller.h"

namespace webrtc {

BbrNetworkControllerFactory::BbrNetworkControllerFactory() {}

std::unique_ptr<NetworkControllerInterface> BbrNetworkControllerFactory::Create(
    NetworkControllerConfig config) {
  return std::make_unique<bbr::BbrNetworkController>(config);
}

TimeDelta BbrNetworkControllerFactory::GetProcessInterval() const {
  return TimeDelta::PlusInfinity();
}

}  // namespace webrtc
