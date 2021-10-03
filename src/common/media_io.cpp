#include "common/media_io.h"

#include <fcntl.h>
#include <unistd.h>

#include "utils/media_msg_chain.h"
#include "http/h/http_protocal.h"

namespace ma {

// For utest to mock it.
typedef int (*srs_open_t)(const char* path, int oflag, ...);
typedef ssize_t (*srs_write_t)(int fildes, const void* buf, size_t nbyte);
typedef ssize_t (*srs_read_t)(int fildes, void* buf, size_t nbyte);
typedef off_t (*srs_lseek_t)(int fildes, off_t offset, int whence);
typedef int (*srs_close_t)(int fildes);

// For utest to mock it.
srs_open_t _srs_open_fn = ::open;
srs_write_t _srs_write_fn = ::write;
srs_read_t _srs_read_fn = ::read;
srs_lseek_t _srs_lseek_fn = ::lseek;
srs_close_t _srs_close_fn = ::close;

MDEFINE_LOGGER(SrsFileWriter, "SrsFileWriter");

SrsFileWriter::SrsFileWriter() {
  fd = -1;
}

SrsFileWriter::~SrsFileWriter() {
  close();
}

srs_error_t SrsFileWriter::open(const std::string& p) {
  srs_error_t err = srs_success;
  
  if (fd > 0) {
    return srs_error_new(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", p.c_str());
  }
  
  int flags = O_CREAT|O_WRONLY|O_TRUNC;
  mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
  
  if ((fd = _srs_open_fn(p.c_str(), flags, mode)) < 0) {
    return srs_error_new(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
  }
  
  path = p;
  
  return err;
}

srs_error_t SrsFileWriter::open_append(const std::string& p) {
  srs_error_t err = srs_success;
  
  if (fd > 0) {
    return srs_error_new(ERROR_SYSTEM_FILE_ALREADY_OPENED, "file %s already opened", path.c_str());
  }
  
  int flags = O_CREAT|O_APPEND|O_WRONLY;
  mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
  
  if ((fd = _srs_open_fn(p.c_str(), flags, mode)) < 0) {
    return srs_error_new(ERROR_SYSTEM_FILE_OPENE, "open file %s failed", p.c_str());
  }
  
  path = p;
  
  return err;
}

void SrsFileWriter::close() {
  if (fd < 0) {
    return;
  }
  
  if (_srs_close_fn(fd) < 0) {
    MLOG_CWARN("close file %s failed", path.c_str());
  }
  fd = -1;
  
  return;
}

bool SrsFileWriter::is_open() {
  return fd > 0;
}

void SrsFileWriter::seek2(int64_t offset)
{
  off_t r0 = _srs_lseek_fn(fd, (off_t)offset, SEEK_SET);
  srs_assert(r0 != -1);
}

int64_t SrsFileWriter::tellg() {
  return (int64_t)_srs_lseek_fn(fd, 0, SEEK_CUR);
}

srs_error_t SrsFileWriter::write(void* buf, size_t count, ssize_t* pnwrite) {
  srs_error_t err = srs_success;
  
  ssize_t nwrite;

  if ((nwrite = _srs_write_fn(fd, buf, count)) < 0) {
    return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "write to file %s failed", path.c_str());
  }
  
  if (pnwrite != NULL) {
    *pnwrite = nwrite;
  }
  
  return err;
}

srs_error_t SrsFileWriter::write(MessageChain* msg, ssize_t* pnwrite) {
  srs_error_t err = srs_success;

  MessageChain* next = msg;
  ssize_t nwrite = 0;
  while (next != nullptr) {
    ssize_t this_nwrite = 0;
    uint32_t len = next->GetFirstMsgLength();
    if (len != 0) {
      if ((err = write((void*)next->GetFirstMsgReadPtr(), len, &this_nwrite)) != srs_success) {
        return srs_error_wrap(err, "write file");
      }
      nwrite += this_nwrite;
    }
    next = next->GetNext();
  }
  
  if (pnwrite) {
    *pnwrite = nwrite;
  }
  
  return err;
}

srs_error_t SrsFileWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) {
  srs_error_t err = srs_success;
  
  ssize_t nwrite = 0;
  for (int i = 0; i < iovcnt; i++) {
    const iovec* piov = iov + i;
    ssize_t this_nwrite = 0;
    if ((err = write(piov->iov_base, piov->iov_len, &this_nwrite)) != srs_success) {
      return srs_error_wrap(err, "write file");
    }
    nwrite += this_nwrite;
  }
  
  if (pnwrite) {
    *pnwrite = nwrite;
  }
  
  return err;
}

srs_error_t SrsFileWriter::lseek(off_t offset, int whence, off_t* seeked) {
  off_t sk = _srs_lseek_fn(fd, offset, whence);
  if (sk < 0) {
    return srs_error_new(ERROR_SYSTEM_FILE_SEEK, "seek file");
  }
  
  if (seeked) {
    *seeked = sk;
  }
  
  return srs_success;
}

SrsBufferWriter::SrsBufferWriter(std::shared_ptr<IHttpResponseWriter> w)
  : writer_{w} {
}

srs_error_t SrsBufferWriter::open(const std::string& /*file*/) {
  return srs_success;
}

void SrsBufferWriter::close() {
}

bool SrsBufferWriter::is_open() {
  return true;
}

int64_t SrsBufferWriter::tellg() {
  return 0;
}

srs_error_t SrsBufferWriter::write(void* buf, size_t count, ssize_t* pnwrite) {
  srs_error_t err = srs_success;
  if (pnwrite) {
      *pnwrite = count;
  }

  auto writer = writer_.lock();

  if (writer) {
    err = writer->write((const char*)buf, (int)count);
  }
  
  return err;
}

srs_error_t SrsBufferWriter::write(MessageChain* msg, ssize_t* pnwrite) {
  srs_error_t result  = srs_success;

  auto writer = writer_.lock();
  if (writer && (result = writer->write(msg, pnwrite)) != srs_success) {
    return srs_error_wrap(result, "SrsBufferWriter::write");
  }

  return result;
}

srs_error_t SrsBufferWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) {
  srs_error_t result  = srs_success;

  auto writer = writer_.lock();
  if (writer && (result = writer->writev(iov, iovcnt, pnwrite)) != srs_success) {
    return srs_error_wrap(result, "SrsBufferWriter::writev");
  }

  return result;
}

}

