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

#include "media_rtmp_msg.h"

#include "common/media_consts.h"
#include "common/media_log.h"
#include "utils/media_serializer_T.h"
#include "connection/h/media_io.h"
#include "rtmp/media_rtmp_const.h"
#include "rtmp/media_protocol_stream.h"

namespace ma {

// The amf0 command message, command name macros
#define RTMP_AMF0_COMMAND_CONNECT               "connect"
#define RTMP_AMF0_COMMAND_CREATE_STREAM         "createStream"
#define RTMP_AMF0_COMMAND_CLOSE_STREAM          "closeStream"
#define RTMP_AMF0_COMMAND_PLAY                  "play"
#define RTMP_AMF0_COMMAND_PAUSE                 "pause"
#define RTMP_AMF0_COMMAND_ON_BW_DONE            "onBWDone"
#define RTMP_AMF0_COMMAND_ON_STATUS             "onStatus"
#define RTMP_AMF0_COMMAND_RESULT                "_result"
#define RTMP_AMF0_COMMAND_ERROR                 "_error"
#define RTMP_AMF0_COMMAND_RELEASE_STREAM        "releaseStream"
#define RTMP_AMF0_COMMAND_FC_PUBLISH            "FCPublish"
#define RTMP_AMF0_COMMAND_UNPUBLISH             "FCUnpublish"
#define RTMP_AMF0_COMMAND_PUBLISH               "publish"
#define RTMP_AMF0_DATA_SAMPLE_ACCESS            "|RtmpSampleAccess"



#define RTMP_FMT_TYPE0                          0
#define RTMP_FMT_TYPE1                          1
#define RTMP_FMT_TYPE2                          2
#define RTMP_FMT_TYPE3                          3

/**
 * band width check method name, which will be invoked by client.
 * band width check mothods use RtmpBandwidthPacket as its internal packet type,
 * so ensure you set command name when you use it.
 */
// server play control
#define SRS_BW_CHECK_START_PLAY                 "onSrsBandCheckStartPlayBytes"
#define SRS_BW_CHECK_STARTING_PLAY              "onSrsBandCheckStartingPlayBytes"
#define SRS_BW_CHECK_STOP_PLAY                  "onSrsBandCheckStopPlayBytes"
#define SRS_BW_CHECK_STOPPED_PLAY               "onSrsBandCheckStoppedPlayBytes"

// server publish control
#define SRS_BW_CHECK_START_PUBLISH              "onSrsBandCheckStartPublishBytes"
#define SRS_BW_CHECK_STARTING_PUBLISH           "onSrsBandCheckStartingPublishBytes"
#define SRS_BW_CHECK_STOP_PUBLISH               "onSrsBandCheckStopPublishBytes"
// @remark, flash never send out this packet, for its queue is full.
#define SRS_BW_CHECK_STOPPED_PUBLISH            "onSrsBandCheckStoppedPublishBytes"

// EOF control.
// the report packet when check finished.
#define SRS_BW_CHECK_FINISHED                   "onSrsBandCheckFinished"
// @remark, flash never send out this packet, for its queue is full.
#define SRS_BW_CHECK_FINAL                      "finalClientPacket"

// data packets
#define SRS_BW_CHECK_PLAYING                    "onSrsBandCheckPlaying"
#define SRS_BW_CHECK_PUBLISHING                 "onSrsBandCheckPublishing"


static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.rtmp");

static char mh_sizes[] = {11, 7, 3, 0};

RtmpProtocal::RtmpProtocal() {
  if (SRS_PERF_CHUNK_STREAM_CACHE > 0) {
    cs_cache_ = new RtmpChunkStream*[SRS_PERF_CHUNK_STREAM_CACHE];
  }
  for (int cid = 0; cid < SRS_PERF_CHUNK_STREAM_CACHE; cid++) {
    RtmpChunkStream* cs = new RtmpChunkStream(cid);
    // set the perfer cid of chunk,
    // which will copy to the message received.
    cs->header.perfer_cid = cid;
    cs_cache_[cid] = cs;
  }
}

RtmpProtocal::~RtmpProtocal() {
  for (auto& it : chunk_streams_) {
    RtmpChunkStream* stream = it.second;
    delete stream;
  }
  chunk_streams_.clear();

    // free all chunk stream cache.
  for (int i = 0; i < SRS_PERF_CHUNK_STREAM_CACHE; i++) {
    RtmpChunkStream* cs = cs_cache_[i];
    delete cs;
  }
  delete[] cs_cache_;

  if (read_buffer_) {
    read_buffer_->DestroyChained();
  }
}

void RtmpProtocal::Open(std::shared_ptr<RtmpBufferIO> io, RtmpProtocalSink* sink) {
  io_ = std::move(io);
  io_->SetSink(this);
  sink_ = sink;
}

void RtmpProtocal::Close() {
  io_ = nullptr;
}

srs_error_t RtmpProtocal::OnRead(MessageChain* msg) {
  if (read_buffer_) {
    read_buffer_->Append(msg->DuplicateChained());
  } else {
    read_buffer_ = msg->DuplicateChained();
  }

  srs_error_t err = srs_success;
  while(read_buffer_ && read_buffer_->GetChainedLength() != 0) {
    std::shared_ptr<MediaMessage> parsed_msg;
    if (srs_success != (err = ParseMsg(parsed_msg))) {
      if (srs_error_code(err) == ERROR_RTMP_MSG_INVALID_SIZE) {
        delete err;
        err = srs_success;
      }
      break;
    }

    if (parsed_msg) {
      if (parsed_msg->header_.is_ackledgement() || 
          parsed_msg->header_.is_set_chunk_size() || 
          parsed_msg->header_.is_window_ackledgement_size() || 
          parsed_msg->header_.is_user_control_message()) {
        InternalProcessMessage(parsed_msg.get());
        continue;
      }
      err = sink_->OnPacket(std::move(parsed_msg));
    }
  }

  if(read_buffer_ && read_buffer_->GetChainedLength() == 0) {
		read_buffer_->DestroyChained();
		read_buffer_ = nullptr;
	}

  return err;
}

srs_error_t RtmpProtocal::ParseMsg(std::shared_ptr<MediaMessage>& out) {
  RtmpChunkStream*& chunk = current_chunk_;
  if (!chunk) {
    read_buffer_->SaveChainedReadPtr();
    char fmt = 0;
    int cid = 0;
    bool err = ReadBasicHeader(fmt, cid);

    if (!err) {
      // not enough data
      read_buffer_->RewindChained(true);
      return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, "read basic header");
    }

    // the cid must not negative.
    MA_ASSERT(cid >= 0);

    // ensure there's enough message for ReadMessageHeader
    uint32_t len = read_buffer_->GetChainedLength();
    if ((int)len < (int)mh_sizes[(int)fmt]) {
      read_buffer_->RewindChained(true);
      return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, "read message header");
    }
    int32_t ts = 0;
    read_buffer_->Peek(&ts, 3);

    // check ReadMessageHeader length when extern timestamp enabled
    if ((int)fmt < RTMP_FMT_TYPE3 && ts == 0xffffff && (int)len < (int)mh_sizes[(int)fmt] + 4) {
      read_buffer_->RewindChained(true);
      return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, "read message header");
    }
    
    // get the cached chunk stream.
    if (cid < SRS_PERF_CHUNK_STREAM_CACHE) {
      chunk = cs_cache_[cid];
    } else {
      if (chunk_streams_.find(cid) == chunk_streams_.end()) {
        chunk = chunk_streams_[cid] = new RtmpChunkStream(cid);
        chunk->header.perfer_cid = cid;
      } else {
        chunk = chunk_streams_[cid];
      }
    }

    // chunk stream message header
    do {
      srs_error_t err = srs_success;
      if ((err = ReadMessageHeader(chunk, fmt)) != srs_success) {
        return srs_error_wrap(err, "read message header");
      }
    } while(false);
  }
  
  // read msg payload from chunk stream.
  auto msg_opt = ReadMessagePayload(chunk);
  
  // not got an entire RTMP message, try next chunk.
  if (!msg_opt) {
    return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, "read message chunk");
  }

  chunk = nullptr;

  auto msg = std::move(*msg_opt);
  if (msg->size_ <= 0 || msg->header_.payload_length <= 0) {
    MLOG_CTRACE("ignore empty message"
        "(type=%d, size=%d, time=%" PRId64 ", sid=%d).",
        msg->header_.message_type, msg->header_.payload_length,
        msg->header_.timestamp, msg->header_.stream_id);
  } else {
    out = std::move(msg);
  }

  return srs_success;
}

