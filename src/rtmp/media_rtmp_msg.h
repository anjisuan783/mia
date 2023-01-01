/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __MEDIA_RTMP_MESSAGE_H__
#define __MEDIA_RTMP_MESSAGE_H__

#include <memory>
#include <optional>

#include "common/media_consts.h"
#include "common/media_kernel_error.h"
#include "common/media_message.h"
#include "utils/sigslot.h"
#include "rtmp/media_io_buffer.h"
#include "rtmp/media_amf0.h"

namespace ma {

class RtmpBuffer;
class MediaMessage;
class DataBlock;
class MessageChain;
class RtmpChunkStream;
class RtmpPacket;


class RtmpProtocalSink {
 public:
  virtual ~RtmpProtocalSink() = default;

  // packet notifier
  virtual srs_error_t OnPacket(std::shared_ptr<MediaMessage>) = 0;
};

// The protocol provides the rtmp-message-protocol services,
// To recv RTMP message from RTMP chunk stream,
// and to send out RTMP message over RTMP chunk stream.
class RtmpProtocal final : public RtmpBufferIOSink {
 public:
  RtmpProtocal();
  ~RtmpProtocal() override;

  void Open(std::shared_ptr<RtmpBufferIO>, RtmpProtocalSink*);
  void Close();
  srs_error_t Write(RtmpPacket*, int streamid);
  srs_error_t OnRead(MessageChain*) override;
  void OnDisc(srs_error_t) override;

  srs_error_t SetInWinAckSize(int ack_size);
  srs_error_t DecodeMessage(MediaMessage* msg, RtmpPacket*& ppacket);

private:
  // parse chunk message
  srs_error_t ParseMsg(std::shared_ptr<MediaMessage>&);
  srs_error_t SendWithBuffer(std::shared_ptr<MediaMessage>);

  // Read the chunk basic header(fmt, cid) from chunk stream.
  // user can discovery a RtmpChunkStream by cid.
  bool ReadBasicHeader(char& fmt, int& cid);

  // Read the chunk message header(timestamp, payload_length, message_type, stream_id)
  // From chunk stream and save to RtmpChunkStream.
  srs_error_t ReadMessageHeader(RtmpChunkStream* chunk, char fmt);
  
  // Read the chunk payload, remove the used bytes in buffer,
  // if got entire message, set the pmsg.
  std::optional<std::shared_ptr<MediaMessage>> 
      ReadMessagePayload(RtmpChunkStream* chunk);

  // When recv message, update the context.
  srs_error_t InternalProcessMessage(MediaMessage* msg);
 
  // Auto response the ack message.
  srs_error_t ResponseAcknowledgement();
  // When message sentout, update the context.
  srs_error_t OnSendPacket(MessageHeader* mh, RtmpPacket* packet);

  // Auto response the ping message.
  srs_error_t ResponsePingMessage(int32_t timestamp);

  void print_debug_info() { }
  
  // For peer in/out
 private:
  struct AckWindowSize {
    uint32_t window = 0;
    // number of received bytes.
    int64_t nb_recv_bytes = 0;
    // previous responsed sequence number.
    uint32_t sequence_number = 0;
  };

  RtmpProtocalSink* sink_;

  std::shared_ptr<RtmpBufferIO> io_;
  MessageChain* read_buffer_ = nullptr;

  RtmpChunkStream* current_chunk_ = nullptr;

  // The requests sent out, used to build the response.
  // key: transactionId
  // value: the request command name
  std::map<double, std::string> requests_;

  // The chunk stream to decode RTMP messages.
  std::map<int, RtmpChunkStream*> chunk_streams_;
  // Cache some frequently used chunk header.
  // cs_cache, the chunk stream cache.
  RtmpChunkStream** cs_cache_ = nullptr;

  int32_t in_chunk_size_ = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;
  int32_t out_chunk_size_ = SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE;

  // For example, to respose the encoder, for server got lots of packets.
  AckWindowSize in_ack_size_;
  // The output ack window, to require peer to response the ack.
  AckWindowSize out_ack_size_;

  // fixed size, do not change
  char out_c0c3_caches_[SRS_CONSTS_C0C3_HEADERS_MAX];

