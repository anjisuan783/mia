//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#if 0

#ifndef __MEDIA_PROTOCOL_STREAM_H__
#define __MEDIA_PROTOCOL_STREAM_H__

#include "common/media_kernel_error.h"

namespace ma {

#ifdef SRS_PERF_MERGED_READ
/**
 * to improve read performance, merge some packets then read,
 * when it on and read small bytes, we sleep to wait more data.,
 * that is, we merge some data to read together.
 * @see https://github.com/ossrs/srs/issues/241
 */
class IMergeReadHandler {
 public:
  IMergeReadHandler() = default;
  virtual ~IMergeReadHandler() = default;

 /**
   * when read from channel, notice the merge handler to sleep for
   * some small bytes.
   * @remark, it only for server-side, client srs-librtmp just ignore.
   */
  virtual void on_read(ssize_t nread) = 0;
};
#endif

/**
 * the buffer provices bytes cache for protocol. generally,
 * protocol recv data from socket, put into buffer, decode to RTMP message.
 * Usage:
 *       ISrsReader* r = ......;
 *       SrsFastStream* fb = ......;
 *       fb->grow(r, 1024);
 *       char* header = fb->read_slice(100);
 *       char* payload = fb->read_payload(924);
 */
// TODO: FIXME: add utest for it.
class SrsFastStream final {
 public:
  // If buffer is 0, use default size.
  SrsFastStream(int size=0);
  ~SrsFastStream();
 public:
  /**
   * get the size of current bytes in buffer.
   */
  int size();
  /**
   * get the current bytes in buffer.
   * @remark user should use read_slice() if possible,
   *       the bytes() is used to test bytes, for example, to detect the bytes schema.
   */
  char* bytes();
  /**
   * create buffer with specifeid size.
   * @param buffer the size of buffer. ignore when smaller than SRS_MAX_SOCKET_BUFFER.
   * @remark when MR(SRS_PERF_MERGED_READ) disabled, always set to 8K.
   * @remark when buffer changed, the previous ptr maybe invalid.
   * @see https://github.com/ossrs/srs/issues/241
   */
  void set_buffer(int buffer_size);
 public:
  /**
   * read 1byte from buffer, move to next bytes.
   * @remark assert buffer already grow(1).
   */
  char read_1byte();
  /**
   * read a slice in size bytes, move to next bytes.
   * user can use this char* ptr directly, and should never free it.
   * @remark user can use the returned ptr util grow(size),
   *       for the ptr returned maybe invalid after grow(x).
   */
  char* read_slice(int size);
  /**
   * skip some bytes in buffer.
   * @param size the bytes to skip. positive to next; negative to previous.
   * @remark assert buffer already grow(size).
   * @remark always use read_slice to consume bytes, which will reset for EOF.
   *       while skip never consume bytes.
   */
  void skip(int size);
 public:
  /**
   * grow buffer to atleast required size, loop to read from skt to fill.
   * @param reader, read more bytes from reader to fill the buffer to required size.
   * @param required_size, loop to fill to ensure buffer size to required.
   * @return an int error code, error if required_size negative.
   * @remark, we actually maybe read more than required_size, maybe 4k for example.
   */
  srs_error_t grow(ISrsReader* reader, int required_size);
 public:
#ifdef SRS_PERF_MERGED_READ
  /**
   * to improve read performance, merge some packets then read,
   * when it on and read small bytes, we sleep to wait more data.,
   * that is, we merge some data to read together.
   * @param v true to ename merged read.
   * @param handler the handler when merge read is enabled.
   * @see https://github.com/ossrs/srs/issues/241
   * @remark the merged read is optional, ignore if not specifies.
   */
  void set_merge_read(bool v, IMergeReadHandler* handler);
#endif

 private:
#ifdef SRS_PERF_MERGED_READ
  // the merged handler
  bool merged_read;
  IMergeReadHandler* _handler;
#endif
  // the user-space buffer to fill by reader,
  // which use fast index and reset when chunk body read ok.
  // @see https://github.com/ossrs/srs/issues/248
  // ptr to the current read position.
  char* p;
  // ptr to the content end.
  char* end;
  // ptr to the buffer.
  //      buffer <= p <= end <= buffer+nb_buffer
  char* buffer;
  // the size of buffer.
  int nb_buffer;
};

} //namespace ma

#endif //!__MEDIA_PROTOCOL_STREAM_H__
#endif 