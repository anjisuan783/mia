//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.


#ifndef __MEDIA_RTMP_FORMAT_H__
#define __MEDIA_RTMP_FORMAT_H__

#include <memory>
#include "common/media_kernel_error.h"
#include "rtmp/media_rtmp_stack.h"
#include "encoder/media_codec.h"

namespace ma {

class SrsOnMetaDataPacket;
class MediaMessage;

/**
 * Create special structure from RTMP stream, for example, the metadata.
 */
class SrsRtmpFormat : public SrsFormat {
public:
  SrsRtmpFormat();
  virtual ~SrsRtmpFormat();
public:
  // Initialize the format from metadata, optional.
  virtual srs_error_t on_metadata(SrsOnMetaDataPacket* meta);
  // When got a parsed audio packet.
  virtual srs_error_t on_audio(std::shared_ptr<MediaMessage> shared_audio);
  virtual srs_error_t on_audio(int64_t timestamp, char* data, int size);
  // When got a parsed video packet.
  virtual srs_error_t on_video(std::shared_ptr<MediaMessage> shared_video);
  virtual srs_error_t on_video(int64_t timestamp, char* data, int size);
};

}

#endif //!__MEDIA_RTMP_FORMAT_H__