  // The buffer length set by peer.
  int32_t in_buffer_length_ = 0;
};

// The decoded message payload.
// @remark we seperate the packet from message,
//  for the packet focus on logic and domain data,
//  the message bind to the protocol and focus on protocol, such as header.
//  we can merge the message and packet, using OOAD hierachy, packet extends from message,
// it's better for me to use components -- the message use the packet as payload.
class RtmpPacket {
 public:
  RtmpPacket() = default;
  virtual ~RtmpPacket() = default;

  // Covert packet to media message.
  srs_error_t to_msg(MediaMessage* msg, int stream_id);

  // The subpacket can override this encode,
  // For example, video and audio will directly set the payload withou memory 
  // copy, other packet which need to serialize/encode to bytes by override the
  // get_size and encode_packet.
  srs_error_t encode(std::shared_ptr<DataBlock> payload);

  // The subpacket must override to decode packet from stream.
  // @remark never invoke the super.decode, it always failed.
  virtual srs_error_t decode(SrsBuffer* stream);
  
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

// incoming chunk stream maybe interlaced,
// Use the chunk stream to cache the input RTMP chunk streams.
class RtmpChunkStream final {
 public:
  RtmpChunkStream(int _cid);
  ~RtmpChunkStream() = default;
 public:
  // Represents the basic header fmt,
  // which used to identify the variant message header type.
  char fmt = 0;
  // Represents the basic header cid,
  // which is the chunk stream id.
  int cid = 0;
  // Cached message header
  MessageHeader header;
  // Whether the chunk message header has extended timestamp.
  bool extended_timestamp = false;
  // The partially read message.
  std::shared_ptr<MediaMessage> msg;
  // Decoded msg count, to identify whether the chunk stream is fresh.
  int64_t msg_count = 0;
};

// 4.1.1. connect
// The client sends the connect command to the server to request
// connection to a server application instance.
class RtmpConnectAppPacket : public RtmpPacket {
 public:
  RtmpConnectAppPacket();
  ~RtmpConnectAppPacket() override;

  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the command. Set to "connect".
  std::string command_name;
  // Always set to 1.
  double transaction_id;
  // Command information object which has the name-value pairs.
  // @remark: alloc in packet constructor, user can directly use it,
  //       user should never alloc it again which will cause memory leak.
  // @remark, never be NULL.
  RtmpAmf0Object* command_object;
  // Any optional information
  // @remark, optional, init to and maybe NULL.
  RtmpAmf0Object* args;
};
// Response  for RtmpConnectAppPacket.
class RtmpConnectAppResPacket : public RtmpPacket {
 public:
  RtmpConnectAppResPacket();
  ~RtmpConnectAppResPacket() override;

  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // The _result or _error; indicates whether the response is result or error.
  std::string command_name;
  // Transaction ID is 1 for call connect responses
  double transaction_id;
  // Name-value pairs that describe the properties(fmsver etc.) of the connection.
  // @remark, never be NULL.
  RtmpAmf0Object* props;
  // Name-value pairs that describe the response from|the server. 'code',
  // 'level', 'description' are names of few among such information.
  // @remark, never be NULL.
  RtmpAmf0Object* info;
};

// 4.1.2. Call
// The call method of the NetConnection object runs remote procedure
// calls (RPC) at the receiving end. The called RPC name is passed as a
// parameter to the call command.
class RtmpCallPacket : public RtmpPacket {
 public:
  RtmpCallPacket();
  ~RtmpCallPacket() override;
  srs_error_t decode(SrsBuffer* stream) override;

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the remote procedure that is called.
  std::string command_name;
  // If a response is expected we give a transaction Id. Else we pass a value of 0
  double transaction_id;
  // If there exists any command info this
  // is set, else this is set to null type.
  // @remark, optional, init to and maybe NULL.
  RtmpAmf0Any* command_object;
  // Any optional arguments to be provided
  // @remark, optional, init to and maybe NULL.
  RtmpAmf0Any* arguments;
};
// Response  for RtmpCallPacket.
class RtmpCallResPacket : public RtmpPacket {
 public:
  RtmpCallResPacket(double _transaction_id);
  ~RtmpCallResPacket() override;

