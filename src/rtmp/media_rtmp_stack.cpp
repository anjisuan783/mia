#include "media_rtmp_stack.h"

#include "utils/media_msg_chain.h"
#include "common/media_message.h"
#include "rtmp/media_amf0.h"
#include "utils/media_kernel_buffer.h"
#include "rtmp/media_rtmp_const.h"

namespace ma {

SrsPacket::SrsPacket() = default;

SrsPacket::~SrsPacket() = default;

/*
srs_error_t SrsPacket::to_msg(std::shared_ptr<MediaMessage> msg, int stream_id)
{
    srs_error_t err = srs_success;

    int size = 0;
    char* payload = NULL;
    if ((err = encode(size, payload)) != srs_success) {
        return srs_error_wrap(err, "encode packet");
    }

    // encode packet to payload and size.
    if (size <= 0 || payload == NULL) {
        MLOG_WARN("packet is empty, ignore empty message.");
        return err;
    }

    // to message
    MessageHeader header;
    header.payload_length = size;
    header.message_type = get_message_type();
    header.stream_id = stream_id;
    header.perfer_cid = get_prefer_cid();

    MessageChain _payload(size, (const char*)payload, MessageChain::DONT_DELETE, size);
    msg->create(&header, &_payload);
    return err;
}
*/

srs_error_t SrsPacket::encode(std::shared_ptr<DataBlock> payload)
{
    srs_error_t err = srs_success;
    
    int size = get_size();

    if (size > 0) {
        SrsBuffer stream(payload->GetBasePtr(), payload->GetCapacity());

        if ((err = encode_packet(&stream)) != srs_success) {
            return srs_error_wrap(err, "encode packet");
        }
    }
    
    return err;
}

srs_error_t SrsPacket::decode(SrsBuffer* stream)
{
    return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "decode");
}

int SrsPacket::get_prefer_cid()
{
    return 0;
}

int SrsPacket::get_message_type()
{
    return 0;
}

int SrsPacket::get_size()
{
    return 0;
}

srs_error_t SrsPacket::encode_packet(SrsBuffer* stream)
{
    return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "encode");
}


SrsOnMetaDataPacket::SrsOnMetaDataPacket()
{
  name = SRS_CONSTS_RTMP_ON_METADATA;
  metadata = SrsAmf0Any::object();
}

SrsOnMetaDataPacket::~SrsOnMetaDataPacket()
{
  srs_freep(metadata);
}

srs_error_t SrsOnMetaDataPacket::decode(SrsBuffer* stream)
{
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, name)) != srs_success) {
      return srs_error_wrap(err, "name");
  }
  
  // ignore the @setDataFrame
  if (name == SRS_CONSTS_RTMP_SET_DATAFRAME) {
    if ((err = srs_amf0_read_string(stream, name)) != srs_success) {
      return srs_error_wrap(err, "name");
    }
  }

  // Allows empty body metadata.
  if (stream->empty()) {
    return err;
  }
  
  // the metadata maybe object or ecma array
  SrsAmf0Any* any = NULL;
  if ((err = srs_amf0_read_any(stream, &any)) != srs_success) {
    return srs_error_wrap(err, "metadata");
  }
  
  srs_assert(any);
  if (any->is_object()) {
    srs_freep(metadata);
    metadata = any->to_object();
    return err;
  }

  std::unique_ptr<SrsAmf0Any> obj(any);
  
  if (any->is_ecma_array()) {
    SrsAmf0EcmaArray* arr = any->to_ecma_array();
    
    // if ecma array, copy to object.
    for (int i = 0; i < arr->count(); i++) {
      metadata->set(arr->key_at(i), arr->value_at(i)->copy());
    }
  }
  
  return err;
}

int SrsOnMetaDataPacket::get_prefer_cid()
{
  return RTMP_CID_OverConnection2;
}

int SrsOnMetaDataPacket::get_message_type()
{
  return RTMP_MSG_AMF0DataMessage;
}

int SrsOnMetaDataPacket::get_size()
{
  return SrsAmf0Size::str(name) + SrsAmf0Size::object(metadata);
}

srs_error_t SrsOnMetaDataPacket::encode_packet(SrsBuffer* stream)
{
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, name)) != srs_success) {
    return srs_error_wrap(err, "name");
  }
  
  if ((err = metadata->write(stream)) != srs_success) {
    return srs_error_wrap(err, "metadata");
  }
  
  return err;
}


} //namespace ma

