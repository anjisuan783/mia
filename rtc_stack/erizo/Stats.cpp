/*
 * Stats.cpp
 *
 */

#include <sstream>
#include <string>

#include "erizo/Stats.h"
#include "erizo/MediaStream.h"

namespace erizo {

  DEFINE_LOGGER(Stats, "Stats");
  Stats::Stats() : listener_{nullptr} {
  }

  Stats::~Stats() {
  }

  StatNode& Stats::getNode() {
    return root_;
  }

  std::string Stats::getStats() {
    return root_.toString();
  }

  void Stats::setStatsListener(MediaStreamStatsListener* listener) {
    listener_ = listener;
  }

  void Stats::sendStats() {
    if (listener_) listener_->notifyStats(getStats());
  }
}  // namespace erizo