 public:
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the command.
  std::string command_name;
  // ID of the command, to which the response belongs to
  double transaction_id;
  // If there exists any command info this is set, else this is set to null type.
  // @remark, optional, init to and maybe NULL.
  RtmpAmf0Any* command_object;
  // Response from the method that was called.
  // @remark, optional, init to and maybe NULL.
  RtmpAmf0Any* response;
};

// 4.1.3. createStream
// The client sends this command to the server to create a logical
// channel for message communication The publishing of audio, video, and
// metadata is carried out over stream channel created using the
// createStream command.
class RtmpCreateStreamPacket : public RtmpPacket {
 public:
  RtmpCreateStreamPacket();
  ~RtmpCreateStreamPacket() override;

 public:
  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size();
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the command. Set to "createStream".
  std::string command_name;
  // Transaction ID of the command.
  double transaction_id;
  // If there exists any command info this is set, else this is set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
};

// Response  for SrsCreateStreamPacket.
class RtmpCreateStreamResPacket : public RtmpPacket {
 public:
  RtmpCreateStreamResPacket(double _transaction_id, double _stream_id);
  ~RtmpCreateStreamResPacket() override;
  srs_error_t decode(SrsBuffer* stream) override;

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // The _result or _error; indicates whether the response is result or error.
  std::string command_name;
  // ID of the command that response belongs to.
  double transaction_id;
  // If there exists any command info this is set, else this is set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // The return value is either a stream ID or an error information object.
  double stream_id;
};

// client close stream packet.
class RtmpCloseStreamPacket : public RtmpPacket {
 public:
  RtmpCloseStreamPacket();
  ~RtmpCloseStreamPacket() override;
  
  srs_error_t decode(SrsBuffer* stream) override;
 public:
  // Name of the command, set to "closeStream".
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information object does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
};

// FMLE start publish: ReleaseStream/PublishStream/FCPublish/FCUnpublish
class RtmpFMLEStartPacket : public RtmpPacket {
 public:
  RtmpFMLEStartPacket();
  ~RtmpFMLEStartPacket() override;
 public:
  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size();
  srs_error_t encode_packet(SrsBuffer* stream) override;
  // Factory method to create specified FMLE packet.
 public:
  static RtmpFMLEStartPacket* create_release_stream(const std::string& stream);
  static RtmpFMLEStartPacket* create_FC_publish(const std::string& stream);
 public:
  // Name of the command
  std::string command_name;
  // The transaction ID to get the response.
  double transaction_id;
  // If there exists any command info this is set, else this is set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // The stream name to start publish or release.
  std::string stream_name;
};
// Response  for SrsFMLEStartPacket.
class RtmpFMLEStartResPacket : public RtmpPacket {
 public:
  RtmpFMLEStartResPacket(double _transaction_id);
  ~RtmpFMLEStartResPacket() override;
 public:
  srs_error_t decode(SrsBuffer* stream) override;

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size();
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the command
  std::string command_name;
  // The transaction ID to get the response.
  double transaction_id;
  // If there exists any command info this is set, else this is set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // The optional args, set to undefined.
  // @remark, never be NULL, an AMF0 undefined instance.
  RtmpAmf0Any* args; // undefined
};

// FMLE/flash publish
// 4.2.6. Publish
// The client sends the publish command to publish a named stream to the
// server. Using this name, any client can play this stream and receive
// The published audio, video, and data messages.
class RtmpPublishPacket : public RtmpPacket {
 public:
  RtmpPublishPacket();
  ~RtmpPublishPacket() override;

  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the command, set to "publish".
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information object does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // Name with which the stream is published.
  std::string stream_name;
  // Type of publishing. Set to "live", "record", or "append".
  //   record: The stream is published and the data is recorded to a new file.The file
  //           is stored on the server in a subdirectory within the directory that
  //           contains the server application. If the file already exists, it is
  //           overwritten.
  //   append: The stream is published and the data is appended to a file. If no file
  //           is found, it is created.
  //   live: Live data is published without recording it in a file.
  // @remark, SRS only support live.
  // @remark, optional, default to live.
  std::string type;
};

// 4.2.8. pause
// The client sends the pause command to tell the server to pause or
// start playing.
class RtmpPausePacket : public RtmpPacket {
 public:
  RtmpPausePacket();
  ~RtmpPausePacket() override;