srs_error_t RtmpProtocal::SetInWinAckSize(int ack_size) {
  in_ack_size_.window = ack_size;
  return srs_success;
}

srs_error_t RtmpProtocal::DecodeMessage(
    MediaMessage* msg, RtmpPacket*& rPacket) {  
  srs_error_t err = srs_success;
  
  srs_assert(msg != nullptr);
  srs_assert(msg->payload_ != nullptr);
  srs_assert(msg->size_ > 0);
  
  RtmpPacket* packet = nullptr;
  MessageHeader& header = msg->header_;
  MediaStreamBE stream_helper(*(msg->payload_));
  SrsBuffer* stream = dynamic_cast<SrsBuffer*>(&stream_helper);

  msg->payload_->SaveChainedReadPtr();

  // decode specified packet type
  if (header.is_amf0_command() || 
      header.is_amf3_command() || 
      header.is_amf0_data() || 
      header.is_amf3_data()) {
    // skip 1bytes to decode the amf3 command.
    if (header.is_amf3_command() && stream->require(1)) {
      stream->skip(1);
    }
    
    // amf0 command message.
    // need to read the command name.
    std::string command;
    if ((err = srs_amf0_read_string(stream, command)) != srs_success) {
      return srs_error_wrap(err, "decode command name");
    }
    
    // result/error packet
    if (command == RTMP_AMF0_COMMAND_RESULT || 
        command == RTMP_AMF0_COMMAND_ERROR) {
      double transactionId = 0.0;
      if ((err = srs_amf0_read_number(stream, transactionId)) != srs_success) {
        return srs_error_wrap(err, "decode tid for %s", command.c_str());
      }
      
      // reset stream, for header read completed.
      msg->payload_->RewindChained(true);
      if (header.is_amf3_command()) {
        stream->skip(1);
      }
      
      // find the call name
      if (requests_.find(transactionId) == requests_.end()) {
        return srs_error_new(ERROR_RTMP_NO_REQUEST, 
            "find request for command=%s, tid=%.2f", 
            command.c_str(), transactionId);
      }
      
      std::string request_name = requests_[transactionId];
      if (request_name == RTMP_AMF0_COMMAND_CONNECT) {
        rPacket = packet = new RtmpConnectAppResPacket();
        return packet->decode(stream);
      } else if (request_name == RTMP_AMF0_COMMAND_CREATE_STREAM) {
        rPacket = packet = new RtmpCreateStreamResPacket(0, 0);
        return packet->decode(stream);
      } else if (request_name == RTMP_AMF0_COMMAND_RELEASE_STREAM) {
        rPacket = packet = new RtmpFMLEStartResPacket(0);
        return packet->decode(stream);
      } else if (request_name == RTMP_AMF0_COMMAND_FC_PUBLISH) {
        rPacket = packet = new RtmpFMLEStartResPacket(0);
        return packet->decode(stream);
      } else if (request_name == RTMP_AMF0_COMMAND_UNPUBLISH) {
        rPacket = packet = new RtmpFMLEStartResPacket(0);
        return packet->decode(stream);
      } else {
        return srs_error_new(ERROR_RTMP_NO_REQUEST, "request=%s, tid=%.2f",
            request_name.c_str(), transactionId);
      }
    }
    
    // reset to zero(amf3 to 1) to restart decode.
    msg->payload_->RewindChained(true);
    if (header.is_amf3_command()) {
      stream->skip(1);
    }
    
    // decode command object.
    if (command == RTMP_AMF0_COMMAND_CONNECT) {
      rPacket = packet = new RtmpConnectAppPacket();
      return packet->decode(stream);
    } else if (command == RTMP_AMF0_COMMAND_CREATE_STREAM) {
      rPacket = packet = new RtmpCreateStreamPacket();
      return packet->decode(stream);
    } else if (command == RTMP_AMF0_COMMAND_PLAY) {
      rPacket = packet = new RtmpPlayPacket();
      return packet->decode(stream);
    } else if (command == RTMP_AMF0_COMMAND_PAUSE) {
      rPacket = packet = new RtmpPausePacket();
      return packet->decode(stream);
    } else if (command == RTMP_AMF0_COMMAND_RELEASE_STREAM ||
        command == RTMP_AMF0_COMMAND_FC_PUBLISH || 
        command == RTMP_AMF0_COMMAND_UNPUBLISH) {
      rPacket = packet = new RtmpFMLEStartPacket();
      return packet->decode(stream);
    } else if (command == RTMP_AMF0_COMMAND_PUBLISH) {
      rPacket = packet = new RtmpPublishPacket();
      return packet->decode(stream);
    } else if (command == SRS_CONSTS_RTMP_SET_DATAFRAME) {
      rPacket = packet = new RtmpOnMetaDataPacket();
      return packet->decode(stream);
    } else if (command == SRS_CONSTS_RTMP_ON_METADATA) {
      rPacket = packet = new RtmpOnMetaDataPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_FINISHED) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_PLAYING) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_PUBLISHING) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_STARTING_PLAY) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_STARTING_PUBLISH) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_START_PLAY) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_START_PUBLISH) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_STOPPED_PLAY) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_STOP_PLAY) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_STOP_PUBLISH) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_STOPPED_PUBLISH) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == SRS_BW_CHECK_FINAL) {
      rPacket = packet = new RtmpBandwidthPacket();
      return packet->decode(stream);
    } else if (command == RTMP_AMF0_COMMAND_CLOSE_STREAM) {
      rPacket = packet = new RtmpCloseStreamPacket();
      return packet->decode(stream);
    } else if (header.is_amf0_command() || header.is_amf3_command()) {
      rPacket = packet = new RtmpCallPacket();
      return packet->decode(stream);
    }
    
    // default packet to drop message.
    rPacket = packet = new RtmpPacket();
    return err;
  } else if (header.is_user_control_message()) {
    rPacket = packet = new RtmpUserControlPacket();
    return packet->decode(stream);
  } else if (header.is_window_ackledgement_size()) {
    rPacket = packet = new RtmpSetWindowAckSizePacket();
    return packet->decode(stream);
  } else if (header.is_ackledgement()) {
    rPacket = packet = new RtmpAcknowledgementPacket();
    return packet->decode(stream);
  } else if (header.is_set_chunk_size()) {
    rPacket = packet = new RtmpSetChunkSizePacket();
    return packet->decode(stream);
  } else {
    if (!header.is_set_peer_bandwidth() && !header.is_ackledgement()) {
      MLOG_CTRACE("drop unknown message, type=%d", header.message_type);
    }
  }
  return err;
}

srs_error_t RtmpProtocal::Write(RtmpPacket* packet, int stream_id) {
  srs_error_t err = srs_success;

  auto shared_msg = std::make_shared<MediaMessage>();
  if ((err = packet->to_msg(shared_msg.get(), stream_id)) != srs_success) {
    return srs_error_wrap(err, "to message");
  }

  if ((err = SendWithBuffer(shared_msg)) != srs_success) {
    return srs_error_wrap(err, "send packet");
  }
  
  if ((err = OnSendPacket(&shared_msg->header_, packet)) != srs_success) {
    return srs_error_wrap(err, "on send packet");
  }
  
  return err;
}

