//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include "rtmp/media_rtmp_format.h"

#include "common/media_message.h"

namespace ma {

SrsRtmpFormat::SrsRtmpFormat() = default;

SrsRtmpFormat::~SrsRtmpFormat() = default;

srs_error_t SrsRtmpFormat::on_metadata(SrsOnMetaDataPacket*)
{
  // TODO: FIXME: Try to initialize format from metadata.
  return srs_success;
}

srs_error_t SrsRtmpFormat::on_audio(std::shared_ptr<MediaMessage> shared_audio)
{
  //TODO need optimizing
  std::string msg = shared_audio->payload_->FlattenChained();
  char* data = const_cast<char*>(msg.c_str());
  int size = msg.length();
  
  return SrsFormat::on_audio(shared_audio->timestamp_, data, size);
}

srs_error_t SrsRtmpFormat::on_audio(int64_t timestamp, char* data, int size)
{
  return SrsFormat::on_audio(timestamp, data, size);
}

srs_error_t SrsRtmpFormat::on_video(std::shared_ptr<MediaMessage> shared_video)
{
  //TODO need optimizing
  std::string msg = shared_video->payload_->FlattenChained();
  char* data = const_cast<char*>(msg.c_str());
  int size = msg.length();
  
  return SrsFormat::on_video(shared_video->timestamp_, data, size);
}

srs_error_t SrsRtmpFormat::on_video(int64_t timestamp, char* data, int size)
{
  return SrsFormat::on_video(timestamp, data, size);
}

}