  srs_error_t decode(SrsBuffer* stream) override;
 public:
  // Name of the command, set to "pause".
  std::string command_name;
  // There is no transaction ID for this command. Set to 0.
  double transaction_id;
  // Command information object does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // true or false, to indicate pausing or resuming play
  bool is_pause;
  // Number of milliseconds at which the the stream is paused or play resumed.
  // This is the current stream time at the Client when stream was paused. When the
  // playback is resumed, the server will only send messages with timestamps
  // greater than this value.
  double time_ms;
};

// 4.2.1. play
// The client sends this command to the server to play a stream.
class RtmpPlayPacket : public RtmpPacket {
 public:
  RtmpPlayPacket();
  ~RtmpPlayPacket() override;
 public:
  srs_error_t decode(SrsBuffer* stream) override;

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;

 public:
  // Name of the command. Set to "play".
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // Name of the stream to play.
  // To play video (FLV) files, specify the name of the stream without a file
  //       extension (for example, "sample").
  // To play back MP3 or ID3 tags, you must precede the stream name with mp3:
  //       (for example, "mp3:sample".)
  // To play H.264/AAC files, you must precede the stream name with mp4: and specify the
  //       file extension. For example, to play the file sample.m4v, specify
  //       "mp4:sample.m4v"
  std::string stream_name;
  // An optional parameter that specifies the start time in seconds.
  // The default value is -2, which means the subscriber first tries to play the live
  //       stream specified in the Stream Name field. If a live stream of that name is
  //       not found, it plays the recorded stream specified in the Stream Name field.
  // If you pass -1 in the Start field, only the live stream specified in the Stream
  //       Name field is played.
  // If you pass 0 or a positive number in the Start field, a recorded stream specified
  //       in the Stream Name field is played beginning from the time specified in the
  //       Start field.
  // If no recorded stream is found, the next item in the playlist is played.
  double start;
  // An optional parameter that specifies the duration of playback in seconds.
  // The default value is -1. The -1 value means a live stream is played until it is no
  //       longer available or a recorded stream is played until it ends.
  // If u pass 0, it plays the single frame since the time specified in the Start field
  //       from the beginning of a recorded stream. It is assumed that the value specified
  //       in the Start field is equal to or greater than 0.
  // If you pass a positive number, it plays a live stream for the time period specified
  //       in the Duration field. After that it becomes available or plays a recorded
  //       stream for the time specified in the Duration field. (If a stream ends before the
  //       time specified in the Duration field, playback ends when the stream ends.)
  // If you pass a negative number other than -1 in the Duration field, it interprets the
  //       value as if it were -1.
  double duration;
  // An optional Boolean value or number that specifies whether to flush any
  // previous playlist.
  bool reset;
};

// Response  for SrsPlayPacket.
// @remark, user must set the stream_id in header.
class RtmpPlayResPacket : public RtmpPacket {
 public:
  RtmpPlayResPacket();
  ~RtmpPlayResPacket() override;
 
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of the command. If the play command is successful, the command
  // name is set to onStatus.
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* command_object; // null
  // If the play command is successful, the client receives OnStatus message from
  // server which is NetStream.Play.Start. If the specified stream is not found,
  // NetStream.Play.StreamNotFound is received.
  // @remark, never be NULL, an AMF0 object instance.
  RtmpAmf0Object* desc;
};

// When bandwidth test done, notice client.
class RtmpOnBWDonePacket : public RtmpPacket {
 public:
  RtmpOnBWDonePacket();
  ~RtmpOnBWDonePacket() override;

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of command. Set to "onBWDone"
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* args; // null
};

// onStatus command, AMF0 Call
// @remark, user must set the stream_id by SrsCommonMessage.set_packet().
class RtmpOnStatusCallPacket : public RtmpPacket {
 public:
  RtmpOnStatusCallPacket();
  ~RtmpOnStatusCallPacket() override;

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of command. Set to "onStatus"
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* args; // null
  // Name-value pairs that describe the response from the server.
  // 'code','level', 'description' are names of few among such information.
  // @remark, never be NULL, an AMF0 object instance.
  RtmpAmf0Object* data;
};

// The special packet for the bandwidth test.
// actually, it's a SrsOnStatusCallPacket, but
// 1. encode with data field, to send data to client.
// 2. decode ignore the data field, donot care.
class RtmpBandwidthPacket : public RtmpPacket {
public:
  RtmpBandwidthPacket();
  ~RtmpBandwidthPacket() override;