srs_error_t RtmpProtocal::SendWithBuffer(std::shared_ptr<MediaMessage> msg) {
  // try to send use the c0c3 header cache,
  srs_error_t err = srs_success;

  if (!msg->payload_ || msg->size_ <= 0) {
    return err;
  }
  
  int left_size = msg->size_;
  MessageChain* head = nullptr;
  MessageChain* left = msg->payload_->DuplicateChained();
  
  bool first = true;
  char* c0c3_cache = out_c0c3_caches_;
  int nb_cache = SRS_CONSTS_C0C3_HEADERS_MAX;

  while (true) {
    // always has header
    int nbh = msg->ChunkHeader(c0c3_cache, nb_cache, first);
    MA_ASSERT(nbh > 0);

    if (first)
      first = false;
    
    // header
    MessageChain chunk_header(nbh, c0c3_cache, MessageChain::DONT_DELETE, nbh);
    MessageChain* pchunk_header = chunk_header.DuplicateChained();
    
    // payload
    pchunk_header->Append(left);
    int payload_size = std::min<int>(out_chunk_size_, left_size);
    left = left->Disjoint(payload_size);
    
    if (head) {
      head->Append(pchunk_header);
    } else {
      head = pchunk_header;
    }

    left_size -= payload_size;

    //finish
    if (left_size <= 0)
      break;
  }
  MA_ASSERT(left == nullptr);
  // now send msg
  if (head && (err = io_->Write(head)) != srs_success) {
  }

  return err;
}

void RtmpProtocal::OnDisc(srs_error_t) {

}

/**
 * 6.1.1. Chunk Basic Header
 * The Chunk Basic Header encodes the chunk stream ID and the chunk
 * type(represented by fmt field in the figure below). Chunk type
 * determines the format of the encoded message header. Chunk Basic
 * Header field may be 1, 2, or 3 bytes, depending on the chunk stream
 * ID.
 *
 * The bits 0-5 (least significant) in the chunk basic header represent
 * the chunk stream ID.
 *
 * Chunk stream IDs 2-63 can be encoded in the 1-byte version of this
 * field.
 *    0 1 2 3 4 5 6 7
 *   +-+-+-+-+-+-+-+-+
 *   |fmt|   cs id   |
 *   +-+-+-+-+-+-+-+-+
 *   Figure 6 Chunk basic header 1
 *
 * Chunk stream IDs 64-319 can be encoded in the 2-byte version of this
 * field. ID is computed as (the second byte + 64).
 *   0                   1
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |fmt|    0      | cs id - 64    |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   Figure 7 Chunk basic header 2
 *
 * Chunk stream IDs 64-65599 can be encoded in the 3-byte version of
 * this field. ID is computed as ((the third byte)*256 + the second byte
 * + 64).
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   |fmt|     1     |         cs id - 64            |
 *   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *   Figure 8 Chunk basic header 3
 *
 * cs id: 6 bits
 * fmt: 2 bits
 * cs id - 64: 8 or 16 bits
 *
 * Chunk stream IDs with values 64-319 could be represented by both 2-
 * byte version and 3-byte version of this field.
 */
bool RtmpProtocal::ReadBasicHeader(char& fmt, int& cid) {
  uint8_t byte;

  int err = read_buffer_->Read(&byte, 1);
  if (MessageChain::error_part_data == err) {
    return false;
  }

  cid = byte & 0x3f;
  fmt = (byte >> 6) & 0x03;
  
  // 2-63, 1B chunk header
  if (cid > 1)
    return true;

  // 64-319, 2B chunk header
  if (cid == 0) {
    if (MessageChain::error_part_data == (err = read_buffer_->Read(&byte, 1))) {
      return false;
    }

    cid = 64;
    cid += byte;
    return true;
  }

  // 64-65599, 3B chunk header
  MA_ASSERT(cid == 1);

  uint8_t _2bytes[2];
  if (MessageChain::error_part_data == 
      (err=read_buffer_->Read(_2bytes, 2))) {
    return false;
  }
  
  cid = 64;
  cid += (int)_2bytes[0];
  cid += (int)_2bytes[1] * 256;
  
  return true;
}


srs_error_t RtmpProtocal::ReadMessageHeader(RtmpChunkStream* chunk, char fmt) {
  srs_error_t err = srs_success;
  //when first packet, the chunk->msg is NULL).
  bool is_first_chunk_of_msg = !chunk->msg;
  
  // but, we can ensure that when a chunk stream is fresh,
  // the fmt must be 0, a new stream.
  if (chunk->msg_count == 0 && fmt != RTMP_FMT_TYPE0) {
    if (fmt == RTMP_FMT_TYPE1) {
      MLOG_WARN("fresh chunk starts with fmt=1");
    } else {
      // must be a RTMP protocol level error.
      return srs_error_new(ERROR_RTMP_CHUNK_START, 
          "fresh chunk expect fmt=0, actual=%d, cid=%d", fmt, chunk->cid);
    }
  }
  
  // when exists cache msg, means got an partial message,
  // the fmt must not be type0 which means new message.
  if (chunk->msg && fmt == RTMP_FMT_TYPE0) {
    return srs_error_new(ERROR_RTMP_CHUNK_START, 
        "for existed chunk, fmt should not be 0");
  }
  
  // create msg when new chunk stream start
  if (!chunk->msg) {
    chunk->msg = std::make_shared<MediaMessage>();
  }
  
  int mh_size = mh_sizes[(int)fmt];
  
  char bytes[15];
  int ret = read_buffer_->Read(bytes, mh_size);
  if (mh_size > 0 && (MessageChain::error_part_data == ret)) {
    // never reach here
    MA_ASSERT(false);
  }
  
  if (fmt <= RTMP_FMT_TYPE2) {
    char* p = bytes;
    char* pp = (char*)&chunk->header.timestamp_delta;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    pp[3] = 0;
    
    chunk->extended_timestamp = 
        (chunk->header.timestamp_delta >= RTMP_EXTENDED_TIMESTAMP);
    if (!chunk->extended_timestamp) {
      if (RTMP_FMT_TYPE0 == fmt) {
        chunk->header.timestamp = chunk->header.timestamp_delta;
      } else {
        chunk->header.timestamp += chunk->header.timestamp_delta;
      }
    }
    
    if (fmt <= RTMP_FMT_TYPE1) {
      int32_t payload_length = 0;
      pp = (char*)&payload_length;
      pp[2] = *p++;
      pp[1] = *p++;
      pp[0] = *p++;
      pp[3] = 0;
      
      // check message length of fmt 0 and fmt 1
      if (!is_first_chunk_of_msg && 
          chunk->header.payload_length != payload_length) {
        return srs_error_new(ERROR_RTMP_PACKET_SIZE, 
            "msg in chunk cache, size=%d cannot change to %d", 
            chunk->header.payload_length, payload_length);
      }
      
      chunk->header.payload_length = payload_length;
      chunk->header.message_type = *p++;
      
      if (fmt == RTMP_FMT_TYPE0) {
          pp = (char*)&chunk->header.stream_id;
          pp[0] = *p++;
          pp[1] = *p++;
          pp[2] = *p++;
          pp[3] = *p++;
      }
    }
  } else {
    // update the timestamp even fmt=3 for first chunk packet
    if (is_first_chunk_of_msg && !chunk->extended_timestamp) {
      chunk->header.timestamp += chunk->header.timestamp_delta;
    }
  }
  
  // read extended-timestamp
  if (chunk->extended_timestamp) {
    mh_size += 4;
    ret = read_buffer_->Peek(bytes, 4);
    MA_ASSERT(MessageChain::error_part_data == ret);

    char* p = bytes;
    
    uint32_t timestamp = 0x00;
    char* pp = (char*)&timestamp;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    
    // always use 31bits timestamp, for some server may use 32bits extended timestamp.
    timestamp &= 0x7fffffff;
    
    uint32_t chunk_timestamp = (uint32_t)chunk->header.timestamp;
    
    if (!is_first_chunk_of_msg && chunk_timestamp > 0 && 
        chunk_timestamp != timestamp) {
      mh_size -= 4;
    } else {
      /**
       * about the is_first_chunk_of_msg.
       * @remark, for the first chunk of message, always use the extended timestamp.
       */
      /**
       * if chunk_timestamp<=0, the chunk previous packet has no extended-timestamp,
       * always use the extended timestamp.
       */
      chunk->header.timestamp = timestamp;
      read_buffer_->AdvanceChainedReadPtr(4);
    }
  }
  
  chunk->header.timestamp &= 0x7fffffff;
  
  // valid message, the payload_length is 24bits, so it should never be negative.
  MA_ASSERT(chunk->header.payload_length >= 0);
  
  // copy header to msg
  chunk->msg->header_ = chunk->header;
  
  // increase the msg count, the chunk stream can accept fmt=1/2/3 message now.
  chunk->msg_count++;
  
  return err;
}

