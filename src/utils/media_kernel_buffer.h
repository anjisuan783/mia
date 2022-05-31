//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef SRS_KERNEL_BUFFER_HPP
#define SRS_KERNEL_BUFFER_HPP


#include <sys/types.h>
#include <string>

#include "common/media_kernel_error.h"

namespace ma {

class SrsBuffer;

// Encoder.
class ISrsEncoder {
 public:
  ISrsEncoder();
  virtual ~ISrsEncoder();
  /**
   * get the number of bytes to code to.
   */
  virtual uint64_t nb_bytes() = 0;
  /**
   * encode object to bytes in SrsBuffer.
   */
  virtual srs_error_t encode(SrsBuffer* buf) = 0;
};

/**
 * the srs codec, to code and decode object with bytes:
 *      code: to encode/serialize object to bytes in buffer,
 *      decode: to decode/deserialize object from bytes in buffer.
 * we use SrsBuffer as bytes helper utility,
 * for example, to code:
 *      ISrsCodec* obj = ...
 *      char* bytes = new char[obj->size()];
 *
 *      SrsBuffer* buf = new SrsBuffer();
 *      buf->initialize(bytes, obj->size())
 *
 *      obj->encode(buf);
 * for example, to decode:
 *      int nb_bytes = ...
 *      char* bytes = ...
 *
 *      SrsBuffer* buf = new Srsbuffer();
 *      buf->initialize(bytes, nb_bytes);
 *
 *      ISrsCodec* obj = ...
 *      obj->decode(buf);
 * @remark protocol or amf0 or json should implements this interface.
 */
// TODO: FIXME: protocol, amf0, json should implements it.
class ISrsCodec : public ISrsEncoder {
 public:
  ISrsCodec();
  virtual ~ISrsCodec();
 public:
  // Get the number of bytes to code to.
  virtual uint64_t nb_bytes() = 0;
  // Encode object to bytes in SrsBuffer.
  virtual srs_error_t encode(SrsBuffer* buf) = 0;

  // Decode object from bytes in SrsBuffer.
  virtual srs_error_t decode(SrsBuffer* buf) = 0;
};

class MessageChain;
/**
 * bytes utility, used to:
 * convert basic types to bytes,
 * build basic types from bytes.
 * @remark the buffer never mange the bytes, user must manage it.
 */
class SrsBuffer {
 public:
  SrsBuffer() = default;

  // Create buffer with data b and size nn.
  // @remark User must free the data b.
  SrsBuffer(char* b, int nn);
  //SrsBuffer(MessageChain&);
  virtual ~SrsBuffer() = default;

  // Copy the object, keep position of buffer.
  //SrsBuffer* copy();
  // Get the data and head of buffer.
  //      current-bytes = head() = data() + pos()
  char* data();
  char* head();
  // Get the total size of buffer.
  //      left-bytes = size() - pos()
  virtual int size();
  void set_size(int v);
  // Get the current buffer position.
  int pos();
  // Left bytes in buffer, total size() minus the current pos().
  virtual int left();
  // Whether buffer is empty.
  virtual bool empty();
  // Whether buffer is able to supply required size of bytes.
  // @remark User should check buffer by require then do read/write.
  // @remark Assert the required_size is not negative.
  virtual bool require(int required_size);
  // Skip some size.
  // @param size can be any value. positive to forward; negative to backward.
  // @remark to skip(pos()) to reset buffer.
  // @remark assert initialized, the data() not NULL.
  virtual void skip(int size);
  // Read 1bytes char from buffer.
  virtual int8_t read_1bytes();
  // Read 2bytes int from buffer.
  virtual int16_t read_2bytes();
  virtual int16_t read_le2bytes();
  // Read 3bytes int from buffer.
  virtual int32_t read_3bytes();
  virtual int32_t read_le3bytes();
  // Read 4bytes int from buffer.
  virtual int32_t read_4bytes();
  virtual int32_t read_le4bytes();
  // Read 8bytes int from buffer.
  virtual int64_t read_8bytes();
  virtual int64_t read_le8bytes();
  // Read string from buffer, length specifies by param len.
  virtual std::string read_string(int len);
  // Read bytes from buffer, length specifies by param len.
  virtual void read_bytes(char* data, int size);
  // Write 1bytes char to buffer.
  virtual void write_1bytes(int8_t value);
  // Write 2bytes int to buffer.
  virtual void write_2bytes(int16_t value);
  virtual void write_le2bytes(int16_t value);
  // Write 4bytes int to buffer.
  virtual void write_4bytes(int32_t value);
  virtual void write_le4bytes(int32_t value);
  // Write 3bytes int to buffer.
  virtual void write_3bytes(int32_t value);
  virtual void write_le3bytes(int32_t value);
  // Write 8bytes int to buffer.
  virtual void write_8bytes(int64_t value);
  virtual void write_le8bytes(int64_t value);
  // Write string to buffer
  virtual void write_string(const std::string& value);
  // Write bytes to buffer
  virtual void write_bytes(char* data, int size);
 private:
  // current position at bytes.
  char* p = nullptr;
  // the bytes data for buffer to read or write.
  char* bytes = nullptr;
  // the total number of bytes.
  int nb_bytes = 0;
};

/**
 * the bit buffer, base on SrsBuffer,
 * for exmaple, the h.264 avc buffer is bit buffer.
 */
class SrsBitBuffer {
private:
  int8_t cb;
  uint8_t cb_left;
  SrsBuffer* stream;
public:
  SrsBitBuffer(SrsBuffer* b);
  ~SrsBitBuffer();
  bool empty();
  int8_t read_bit();
};

}
#endif