 public:
  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
  // help function for bandwidth packet.
 public:
  virtual bool is_start_play();
  virtual bool is_starting_play();
  virtual bool is_stop_play();
  virtual bool is_stopped_play();
  virtual bool is_start_publish();
  virtual bool is_starting_publish();
  virtual bool is_stop_publish();
  virtual bool is_stopped_publish();
  virtual bool is_finish();
  virtual bool is_final();
  static RtmpBandwidthPacket* create_start_play();
  static RtmpBandwidthPacket* create_starting_play();
  static RtmpBandwidthPacket* create_playing();
  static RtmpBandwidthPacket* create_stop_play();
  static RtmpBandwidthPacket* create_stopped_play();
  static RtmpBandwidthPacket* create_start_publish();
  static RtmpBandwidthPacket* create_starting_publish();
  static RtmpBandwidthPacket* create_publishing();
  static RtmpBandwidthPacket* create_stop_publish();
  static RtmpBandwidthPacket* create_stopped_publish();
  static RtmpBandwidthPacket* create_finish();
  static RtmpBandwidthPacket* create_final();
 private:
  virtual RtmpBandwidthPacket* set_command(const std::string& command);

 public:
  // Name of command.
  std::string command_name;
  // Transaction ID set to 0.
  double transaction_id;
  // Command information does not exist. Set to null type.
  // @remark, never be NULL, an AMF0 null instance.
  RtmpAmf0Any* args; // null
  // Name-value pairs that describe the response from the server.
  // 'code','level', 'description' are names of few among such information.
  // @remark, never be NULL, an AMF0 object instance.
  RtmpAmf0Object* data;
};

// onStatus data, AMF0 Data
// @remark, user must set the stream_id by SrsCommonMessage.set_packet().
class RtmpOnStatusDataPacket : public RtmpPacket {
 public:
  RtmpOnStatusDataPacket();
  ~RtmpOnStatusDataPacket() override;
 public:
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of command. Set to "onStatus"
  std::string command_name;
  // Name-value pairs that describe the response from the server.
  // 'code', are names of few among such information.
  // @remark, never be NULL, an AMF0 object instance.
  RtmpAmf0Object* data;
};

// AMF0Data RtmpSampleAccess
// @remark, user must set the stream_id by SrsCommonMessage.set_packet().
class RtmpSampleAccessPacket : public RtmpPacket {
 public:
  RtmpSampleAccessPacket();

  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of command. Set to "|RtmpSampleAccess".
  std::string command_name;
  // Whether allow access the sample of video.
  // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#videoSampleAccess
  bool video_sample_access;
  // Whether allow access the sample of audio.
  // @see: http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/net/NetStream.html#audioSampleAccess
  bool audio_sample_access;
};

// The stream metadata.
// FMLE: @setDataFrame
// others: onMetaData
class RtmpOnMetaDataPacket : public RtmpPacket {
 public:
  RtmpOnMetaDataPacket();
  ~RtmpOnMetaDataPacket() override;

  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Name of metadata. Set to "onMetaData"
  std::string name;
  // Metadata of stream.
  // @remark, never be NULL, an AMF0 object instance.
  RtmpAmf0Object* metadata;
};

// 5.5. Window Acknowledgement Size (5)
// The client or the server sends this message to inform the peer which
// window size to use when sending acknowledgment.
class RtmpSetWindowAckSizePacket : public RtmpPacket {
 public:
  RtmpSetWindowAckSizePacket();
  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  int32_t ackowledgement_window_size;
};

// 5.3. Acknowledgement (3)
// The client or the server sends the acknowledgment to the peer after
// receiving bytes equal to the window size.
class RtmpAcknowledgementPacket : public RtmpPacket {
 public:
  RtmpAcknowledgementPacket();

  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  uint32_t sequence_number;
};

// 7.1. Set Chunk Size
// Protocol control message 1, Set Chunk Size, is used to notify the
// peer about the new maximum chunk size.
class RtmpSetChunkSizePacket : public RtmpPacket {
 public:
  RtmpSetChunkSizePacket();

  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
public:
  // The maximum chunk size can be 65536 bytes. The chunk size is
  // maintained independently for each direction.
  int32_t chunk_size;
};

// 5.6. Set Peer Bandwidth (6)
enum SrsPeerBandwidthType {
  // The sender can mark this message hard (0), soft (1), or dynamic (2)
  // using the Limit type field.
  RtmpPeerBandwidthHard = 0,
  RtmpPeerBandwidthSoft = 1,
  RtmpPeerBandwidthDynamic = 2,
};

// 5.6. Set Peer Bandwidth (6)
// The client or the server sends this message to update the output
// bandwidth of the peer.
class RtmpSetPeerBandwidthPacket : public RtmpPacket {
 public:
  RtmpSetPeerBandwidthPacket();
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  int32_t bandwidth;
  // @see: SrsPeerBandwidthType
  int8_t type;
};

// 3.7. User Control message
enum SrcPCUCEventType {
  // Generally, 4bytes event-data
  