std::optional<std::shared_ptr<MediaMessage>> 
RtmpProtocal::ReadMessagePayload(RtmpChunkStream* chunk) {
  // empty message
  if (chunk->header.payload_length <= 0) {
    MLOG_CTRACE("get an empty RTMP message"
        "(type=%d, size=%d, time=%" PRId64 ", sid=%d)", 
        chunk->header.message_type,
        chunk->header.payload_length, 
        chunk->header.timestamp, 
        chunk->header.stream_id);
    
    return std::move(chunk->msg);
  }
  
  // the chunk payload size.
  int payload_size = chunk->header.payload_length - chunk->msg->size_;
  uint32_t msg_len = read_buffer_->GetChainedLength();
  payload_size = std::min<int>(
      std::min<int>(payload_size, in_chunk_size_), msg_len);

  MessageChain* chunk_msg = read_buffer_;
  read_buffer_ = read_buffer_->Disjoint(payload_size);

  if (!chunk->msg->payload_) {
    chunk->msg->payload_ = chunk_msg;
  } else {
    chunk->msg->payload_->Append(chunk_msg);
  }

  chunk->msg->size_ += payload_size;
  
  // got entire RTMP message?
  if (chunk->header.payload_length == chunk->msg->size_) {
    return std::move(chunk->msg);
  }
  return std::nullopt;
}

srs_error_t RtmpProtocal::InternalProcessMessage(MediaMessage* msg) {
  srs_error_t err = srs_success;
  
  // try to response acknowledgement
  if ((err = ResponseAcknowledgement()) != srs_success) {
    return srs_error_wrap(err, "response ack");
  }
  
  RtmpPacket* packet = NULL;
  switch (msg->header_.message_type) {
    case RTMP_MSG_SetChunkSize:
    case RTMP_MSG_UserControlMessage:
    case RTMP_MSG_WindowAcknowledgementSize:
      if ((err = DecodeMessage(msg, packet)) != srs_success) {
        return srs_error_wrap(err, "decode message");
      }
      break;
    case RTMP_MSG_VideoMessage:
    case RTMP_MSG_AudioMessage:
      print_debug_info();
    default:
      return err;
  }
  
  srs_assert(packet);

  // always free the packet.
  std::unique_ptr<RtmpPacket> packet_guard(packet);
  
  switch (msg->header_.message_type) {
    case RTMP_MSG_WindowAcknowledgementSize: {
      RtmpSetWindowAckSizePacket* pkt =
          dynamic_cast<RtmpSetWindowAckSizePacket*>(packet);
  
      if (pkt->ackowledgement_window_size > 0) {
        in_ack_size_.window = (uint32_t)pkt->ackowledgement_window_size;
        // @remark, we ignore this message, for user noneed to care.
        // but it's important for dev, for client/server will block 
        // if required ack msg not arrived.
      }
      break;
    }
    case RTMP_MSG_SetChunkSize: {
      RtmpSetChunkSizePacket* pkt = 
          dynamic_cast<RtmpSetChunkSizePacket*>(packet);
      
      if (pkt->chunk_size < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE) {
        return srs_error_new(ERROR_RTMP_CHUNK_SIZE, 
            "chunk size should be %d+, value=%d", 
            SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, pkt->chunk_size);
      }
      
      in_chunk_size_ = pkt->chunk_size;
      MLOG_TRACE("set chunk size " << in_chunk_size_);
      break;
    }
    case RTMP_MSG_UserControlMessage: {
      RtmpUserControlPacket* pkt = dynamic_cast<RtmpUserControlPacket*>(packet);
      if (pkt->event_type == SrcPCUCSetBufferLength) {
        in_buffer_length_ = pkt->extra_data;
      }

      if (pkt->event_type == SrcPCUCPingRequest) {
        if ((err = ResponsePingMessage(pkt->event_data)) != srs_success) {
          return srs_error_wrap(err, "response ping");
        }
      }
      break;
    }
    default:
      ;
  }
  
  return err;
}

srs_error_t RtmpProtocal::OnSendPacket(MessageHeader* mh, RtmpPacket* packet) {
  srs_error_t err = srs_success;
  
  // ignore raw bytes oriented RTMP message.
  if (packet == NULL) {
    return err;
  }
  
  switch (mh->message_type) {
    case RTMP_MSG_SetChunkSize: {
      RtmpSetChunkSizePacket* pkt = 
          dynamic_cast<RtmpSetChunkSizePacket*>(packet);
      out_chunk_size_ = pkt->chunk_size;
      break;
    }
    case RTMP_MSG_WindowAcknowledgementSize: {
      RtmpSetWindowAckSizePacket* pkt = 
          dynamic_cast<RtmpSetWindowAckSizePacket*>(packet);
      out_ack_size_.window = (uint32_t)pkt->ackowledgement_window_size;
      break;
    }
    case RTMP_MSG_AMF0CommandMessage:
    case RTMP_MSG_AMF3CommandMessage: {
      if (true) {
        RtmpConnectAppPacket* pkt = dynamic_cast<RtmpConnectAppPacket*>(packet);
        if (pkt) {
          requests_[pkt->transaction_id] = pkt->command_name;
          break;
        }
      }
      if (true) {
        RtmpCreateStreamPacket* pkt = dynamic_cast<RtmpCreateStreamPacket*>(packet);
        if (pkt) {
          requests_[pkt->transaction_id] = pkt->command_name;
          break;
        }
      }
      if (true) {
        RtmpFMLEStartPacket* pkt = dynamic_cast<RtmpFMLEStartPacket*>(packet);
        if (pkt) {
          requests_[pkt->transaction_id] = pkt->command_name;
          break;
        }
      }
      break;
    }
    case RTMP_MSG_VideoMessage:
    case RTMP_MSG_AudioMessage:
      print_debug_info();
    default:
      ;
  }
  
  return err;
}

srs_error_t RtmpProtocal::ResponseAcknowledgement() {
  srs_error_t err = srs_success;
  
  if (in_ack_size_.window <= 0) {
    return err;
  }
  
  // ignore when delta bytes not exceed half of window(ack size).
  int64_t recv_bytes = io_->GetRecvBytes();
  uint32_t delta = (uint32_t)(recv_bytes - in_ack_size_.nb_recv_bytes);
  if (delta < in_ack_size_.window / 2) {
    return err;
  }
  in_ack_size_.nb_recv_bytes = recv_bytes;
  
  // when the sequence number overflow, reset it.
  uint32_t sequence_number = in_ack_size_.sequence_number + delta;
  if (sequence_number > 0xf0000000) {
    sequence_number = delta;
  }
  in_ack_size_.sequence_number = sequence_number;
  
  RtmpAcknowledgementPacket pkt;
  pkt.sequence_number = sequence_number;
  
  if ((err = Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "send ack");
  }
  
  return err;
}

srs_error_t RtmpProtocal::ResponsePingMessage(int32_t timestamp) {
  srs_error_t err = srs_success;
  
  MLOG_CTRACE("get a ping request, response it. timestamp=%d", timestamp);
  
  RtmpUserControlPacket pkt;
  
  pkt.event_type = SrcPCUCPingResponse;
  pkt.event_data = timestamp;
  
  if ((err = Write(&pkt, 0)) != srs_success) {
    return srs_error_wrap(err, "ping response");
  }
  
  return err;
}

//RtmpPacket
srs_error_t RtmpPacket::to_msg(MediaMessage* msg, int stream_id) {
  srs_error_t err = srs_success;

  int size = get_size();
  if (size <= 0)
    return err;

  MessageChain mb(size);
  SrsBuffer stream(mb.GetFirstMsgWritePtr(), size);
  if ((err = encode_packet(&stream)) != srs_success) {
    return srs_error_wrap(err, "serialize packet");
  }
  mb.AdvanceChainedWritePtr(size);

  // to message
  MessageHeader header;
  header.payload_length = size;
  header.message_type = get_message_type();
  header.stream_id = stream_id;
  header.perfer_cid = get_prefer_cid();

  msg->create(&header, &mb);
  return err;
}

srs_error_t RtmpPacket::encode(std::shared_ptr<DataBlock> payload) {
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

srs_error_t RtmpPacket::decode(SrsBuffer* stream) {
  return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "decode");
}

int RtmpPacket::get_prefer_cid() {
  return 0;
}

