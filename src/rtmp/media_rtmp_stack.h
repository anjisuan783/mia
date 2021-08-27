#ifndef __MEDIA_RTMP_STACK_H__
#define __MEDIA_RTMP_STACK_H__

#include <memory>
#include "common/srs_kernel_error.h"

namespace ma {

class SrsAmf0Object;
class SrsBuffer;
class MediaMessage;
class DataBlock;

// The decoded message payload.
// @remark we seperate the packet from message,
//        for the packet focus on logic and domain data,
//        the message bind to the protocol and focus on protocol, such as header.
//         we can merge the message and packet, using OOAD hierachy, packet extends from message,
//         it's better for me to use components -- the message use the packet as payload.
class SrsPacket
{
public:
    SrsPacket();
    virtual ~SrsPacket();
public:
    // Covert packet to common message.
    //virtual srs_error_t to_msg(std::shared_ptr<MediaMessage> msg, int stream_id);
public:
    // The subpacket can override this encode,
    // For example, video and audio will directly set the payload withou memory copy,
    // other packet which need to serialize/encode to bytes by override the
    // get_size and encode_packet.
    virtual srs_error_t encode(std::shared_ptr<DataBlock> payload);
// Decode functions for concrete packet to override.
public:
    // The subpacket must override to decode packet from stream.
    // @remark never invoke the super.decode, it always failed.
    virtual srs_error_t decode(SrsBuffer* stream);
// Encode functions for concrete packet to override.
public:
    // The cid(chunk id) specifies the chunk to send data over.
    // Generally, each message perfer some cid, for example,
    // all protocol control messages perfer RTMP_CID_ProtocolControl,
    // SrsSetWindowAckSizePacket is protocol control message.
    virtual int get_prefer_cid();
    // The subpacket must override to provide the right message type.
    // The message type set the RTMP message type in header.
    virtual int get_message_type();
protected:
    // The subpacket can override to calc the packet size.
    virtual int get_size();
    // The subpacket can override to encode the payload to stream.
    // @remark never invoke the super.encode_packet, it always failed.
    virtual srs_error_t encode_packet(SrsBuffer* stream);
};


// The stream metadata.
// FMLE: @setDataFrame
// others: onMetaData
class SrsOnMetaDataPacket final : public SrsPacket
{
public:
  // Name of metadata. Set to "onMetaData"
  std::string name;
  // Metadata of stream.
  // @remark, never be NULL, an AMF0 object instance.
  SrsAmf0Object* metadata;
  
  SrsOnMetaDataPacket();
  virtual ~SrsOnMetaDataPacket();
// Decode functions for concrete packet to override.
public:
  srs_error_t decode(SrsBuffer* stream) override;
  // Encode functions for concrete packet to override.

  int get_size() override;
  int get_prefer_cid() override;
  int get_message_type() override;
protected:  
  srs_error_t encode_packet(SrsBuffer* stream) override;
};

} //namespace ma

#endif //!__MEDIA_RTMP_STACK_H__

