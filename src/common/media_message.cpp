#include "common/media_message.h"

#include "rtmp/media_rtmp_const.h"
#include "utils/media_protocol_utility.h"

namespace ma {
void MessageHeader::initialize_audio(int size, int64_t time, int stream) {
  message_type = RTMP_MSG_AudioMessage;
  payload_length = (int32_t)size;
  //timestamp_delta = (int32_t)time;
  timestamp = time;
  stream_id = (int32_t)stream;
  
  // audio chunk-id
  perfer_cid = RTMP_CID_Audio;
}

void MessageHeader::initialize_video(int size, int64_t time, int stream) {
  message_type = RTMP_MSG_VideoMessage;
  payload_length = (int32_t)size;
  //timestamp_delta = (int32_t)time;
  timestamp = time;
  stream_id = (int32_t)stream;
  
  // video chunk-id
  perfer_cid = RTMP_CID_Video;
}

void MessageHeader::initialize_amf0_script(int size, int stream) {
    message_type = RTMP_MSG_AMF0DataMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)0;
    timestamp = (int64_t)0;
    stream_id = (int32_t)stream;
    
    // amf0 script use connection2 chunk-id
    perfer_cid = RTMP_CID_OverConnection2;
}

bool MessageHeader::is_audio() {
  return message_type == RTMP_MSG_AudioMessage;
}

bool MessageHeader::is_video() {
  return message_type == RTMP_MSG_VideoMessage ;
}

bool MessageHeader::is_amf0_command() {
  return message_type == RTMP_MSG_AMF0CommandMessage;
}

bool MessageHeader::is_amf0_data() {
  return message_type == RTMP_MSG_AMF0DataMessage;
}

bool MessageHeader::is_amf3_command() {
  return message_type == RTMP_MSG_AMF3CommandMessage;
}

bool MessageHeader::is_amf3_data() {
  return message_type == RTMP_MSG_AMF3DataMessage;
}

bool MessageHeader::is_window_ackledgement_size() {
  return message_type == RTMP_MSG_WindowAcknowledgementSize;
}

bool MessageHeader::is_ackledgement() {
  return message_type == RTMP_MSG_Acknowledgement;
}

bool MessageHeader::is_set_chunk_size() {
  return message_type == RTMP_MSG_SetChunkSize;
}

bool MessageHeader::is_user_control_message() {
  return message_type == RTMP_MSG_UserControlMessage;
}

bool MessageHeader::is_set_peer_bandwidth() {
  return message_type == RTMP_MSG_SetPeerBandwidth;
}

bool MessageHeader::is_aggregate() {
  return message_type == RTMP_MSG_AggregateMessage;
}

//MediaMessage
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
    size_ = r.size_;
    payload_ = r.payload_->DuplicateChained();
}

MediaMessage::MediaMessage(MediaMessage&& r)
  : MediaMessage() {
  header_ = r.header_;
  size_ = r.size_;
  payload_ = r.payload_;
  r.payload_ = nullptr;
}

MediaMessage::~MediaMessage() {
  if (payload_) {
    payload_->DestroyChained();
    payload_ = nullptr;
  }
}

void MediaMessage::operator=(MediaMessage&& r) {
  header_ = r.header_;
  size_ = r.size_;
  payload_ = r.payload_;
  r.payload_ = nullptr;
}

void MediaMessage::operator=(const MediaMessage& r) {
  header_ = r.header_;
  size_ = r.size_;
  if (r.payload_) {
    payload_ = r.payload_->DuplicateChained();
  } else {
    payload_ = nullptr;
  }
}

void MediaMessage::create(MessageHeader* pheader, MessageChain* data) {
  header_ = *pheader;
  if (payload_) {
    payload_->DestroyChained();
  }
  payload_ = data->DuplicateChained();
  size_ = payload_->GetChainedLength();
}

std::shared_ptr<MediaMessage> MediaMessage::create(
    MessageHeader* pheader, const char* payload) {
  auto data_block = DataBlock::Create(pheader->payload_length, payload);
  return MediaMessage::create(pheader, std::move(data_block));
}

std::shared_ptr<MediaMessage> MediaMessage::create(
    MessageHeader* pheader, std::shared_ptr<DataBlock> payload) {
  assert(pheader->payload_length == payload->GetLength());
  auto msg = std::make_shared<MediaMessage>(pheader, nullptr);
  msg->payload_ = new MessageChain(std::move(payload));
  return std::move(msg);
}

int MediaMessage::ChunkHeader(char* cache, int nb_cache, bool c0) {
  if (c0) {
    return srs_chunk_header_c0(header_.perfer_cid, (uint32_t)timestamp_,
        header_.payload_length, header_.message_type, 
        header_.stream_id, cache, nb_cache);
  }

  return srs_chunk_header_c3(
      header_.perfer_cid, (uint32_t)timestamp_, cache, nb_cache);
}

std::shared_ptr<MediaMessage> MediaMessage::Copy() {
  return std::make_shared<MediaMessage>(*this);
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
