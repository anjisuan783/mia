// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_RTP_PACKETBUFFERSERVICE_H_
#define ERIZO_SRC_ERIZO_RTP_PACKETBUFFERSERVICE_H_

#include "erizo//logger.h"
#include "erizo//MediaDefinitions.h"
#include "erizo/rtp/RtpHeaders.h"
#include "erizo/pipeline/Service.h"

static constexpr uint16_t kServicePacketBufferSize = 256;

namespace erizo {
class PacketBufferService: public Service {
 public:
  DECLARE_LOGGER();

  PacketBufferService();
  ~PacketBufferService() {}

  PacketBufferService(const PacketBufferService&& service);

  void insertPacket(std::shared_ptr<DataPacket> packet);

  std::shared_ptr<DataPacket> getVideoPacket(uint16_t seq_num);
  std::shared_ptr<DataPacket> getAudioPacket(uint16_t seq_num);

 private:
  uint16_t getIndexInBuffer(uint16_t seq_num);

 private:
  std::vector<std::shared_ptr<DataPacket>> audio_;
  std::vector<std::shared_ptr<DataPacket>> video_;
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_RTP_PACKETBUFFERSERVICE_H_
