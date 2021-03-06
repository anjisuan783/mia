//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_IO_H__
#define __MEDIA_IO_H__

#include <sys/uio.h>
#include <memory>

#include "common/media_kernel_error.h"

namespace ma {

/**
 * The seeker to seek with a device.
 */
class ISrsSeeker {
 public:
  ISrsSeeker() = default;
  virtual ~ISrsSeeker() = default;
 public:
  /**
   * The lseek() function repositions the offset of the file descriptor fildes to the argument offset, according to the
   * directive whence. lseek() repositions the file pointer fildes as follows:
   *      If whence is SEEK_SET, the offset is set to offset bytes.
   *      If whence is SEEK_CUR, the offset is set to its current location plus offset bytes.
   *      If whence is SEEK_END, the offset is set to the size of the file plus offset bytes.
   * @param seeked Upon successful completion, lseek() returns the resulting offset location as measured in bytes from
   *      the beginning of the file. NULL to ignore.
   */
  virtual srs_error_t lseek(off_t offset, int whence, off_t* seeked) = 0;
};

class MessageChain;

/**
* The writer to write stream data to channel.
*/
class ISrsStreamWriter {
 public:
  ISrsStreamWriter() = default;
  virtual ~ISrsStreamWriter() = default;
 public:
  /**
   * write bytes over writer.
   * @nwrite the actual written bytes. NULL to ignore.
   */
  virtual srs_error_t write(void* buf, size_t size, ssize_t* nwrite) = 0;
  
  /**
   * write iov over writer.
   * @nwrite the actual written bytes. NULL to ignore.
   * @remark for the HTTP FLV, to writev to improve performance.
   *      @see https://github.com/ossrs/srs/issues/405
   */
  virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t* nwrite) = 0;
  virtual srs_error_t write(MessageChain*, ssize_t* nwrite) = 0; 
};

/**
* The generally writer, stream and vector writer.
*/
class ISrsWriter : public ISrsStreamWriter {
 public:
  ISrsWriter() = default;
  virtual ~ISrsWriter() = default;
};


/**
* The writer and seeker.
*/
class ISrsWriteSeeker : public ISrsWriter, public ISrsSeeker {
 public:
  ISrsWriteSeeker() = default;
  virtual ~ISrsWriteSeeker() = default;
};

class SrsFileWriter : public ISrsWriteSeeker {
 private:
  std::string path;
  int fd;
 public:
  SrsFileWriter();
  virtual ~SrsFileWriter();
  /**
   * open file writer, in truncate mode.
   * @param p a string indicates the path of file to open.
   */
  virtual srs_error_t open(const std::string& p);
  /**
   * open file writer, in append mode.
   * @param p a string indicates the path of file to open.
   */
  virtual srs_error_t open_append(const std::string& p);
  /**
   * close current writer.
   * @remark user can reopen again.
   */
  virtual void close();

  virtual bool is_open();
  virtual void seek2(int64_t offset);
  virtual int64_t tellg();

  // Interface ISrsWriteSeeker
  srs_error_t write(void* buf, size_t count, ssize_t* pnwrite) override;
  srs_error_t write(MessageChain*, ssize_t* nwrite) override;
  srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) override;
  srs_error_t lseek(off_t offset, int whence, off_t* seeked) override;
};

class IHttpResponseWriter;

// Write stream to http response direclty.
class SrsBufferWriter : public SrsFileWriter {
 private:
  std::shared_ptr<IHttpResponseWriter> writer_;
 public:
  SrsBufferWriter(std::shared_ptr<IHttpResponseWriter> w);
  virtual ~SrsBufferWriter() = default;

  srs_error_t open(const std::string& file) override;
  void close() override;

  bool is_open() override;
  int64_t tellg() override;
 
  srs_error_t write(void* buf, size_t count, ssize_t* pnwrite) override;
  srs_error_t write(MessageChain*, ssize_t* pnwrite) override;
  srs_error_t writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) override;
};

/**
 * The reader to read data from channel.
 */
class ISrsReader {
 public:
  ISrsReader() = default;
  virtual ~ISrsReader() = default;
 public:
  /**
   * Read bytes from reader.
   * @param nread How many bytes read from channel. NULL to ignore.
   */
  virtual srs_error_t read(void* buf, size_t size, ssize_t* nread) = 0;
};

/**
 * The reader and seeker.
 */
class ISrsReadSeeker : public ISrsReader, public ISrsSeeker {
 public:
  ISrsReadSeeker() = default;
  virtual ~ISrsReadSeeker() = default;
};

/**
 * file reader, to read from file.
 */
class SrsFileReader : public ISrsReadSeeker {
private:
  std::string path;
  int fd{-1};
 public:
  SrsFileReader() = default;
  virtual ~SrsFileReader();
 public:
  /**
   * open file reader.
   * @param p a string indicates the path of file to open.
   */
  virtual srs_error_t open(const std::string& p);
  /**
   * close current reader.
   * @remark user can reopen again.
   */
  virtual void close();
 public:
  // TODO: FIXME: extract interface.
  virtual bool is_open();
  virtual int64_t tellg();
  virtual void skip(int64_t size);
  virtual int64_t seek2(int64_t offset);
  virtual int64_t filesize();
// Interface ISrsReadSeeker
 public:
  srs_error_t read(void* buf, size_t size, ssize_t* nread) override;
  srs_error_t lseek(off_t offset, int whence, off_t* seeked) override;
};

// The file reader factory.
class ISrsFileReaderFactory final {
 public:
  SrsFileReader* create_file_reader();
};

}
#endif //!__MEDIA_IO_H__
