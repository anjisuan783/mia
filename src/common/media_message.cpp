#include "common/media_message.h"

#include "rtmp/media_rtmp_const.h"

namespace ma
{

MDEFINE_LOGGER(MediaMessage, "MediaMessage");

void MessageHeader::initialize_audio(int size, uint32_t time, int stream) {
  message_type = RTMP_MSG_AudioMessage;
  payload_length = (int32_t)size;
  timestamp_delta = (int32_t)time;
  timestamp = (int64_t)time;
  stream_id = (int32_t)stream;
  
  // audio chunk-id
  perfer_cid = RTMP_CID_Audio;
}

void MessageHeader::initialize_video(int size, uint32_t time, int stream) {
  message_type = RTMP_MSG_VideoMessage;
  payload_length = (int32_t)size;
  timestamp_delta = (int32_t)time;
  timestamp = (int64_t)time;
  stream_id = (int32_t)stream;
  
  // video chunk-id
  perfer_cid = RTMP_CID_Video;
}

inline bool MessageHeader::is_audio() {
  return message_type == RTMP_MSG_AudioMessage;
}

inline bool MessageHeader::is_video() {
  return message_type == RTMP_MSG_VideoMessage ;
}

MediaMessage::MediaMessage(MessageHeader* pheader, MessageChain* data)
  : header_{*pheader},
    timestamp_{header_.timestamp},
    size_{header_.payload_length} {

  if (data) {
    payload_ = data->DuplicateChained();
  }
}

MediaMessage::MediaMessage()
  : timestamp_{header_.timestamp},
    size_{header_.payload_length} {
}

MediaMessage::MediaMessage(const MediaMessage& r)
  : MediaMessage() {
    header_ = r.header_;
    payload_ = r.payload_->DuplicateChained();
}

MediaMessage::~MediaMessage() {
  if (payload_) {
    payload_->DestroyChained();
    payload_ = nullptr;
  }
}

void MediaMessage::create(MessageHeader* pheader, MessageChain* data) {
  header_ = *pheader;
  if (payload_) {
    payload_->DestroyChained();
  }

  payload_ = data->DuplicateChained();
}


std::shared_ptr<MediaMessage> MediaMessage::create(
    MessageHeader* pheader, const char* payload) {
  
  auto media_msg = std::make_shared<MediaMessage>(pheader, nullptr);
  
  auto audio_block = DataBlock::Create(pheader->payload_length, payload);
  
  media_msg->payload_ = new MessageChain(audio_block, MessageChain::DUPLICATED);

  return std::move(media_msg);
}

std::shared_ptr<MediaMessage> MediaMessage::create(
    MessageHeader* pheader, std::shared_ptr<DataBlock> payload) {

  MA_ASSERT(pheader->payload_length == payload->GetLength()); 
  auto media_msg = std::make_shared<MediaMessage>(pheader, nullptr);
  
  media_msg->payload_ = new MessageChain(payload, MessageChain::DUPLICATED);

  return std::move(media_msg);
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

