#include "common/media_message.h"

#include "datapackage.h"
#include "rtmp/media_rtmp_const.h"

namespace ma
{

inline bool MessageHeader::is_audio() {
  return message_type == RTMP_MSG_AudioMessage;
}

inline bool MessageHeader::is_video() {
  return message_type == RTMP_MSG_VideoMessage ;
}

MediaMessage::MediaMessage(MessageHeader* pheader, CDataPackage* data)
  : header_{*pheader},
    timestamp_{header_.timestamp},
    size_{header_.payload_length},
    payload_{data->DuplicatePackage()} {
}

MediaMessage::MediaMessage()
  : timestamp_{header_.timestamp},
    size_{header_.payload_length} {
}

MediaMessage::MediaMessage(const MediaMessage& r)
  : MediaMessage() {
    header_ = r.header_;
    payload_ = r.payload_->DuplicatePackage();
}

MediaMessage::~MediaMessage() {
  if (payload_) {
    payload_->DestroyPackage();
    payload_ = nullptr;
  }
}

void MediaMessage::create(MessageHeader* pheader, CDataPackage* data) {
  header_ = *pheader;
  if (payload_) {
    payload_->DestroyPackage();
  }

  payload_ = data->DuplicatePackage();
}

bool MediaMessage::is_av() {
  return header_.is_audio() || header_.is_video();
}

bool MediaMessage::is_video() {
  return header_.is_video();
}

bool MediaMessage::is_audio() {
  return header_.is_audio();
}

}