  // The server sends this event to notify the client
  // that a stream has become functional and can be
  // used for communication. By default, this event
  // is sent on ID 0 after the application connect
  // command is successfully received from the
  // client. The event data is 4-byte and represents
  // The stream ID of the stream that became
  // Functional.
  SrcPCUCStreamBegin = 0x00,
  
  // The server sends this event to notify the client
  // that the playback of data is over as requested
  // on this stream. No more data is sent without
  // issuing additional commands. The client discards
  // The messages received for the stream. The
  // 4 bytes of event data represent the ID of the
  // stream on which playback has ended.
  SrcPCUCStreamEOF = 0x01,
  
  // The server sends this event to notify the client
  // that there is no more data on the stream. If the
  // server does not detect any message for a time
  // period, it can notify the subscribed clients
  // that the stream is dry. The 4 bytes of event
  // data represent the stream ID of the dry stream.
  SrcPCUCStreamDry = 0x02,
  
  // The client sends this event to inform the server
  // of the buffer size (in milliseconds) that is
  // used to buffer any data coming over a stream.
  // This event is sent before the server starts
  // processing the stream. The first 4 bytes of the
  // event data represent the stream ID and the next
  // 4 bytes represent the buffer length, in
  // milliseconds.
  SrcPCUCSetBufferLength = 0x03, // 8bytes event-data
  
  // The server sends this event to notify the client
  // that the stream is a recorded stream. The
  // 4 bytes event data represent the stream ID of
  // The recorded stream.
  SrcPCUCStreamIsRecorded = 0x04,
  
  // The server sends this event to test whether the
  // client is reachable. Event data is a 4-byte
  // timestamp, representing the local server time
  // When the server dispatched the command. The
  // client responds with kMsgPingResponse on
  // receiving kMsgPingRequest.
  SrcPCUCPingRequest = 0x06,
  
  // The client sends this event to the server in
  // Response  to the ping request. The event data is
  // a 4-byte timestamp, which was received with the
  // kMsgPingRequest request.
  SrcPCUCPingResponse = 0x07,
  
  // For PCUC size=3, for example the payload is "00 1A 01",
  // it's a FMS control event, where the event type is 0x001a and event data is 0x01,
  // please notice that the event data is only 1 byte for this event.
  RtmpPCUCFmsEvent0 = 0x1a,
};

// 5.4. User Control Message (4)
//
// For the EventData is 4bytes.
// Stream Begin(=0)              4-bytes stream ID
// Stream EOF(=1)                4-bytes stream ID
// StreamDry(=2)                 4-bytes stream ID
// SetBufferLength(=3)           8-bytes 4bytes stream ID, 4bytes buffer length.
// StreamIsRecorded(=4)          4-bytes stream ID
// PingRequest(=6)               4-bytes timestamp local server time
// PingResponse(=7)              4-bytes timestamp received ping request.
//
// 3.7. User Control message
// +------------------------------+-------------------------
// | Event Type ( 2- bytes ) | Event Data
// +------------------------------+-------------------------
// Figure 5 Pay load for the 'User Control Message'.
class RtmpUserControlPacket : public RtmpPacket {
 public:
  RtmpUserControlPacket();
  srs_error_t decode(SrsBuffer* stream) override;
  int get_prefer_cid() override;
  int get_message_type() override;
 protected:
  int get_size() override;
  srs_error_t encode_packet(SrsBuffer* stream) override;
 public:
  // Event type is followed by Event data.
  // @see: SrcPCUCEventType
  int16_t event_type;
  // The event data generally in 4bytes.
  // @remark for event type is 0x001a, only 1bytes.
  // @see SrsPCUCFmsEvent0
  int32_t event_data;
  // 4bytes if event_type is SetBufferLength; otherwise 0.
  int32_t extra_data;
};

} //namespace ma

#endif //!__MEDIA_RTMP_MESSAGE_H__