int RtmpPacket::get_message_type() {
  return 0;
}

int RtmpPacket::get_size() {
  return 0;
}

srs_error_t RtmpPacket::encode_packet(SrsBuffer* stream) {
  return srs_error_new(ERROR_SYSTEM_PACKET_INVALID, "encode");
}

//RtmpChunkStream
RtmpChunkStream::RtmpChunkStream(int _cid) {
  cid = _cid;
}

//RtmpConnectAppPacket
RtmpConnectAppPacket::RtmpConnectAppPacket() 
    : command_name(RTMP_AMF0_COMMAND_CONNECT),
      transaction_id(1),
      command_object(RtmpAmf0Any::object()),
      args(nullptr) {
}

RtmpConnectAppPacket::~RtmpConnectAppPacket() {
  srs_freep(command_object);
  srs_freep(args);
}

srs_error_t RtmpConnectAppPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_CONNECT) {
    return srs_error_new(ERROR_RTMP_AMF0_DECODE, 
        "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  // some client donot send id=1.0, so we only warn user if not match.
  if (transaction_id != 1.0) {
    MLOG_CWARN("invalid transaction_id=%.2f", transaction_id);
  }
  
  if ((err = command_object->read(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  if (!stream->empty()) {
    srs_freep(args);
    
    // see: https://github.com/ossrs/srs/issues/186
    // the args maybe any amf0, for instance, a string. we should drop if not object.
    RtmpAmf0Any* any = NULL;
    if ((err = RtmpAmf0Any::discovery(stream, &any)) != srs_success) {
      return srs_error_wrap(err, "args");
    }
    srs_assert(any);
    
    // read the instance
    if ((err = any->read(stream)) != srs_success) {
      srs_freep(any);
      return srs_error_wrap(err, "args");
    }
    
    // drop when not an AMF0 object.
    if (!any->is_object()) {
      MLOG_CWARN("drop the args, see: '4.1.1. connect', marker=%#x", 
          (uint8_t)any->marker);
      srs_freep(any);
    } else {
      args = any->to_object();
    }
  }
  
  return err;
}

int RtmpConnectAppPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpConnectAppPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpConnectAppPacket::get_size() {
  int size = 0;
  
  size += RtmpAmf0Size::str(command_name);
  size += RtmpAmf0Size::number();
  size += RtmpAmf0Size::object(command_object);
  if (args) {
      size += RtmpAmf0Size::object(args);
  }
  
  return size;
}

srs_error_t RtmpConnectAppPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = command_object->write(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if (args && (err = args->write(stream)) != srs_success) {
      return srs_error_wrap(err, "args");
  }
  
  return err;
}

//RtmpConnectAppResPacket
RtmpConnectAppResPacket::RtmpConnectAppResPacket() {
  command_name = RTMP_AMF0_COMMAND_RESULT;
  transaction_id = 1;
  props = RtmpAmf0Any::object();
  info = RtmpAmf0Any::object();
}

RtmpConnectAppResPacket::~RtmpConnectAppResPacket() {
  srs_freep(props);
  srs_freep(info);
}

srs_error_t RtmpConnectAppResPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
    return srs_error_new(ERROR_RTMP_AMF0_DECODE, 
        "command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  // some client donot send id=1.0, so we only warn user if not match.
  if (transaction_id != 1.0) {
    MLOG_CWARN("invalid transaction_id=%.2f", transaction_id);
  }
  
  // for RED5(1.0.6), the props is NULL, we must ignore it.
  // @see https://github.com/ossrs/srs/issues/418
  if (!stream->empty()) {
    RtmpAmf0Any* p = NULL;
    if ((err = srs_amf0_read_any(stream, &p)) != srs_success) {
      return srs_error_wrap(err, "args");
    }
    
    // ignore when props is not amf0 object.
    if (!p->is_object()) {
      MLOG_CWARN("ignore connect response props marker=%#x.", 
          (uint8_t)p->marker);
      srs_freep(p);
    } else {
      srs_freep(props);
      props = p->to_object();
    }
  }
  
  if ((err = info->read(stream)) != srs_success) {
    return srs_error_wrap(err, "args");
  }
  
  return err;
}

int RtmpConnectAppResPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpConnectAppResPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpConnectAppResPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::object(props) + RtmpAmf0Size::object(info);
}

srs_error_t RtmpConnectAppResPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = props->write(stream)) != srs_success) {
      return srs_error_wrap(err, "props");
  }
  
  if ((err = info->write(stream)) != srs_success) {
      return srs_error_wrap(err, "info");
  }
  
  return err;
}

//RtmpCallPacket
RtmpCallPacket::RtmpCallPacket() {
  command_name = "";
  transaction_id = 0;
  command_object = NULL;
  arguments = NULL;
}

RtmpCallPacket::~RtmpCallPacket() {
  srs_freep(command_object);
  srs_freep(arguments);
}

srs_error_t RtmpCallPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty()) {
      return srs_error_new(ERROR_RTMP_AMF0_DECODE, "empty command_name");
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  srs_freep(command_object);
  if ((err = RtmpAmf0Any::discovery(stream, &command_object)) != srs_success) {
      return srs_error_wrap(err, "discovery command_object");
  }
  if ((err = command_object->read(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if (!stream->empty()) {
      srs_freep(arguments);
      if ((err = RtmpAmf0Any::discovery(stream, &arguments)) != srs_success) {
          return srs_error_wrap(err, "discovery args");
      }
      if ((err = arguments->read(stream)) != srs_success) {
          return srs_error_wrap(err, "read args");
      }
  }
  
  return err;
}

int RtmpCallPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpCallPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpCallPacket::get_size() {
  int size = 0;
  
  size += RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number();
  
  if (command_object) {
      size += command_object->total_size();
  }
  
  if (arguments) {
      size += arguments->total_size();
  }
  
  return size;
}

srs_error_t RtmpCallPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if (command_object && (err = command_object->write(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if (arguments && (err = arguments->write(stream)) != srs_success) {
      return srs_error_wrap(err, "args");
  }
  
  return err;
}

RtmpCallResPacket::RtmpCallResPacket(double _transaction_id) {
  command_name = RTMP_AMF0_COMMAND_RESULT;
  transaction_id = _transaction_id;
  command_object = NULL;
  response = NULL;
}

RtmpCallResPacket::~RtmpCallResPacket() {
  srs_freep(command_object);
  srs_freep(response);
}

int RtmpCallResPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpCallResPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpCallResPacket::get_size() {
  int size = 0;
  
  size += RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number();
  
  if (command_object) {
      size += command_object->total_size();
  }
  
  if (response) {
      size += response->total_size();
  }
  
  return size;
}

srs_error_t RtmpCallResPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if (command_object && (err = command_object->write(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if (response && (err = response->write(stream)) != srs_success) {
      return srs_error_wrap(err, "response");
  }
  
  return err;
}

//RtmpCreateStreamPacket
RtmpCreateStreamPacket::RtmpCreateStreamPacket() {
  command_name = RTMP_AMF0_COMMAND_CREATE_STREAM;
  transaction_id = 2;
  command_object = RtmpAmf0Any::null();
}

RtmpCreateStreamPacket::~RtmpCreateStreamPacket() {
  srs_freep(command_object);
}

srs_error_t RtmpCreateStreamPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_CREATE_STREAM) {
    return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  return err;
}

int RtmpCreateStreamPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpCreateStreamPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpCreateStreamPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
      + RtmpAmf0Size::null();
}

srs_error_t RtmpCreateStreamPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  return err;
}

RtmpCreateStreamResPacket::RtmpCreateStreamResPacket(
    double _transaction_id, double _stream_id) {
  command_name = RTMP_AMF0_COMMAND_RESULT;
  transaction_id = _transaction_id;
  command_object = RtmpAmf0Any::null();
  stream_id = _stream_id;
}

RtmpCreateStreamResPacket::~RtmpCreateStreamResPacket() {
  srs_freep(command_object);
}

srs_error_t RtmpCreateStreamResPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
      return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_read_number(stream, stream_id)) != srs_success) {
      return srs_error_wrap(err, "stream_id");
  }
  
  return err;
}

int RtmpCreateStreamResPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpCreateStreamResPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpCreateStreamResPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
    + RtmpAmf0Size::null() + RtmpAmf0Size::number();
}

srs_error_t RtmpCreateStreamResPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_write_number(stream, stream_id)) != srs_success) {
      return srs_error_wrap(err, "stream_id");
  }
  
  return err;
}

RtmpCloseStreamPacket::RtmpCloseStreamPacket() {
  command_name = RTMP_AMF0_COMMAND_CLOSE_STREAM;
  transaction_id = 0;
  command_object = RtmpAmf0Any::null();
}

RtmpCloseStreamPacket::~RtmpCloseStreamPacket() {
  srs_freep(command_object);
}

srs_error_t RtmpCloseStreamPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  return err;
}

//RtmpFMLEStartPacket
RtmpFMLEStartPacket::RtmpFMLEStartPacket() {
  command_name = RTMP_AMF0_COMMAND_RELEASE_STREAM;
  transaction_id = 0;
  command_object = RtmpAmf0Any::null();
}

RtmpFMLEStartPacket::~RtmpFMLEStartPacket() {
  srs_freep(command_object);
}

srs_error_t RtmpFMLEStartPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  
  bool invalid_command_name = 
      (command_name != RTMP_AMF0_COMMAND_RELEASE_STREAM && 
      command_name != RTMP_AMF0_COMMAND_FC_PUBLISH && 
      command_name != RTMP_AMF0_COMMAND_UNPUBLISH);
  if (command_name.empty() || invalid_command_name) {
    return srs_error_new(ERROR_RTMP_AMF0_DECODE, 
        "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_read_string(stream, stream_name)) != srs_success) {
    return srs_error_wrap(err, "stream_name");
  }
  
  return err;
}

int RtmpFMLEStartPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpFMLEStartPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpFMLEStartPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
      + RtmpAmf0Size::null() + RtmpAmf0Size::str(stream_name);
}

srs_error_t RtmpFMLEStartPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_write_string(stream, stream_name)) != srs_success) {
    return srs_error_wrap(err, "stream_name");
  }
  
  return err;
}

RtmpFMLEStartPacket* RtmpFMLEStartPacket::create_release_stream(
    const std::string& stream) {
  RtmpFMLEStartPacket* pkt = new RtmpFMLEStartPacket();
  
  pkt->command_name = RTMP_AMF0_COMMAND_RELEASE_STREAM;
  pkt->transaction_id = 2;
  pkt->stream_name = stream;
  
  return pkt;
}

RtmpFMLEStartPacket* RtmpFMLEStartPacket::create_FC_publish(
      const std::string& stream) {
  RtmpFMLEStartPacket* pkt = new RtmpFMLEStartPacket();
  
  pkt->command_name = RTMP_AMF0_COMMAND_FC_PUBLISH;
  pkt->transaction_id = 3;
  pkt->stream_name = stream;
  
  return pkt;
}

RtmpFMLEStartResPacket::RtmpFMLEStartResPacket(double _transaction_id) {
  command_name = RTMP_AMF0_COMMAND_RESULT;
  transaction_id = _transaction_id;
  command_object = RtmpAmf0Any::null();
  args = RtmpAmf0Any::undefined();
}

RtmpFMLEStartResPacket::~RtmpFMLEStartResPacket() {
  srs_freep(command_object);
  srs_freep(args);
}

srs_error_t RtmpFMLEStartResPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_RESULT) {
      return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_read_undefined(stream)) != srs_success) {
      return srs_error_wrap(err, "stream_id");
  }
  
  return err;
}

int RtmpFMLEStartResPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpFMLEStartResPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpFMLEStartResPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
    + RtmpAmf0Size::null() + RtmpAmf0Size::undefined();
}

srs_error_t RtmpFMLEStartResPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_write_undefined(stream)) != srs_success) {
      return srs_error_wrap(err, "args");
  }
  
  return err;
}

RtmpPublishPacket::RtmpPublishPacket() {
  command_name = RTMP_AMF0_COMMAND_PUBLISH;
  transaction_id = 0;
  command_object = RtmpAmf0Any::null();
  type = "live";
}

RtmpPublishPacket::~RtmpPublishPacket() {
  srs_freep(command_object);
}

srs_error_t RtmpPublishPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PUBLISH) {
      return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_read_string(stream, stream_name)) != srs_success) {
      return srs_error_wrap(err, "stream_name");
  }
  
  if (!stream->empty() && (err = srs_amf0_read_string(stream, type)) != srs_success) {
      return srs_error_wrap(err, "publish type");
  }
  
  return err;
}

int RtmpPublishPacket::get_prefer_cid()
{
  return RTMP_CID_OverStream;
}

int RtmpPublishPacket::get_message_type()
{
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpPublishPacket::get_size()
{
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::null() + RtmpAmf0Size::str(stream_name)
  + RtmpAmf0Size::str(type);
}

srs_error_t RtmpPublishPacket::encode_packet(SrsBuffer* stream)
{
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_write_string(stream, stream_name)) != srs_success) {
      return srs_error_wrap(err, "stream_name");
  }
  
  if ((err = srs_amf0_write_string(stream, type)) != srs_success) {
      return srs_error_wrap(err, "type");
  }
  
  return err;
}

RtmpPausePacket::RtmpPausePacket() {
  command_name = RTMP_AMF0_COMMAND_PAUSE;
  transaction_id = 0;
  command_object = RtmpAmf0Any::null();
  
  time_ms = 0;
  is_pause = true;
}

RtmpPausePacket::~RtmpPausePacket() {
  srs_freep(command_object);
}

srs_error_t RtmpPausePacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PAUSE) {
    return srs_error_new(ERROR_RTMP_AMF0_DECODE, 
        "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_read_boolean(stream, is_pause)) != srs_success) {
    return srs_error_wrap(err, "is_pause");
  }
  
  if ((err = srs_amf0_read_number(stream, time_ms)) != srs_success) {
    return srs_error_wrap(err, "time");
  }
  
  return err;
}

RtmpPlayPacket::RtmpPlayPacket() {
  command_name = RTMP_AMF0_COMMAND_PLAY;
  transaction_id = 0;
  command_object = RtmpAmf0Any::null();
  
  start = -2;
  duration = -1;
  reset = true;
}

RtmpPlayPacket::~RtmpPlayPacket() {
  srs_freep(command_object);
}

srs_error_t RtmpPlayPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
    return srs_error_wrap(err, "command_name");
  }
  if (command_name.empty() || command_name != RTMP_AMF0_COMMAND_PLAY) {
    return srs_error_new(ERROR_RTMP_AMF0_DECODE, 
        "invalid command_name=%s", command_name.c_str());
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
    return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
    return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_read_string(stream, stream_name)) != srs_success) {
    return srs_error_wrap(err, "stream_name");
  }
  
  if (!stream->empty() && 
      (err = srs_amf0_read_number(stream, start)) != srs_success) {
    return srs_error_wrap(err, "start");
  }
  if (!stream->empty() && (err = srs_amf0_read_number(stream, duration)) != srs_success) {
    return srs_error_wrap(err, "duration");
  }
  
  if (stream->empty()) {
    return err;
  }
  
  RtmpAmf0Any* reset_value = NULL;
  if ((err = srs_amf0_read_any(stream, &reset_value)) != srs_success) {
    return srs_error_wrap(err, "reset");
  }
  std::unique_ptr<RtmpAmf0Any> obj_guard(reset_value);
  
  if (reset_value) {
    // check if the value is bool or number
    // An optional Boolean value or number that specifies whether
    // to flush any previous playlist
    if (reset_value->is_boolean()) {
        reset = reset_value->to_boolean();
    } else if (reset_value->is_number()) {
        reset = (reset_value->to_number() != 0);
    } else {
        return srs_error_new(ERROR_RTMP_AMF0_DECODE, "invalid marker=%#x", (uint8_t)reset_value->marker);
    }
  }
  
  return err;
}

int RtmpPlayPacket::get_prefer_cid() {
  return RTMP_CID_OverStream;
}

int RtmpPlayPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpPlayPacket::get_size() {
  int size = RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::null() + RtmpAmf0Size::str(stream_name);
  
  if (start != -2 || duration != -1 || !reset) {
      size += RtmpAmf0Size::number();
  }
  
  if (duration != -1 || !reset) {
      size += RtmpAmf0Size::number();
  }
  
  if (!reset) {
      size += RtmpAmf0Size::boolean();
  }
  
  return size;
}

srs_error_t RtmpPlayPacket::encode_packet(SrsBuffer* stream)
{
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = srs_amf0_write_string(stream, stream_name)) != srs_success) {
      return srs_error_wrap(err, "stream_name");
  }
  
  if ((start != -2 || duration != -1 || !reset) && (err = srs_amf0_write_number(stream, start)) != srs_success) {
      return srs_error_wrap(err, "start");
  }
  
  if ((duration != -1 || !reset) && (err = srs_amf0_write_number(stream, duration)) != srs_success) {
      return srs_error_wrap(err, "duration");
  }
  
  if (!reset && (err = srs_amf0_write_boolean(stream, reset)) != srs_success) {
      return srs_error_wrap(err, "reset");
  }
  
  return err;
}

RtmpPlayResPacket::RtmpPlayResPacket() {
  command_name = RTMP_AMF0_COMMAND_RESULT;
  transaction_id = 0;
  command_object = RtmpAmf0Any::null();
  desc = RtmpAmf0Any::object();
}

RtmpPlayResPacket::~RtmpPlayResPacket() {
  srs_freep(command_object);
  srs_freep(desc);
}

int RtmpPlayResPacket::get_prefer_cid() {
  return RTMP_CID_OverStream;
}

int RtmpPlayResPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpPlayResPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::null() + RtmpAmf0Size::object(desc);
}

srs_error_t RtmpPlayResPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  if ((err = desc->write(stream)) != srs_success) {
      return srs_error_wrap(err, "desc");
  }
  
  return err;
}

RtmpOnBWDonePacket::RtmpOnBWDonePacket() {
  command_name = RTMP_AMF0_COMMAND_ON_BW_DONE;
  transaction_id = 0;
  args = RtmpAmf0Any::null();
}

RtmpOnBWDonePacket::~RtmpOnBWDonePacket() {
  srs_freep(args);
}

int RtmpOnBWDonePacket::get_prefer_cid() {
  return RTMP_CID_OverConnection;
}

int RtmpOnBWDonePacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpOnBWDonePacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::null();
}

srs_error_t RtmpOnBWDonePacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "args");
  }
  
  return err;
}

RtmpOnStatusCallPacket::RtmpOnStatusCallPacket()
{
  command_name = RTMP_AMF0_COMMAND_ON_STATUS;
  transaction_id = 0;
  args = RtmpAmf0Any::null();
  data = RtmpAmf0Any::object();
}

RtmpOnStatusCallPacket::~RtmpOnStatusCallPacket() {
  srs_freep(args);
  srs_freep(data);
}

int RtmpOnStatusCallPacket::get_prefer_cid() {
  return RTMP_CID_OverStream;
}

int RtmpOnStatusCallPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpOnStatusCallPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::null() + RtmpAmf0Size::object(data);
}

srs_error_t RtmpOnStatusCallPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "args");
  }
  
  if ((err = data->write(stream)) != srs_success) {
      return srs_error_wrap(err, "data");
  }
  
  return err;
}

RtmpBandwidthPacket::RtmpBandwidthPacket() {
  command_name = RTMP_AMF0_COMMAND_ON_STATUS;
  transaction_id = 0;
  args = RtmpAmf0Any::null();
  data = RtmpAmf0Any::object();
}

RtmpBandwidthPacket::~RtmpBandwidthPacket() {
  srs_freep(args);
  srs_freep(data);
}

srs_error_t RtmpBandwidthPacket::decode(SrsBuffer *stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_read_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_read_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_read_null(stream)) != srs_success) {
      return srs_error_wrap(err, "command_object");
  }
  
  // @remark, for bandwidth test, ignore the data field.
  // only decode the stop-play, start-publish and finish packet.
  if (is_stop_play() || is_start_publish() || is_finish()) {
      if ((err = data->read(stream)) != srs_success) {
          return srs_error_wrap(err, "command_object");
      }
  }
  
  return err;
}

int RtmpBandwidthPacket::get_prefer_cid() {
  return RTMP_CID_OverStream;
}

int RtmpBandwidthPacket::get_message_type() {
  return RTMP_MSG_AMF0CommandMessage;
}

int RtmpBandwidthPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::number()
  + RtmpAmf0Size::null() + RtmpAmf0Size::object(data);
}

srs_error_t RtmpBandwidthPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_number(stream, transaction_id)) != srs_success) {
      return srs_error_wrap(err, "transaction_id");
  }
  
  if ((err = srs_amf0_write_null(stream)) != srs_success) {
      return srs_error_wrap(err, "args");
  }
  
  if ((err = data->write(stream)) != srs_success) {
      return srs_error_wrap(err, "data");
  }
  
  return err;
}

bool RtmpBandwidthPacket::is_start_play() {
  return command_name == SRS_BW_CHECK_START_PLAY;
}

bool RtmpBandwidthPacket::is_starting_play() {
  return command_name == SRS_BW_CHECK_STARTING_PLAY;
}

bool RtmpBandwidthPacket::is_stop_play() {
  return command_name == SRS_BW_CHECK_STOP_PLAY;
}

bool RtmpBandwidthPacket::is_stopped_play() {
  return command_name == SRS_BW_CHECK_STOPPED_PLAY;
}

bool RtmpBandwidthPacket::is_start_publish() {
  return command_name == SRS_BW_CHECK_START_PUBLISH;
}

bool RtmpBandwidthPacket::is_starting_publish() {
  return command_name == SRS_BW_CHECK_STARTING_PUBLISH;
}

bool RtmpBandwidthPacket::is_stop_publish() {
  return command_name == SRS_BW_CHECK_STOP_PUBLISH;
}

bool RtmpBandwidthPacket::is_stopped_publish() {
  return command_name == SRS_BW_CHECK_STOPPED_PUBLISH;
}

bool RtmpBandwidthPacket::is_finish() {
  return command_name == SRS_BW_CHECK_FINISHED;
}

bool RtmpBandwidthPacket::is_final() {
  return command_name == SRS_BW_CHECK_FINAL;
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_start_play() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_START_PLAY);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_starting_play() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_STARTING_PLAY);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_playing() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_PLAYING);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_stop_play() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_STOP_PLAY);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_stopped_play() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_STOPPED_PLAY);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_start_publish() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_START_PUBLISH);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_starting_publish() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_STARTING_PUBLISH);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_publishing() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_PUBLISHING);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_stop_publish() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_STOP_PUBLISH);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_stopped_publish() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_STOPPED_PUBLISH);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_finish() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_FINISHED);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::create_final() {
  RtmpBandwidthPacket* pkt = new RtmpBandwidthPacket();
  return pkt->set_command(SRS_BW_CHECK_FINAL);
}

RtmpBandwidthPacket* RtmpBandwidthPacket::set_command(
    const std::string& command) {
  command_name = command;
  return this;
}

RtmpOnStatusDataPacket::RtmpOnStatusDataPacket() {
  command_name = RTMP_AMF0_COMMAND_ON_STATUS;
  data = RtmpAmf0Any::object();
}

RtmpOnStatusDataPacket::~RtmpOnStatusDataPacket() {
  srs_freep(data);
}

int RtmpOnStatusDataPacket::get_prefer_cid() {
  return RTMP_CID_OverStream;
}

int RtmpOnStatusDataPacket::get_message_type() {
  return RTMP_MSG_AMF0DataMessage;
}

int RtmpOnStatusDataPacket::get_size() {
  return RtmpAmf0Size::str(command_name) + RtmpAmf0Size::object(data);
}

srs_error_t RtmpOnStatusDataPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = data->write(stream)) != srs_success) {
      return srs_error_wrap(err, "data");
  }
  
  return err;
}

RtmpSampleAccessPacket::RtmpSampleAccessPacket() {
  command_name = RTMP_AMF0_DATA_SAMPLE_ACCESS;
  video_sample_access = false;
  audio_sample_access = false;
}

int RtmpSampleAccessPacket::get_prefer_cid() {
  return RTMP_CID_OverStream;
}

int RtmpSampleAccessPacket::get_message_type() {
  return RTMP_MSG_AMF0DataMessage;
}

int RtmpSampleAccessPacket::get_size() {
  return RtmpAmf0Size::str(command_name)
  + RtmpAmf0Size::boolean() + RtmpAmf0Size::boolean();
}

srs_error_t RtmpSampleAccessPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, command_name)) != srs_success) {
      return srs_error_wrap(err, "command_name");
  }
  
  if ((err = srs_amf0_write_boolean(stream, video_sample_access)) != srs_success) {
      return srs_error_wrap(err, "video sample access");
  }
  
  if ((err = srs_amf0_write_boolean(stream, audio_sample_access)) != srs_success) {
      return srs_error_wrap(err, "audio sample access");
  }
  
  return err;
}

RtmpOnMetaDataPacket::RtmpOnMetaDataPacket() {
  name = SRS_CONSTS_RTMP_ON_METADATA;
  metadata = RtmpAmf0Any::object();
}

RtmpOnMetaDataPacket::~RtmpOnMetaDataPacket() {
  srs_freep(metadata);
}

srs_error_t RtmpOnMetaDataPacket::decode(SrsBuffer* stream) {
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
  RtmpAmf0Any* any = NULL;
  if ((err = srs_amf0_read_any(stream, &any)) != srs_success) {
    return srs_error_wrap(err, "metadata");
  }
  
  srs_assert(any);
  if (any->is_object()) {
    srs_freep(metadata);
    metadata = any->to_object();
    return err;
  }
  
  std::unique_ptr<RtmpAmf0Any> guard_any(any);
  
  if (any->is_ecma_array()) {
    RtmpAmf0EcmaArray* arr = any->to_ecma_array();
    
    // if ecma array, copy to object.
    for (int i = 0; i < arr->count(); i++) {
      metadata->set(arr->key_at(i), arr->value_at(i)->copy());
    }
  }
  
  return err;
}

int RtmpOnMetaDataPacket::get_prefer_cid() {
  return RTMP_CID_OverConnection2;
}

int RtmpOnMetaDataPacket::get_message_type() {
  return RTMP_MSG_AMF0DataMessage;
}

int RtmpOnMetaDataPacket::get_size() {
  return RtmpAmf0Size::str(name) + RtmpAmf0Size::object(metadata);
}

srs_error_t RtmpOnMetaDataPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if ((err = srs_amf0_write_string(stream, name)) != srs_success) {
    return srs_error_wrap(err, "name");
  }
  
  if ((err = metadata->write(stream)) != srs_success) {
    return srs_error_wrap(err, "metadata");
  }
  
  return err;
}

//RtmpSetWindowAckSizePacket
RtmpSetWindowAckSizePacket::RtmpSetWindowAckSizePacket() 
    : ackowledgement_window_size(0) {
}

srs_error_t RtmpSetWindowAckSizePacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(4)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, 
        "requires 4 only %d bytes", stream->left());
  }
  
  ackowledgement_window_size = stream->read_4bytes();
  
  return err;
}

int RtmpSetWindowAckSizePacket::get_prefer_cid() {
  return RTMP_CID_ProtocolControl;
}

int RtmpSetWindowAckSizePacket::get_message_type() {
  return RTMP_MSG_WindowAcknowledgementSize;
}

int RtmpSetWindowAckSizePacket::get_size() {
  return 4;
}

srs_error_t RtmpSetWindowAckSizePacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(4)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, 
        "requires 4 only %d bytes", stream->left());
  }
  
  stream->write_4bytes(ackowledgement_window_size);
  
  return err;
}

//RtmpAcknowledgementPacket
RtmpAcknowledgementPacket::RtmpAcknowledgementPacket() {
  sequence_number = 0;
}

srs_error_t RtmpAcknowledgementPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(4)) {
      return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, "requires 4 only %d bytes", stream->left());
  }
  
  sequence_number = (uint32_t)stream->read_4bytes();
  
  return err;
}

int RtmpAcknowledgementPacket::get_prefer_cid() {
  return RTMP_CID_ProtocolControl;
}

int RtmpAcknowledgementPacket::get_message_type() {
  return RTMP_MSG_Acknowledgement;
}

int RtmpAcknowledgementPacket::get_size() {
  return 4;
}

srs_error_t RtmpAcknowledgementPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(4)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, 
        "requires 4 only %d bytes", stream->left());
  }
  
  stream->write_4bytes(sequence_number);
  
  return err;
}

//RtmpSetChunkSizePacket
RtmpSetChunkSizePacket::RtmpSetChunkSizePacket() 
  : chunk_size(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE) {
}

srs_error_t RtmpSetChunkSizePacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(4)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, 
        "requires 4 only %d bytes", stream->left());
  }
  
  chunk_size = stream->read_4bytes();
  
  return err;
}

int RtmpSetChunkSizePacket::get_prefer_cid() {
  return RTMP_CID_ProtocolControl;
}

int RtmpSetChunkSizePacket::get_message_type() {
  return RTMP_MSG_SetChunkSize;
}

int RtmpSetChunkSizePacket::get_size() {
  return 4;
}

srs_error_t RtmpSetChunkSizePacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(4)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, 
        "requires 4 only %d bytes", stream->left());
  }
  
  stream->write_4bytes(chunk_size);
  
  return err;
}

//RtmpSetPeerBandwidthPacket
RtmpSetPeerBandwidthPacket::RtmpSetPeerBandwidthPacket() {
  bandwidth = 0;
  type = RtmpPeerBandwidthDynamic;
}

int RtmpSetPeerBandwidthPacket::get_prefer_cid() {
  return RTMP_CID_ProtocolControl;
}

int RtmpSetPeerBandwidthPacket::get_message_type() {
  return RTMP_MSG_SetPeerBandwidth;
}

int RtmpSetPeerBandwidthPacket::get_size() {
  return 5;
}

srs_error_t RtmpSetPeerBandwidthPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  if (!stream->require(5)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, "requires 5 only %d bytes", stream->left());
  }
  
  stream->write_4bytes(bandwidth);
  stream->write_1bytes(type);
  
  return err;
}

//RtmpUserControlPacket
RtmpUserControlPacket::RtmpUserControlPacket() {
  event_type = 0;
  event_data = 0;
  extra_data = 0;
}

srs_error_t RtmpUserControlPacket::decode(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(2)) {
    return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, 
        "requires 2 only %d bytes", stream->left());
  }
  
  event_type = stream->read_2bytes();
  
  if (event_type == RtmpPCUCFmsEvent0) {
    if (!stream->require(1)) {
      return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, 
          "requires 1 only %d bytes", stream->left());
    }
    event_data = stream->read_1bytes();
  } else {
    if (!stream->require(4)) {
      return srs_error_new(ERROR_RTMP_MESSAGE_DECODE, 
          "requires 4 only %d bytes", stream->left());
    }
    event_data = stream->read_4bytes();
  }
  
  if (event_type == SrcPCUCSetBufferLength) {
    if (!stream->require(4)) {
      return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, 
          "requires 4 only %d bytes", stream->left());
    }
    extra_data = stream->read_4bytes();
  }
  
  return err;
}

int RtmpUserControlPacket::get_prefer_cid() {
  return RTMP_CID_ProtocolControl;
}

int RtmpUserControlPacket::get_message_type() {
  return RTMP_MSG_UserControlMessage;
}

int RtmpUserControlPacket::get_size() {
  int size = 2;
  
  if (event_type == RtmpPCUCFmsEvent0) {
    size += 1;
  } else {
    size += 4;
  }
  
  if (event_type == SrcPCUCSetBufferLength) {
    size += 4;
  }
  
  return size;
}

srs_error_t RtmpUserControlPacket::encode_packet(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  if (!stream->require(get_size())) {
    return srs_error_new(ERROR_RTMP_MESSAGE_ENCODE, 
        "requires %d only %d bytes", get_size(), stream->left());
  }
  
  stream->write_2bytes(event_type);
  
  if (event_type == RtmpPCUCFmsEvent0) {
    stream->write_1bytes(event_data);
  } else {
    stream->write_4bytes(event_data);
  }
  
  // when event type is set buffer length,
  // write the extra buffer length.
  if (event_type == SrcPCUCSetBufferLength) {
    stream->write_4bytes(extra_data);
  }
  
  return err;
}

} //namespace ma
