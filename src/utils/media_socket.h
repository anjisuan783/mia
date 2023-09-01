#ifndef __MEDIA_SOCKET_H__
#define __MEDIA_SOCKET_H__

#include "common/media_define.h"
#include "common/media_kernel_error.h"
#include "utils/media_addr.h"

namespace ma {

class ErrnoGuard {
 public:
  ErrnoGuard() : err_(errno) {}
  ~ErrnoGuard() { errno = err_; }

 private:
  int err_;
};

class MEDIA_IPC_SAP {
 public:
  enum { NON_BLOCK = 0 };

  MEDIA_IPC_SAP() = default;

  MEDIA_HANDLE GetHandle() const;
  void SetHandle(MEDIA_HANDLE);

  int Enable(int aValue) const;
  int Disable(int aValue) const;
  int Control(int cmd, void* arg) const;

 protected:
  MEDIA_HANDLE handler_ = MEDIA_INVALID_HANDLE;
};

class MediaSocketBase : public MEDIA_IPC_SAP {
 protected:
  enum {
    SD_RECEIVE,
    SD_SEND,
    SD_BOTH
  };

 public:
   MediaSocketBase();
  ~MediaSocketBase();
  int Open(int family, int type, int protocol, bool reuse);
  int Close();

  /// Wrapper around the <setsockopt> system call.
  int SetOpt(int level, int option, const void* val, int len) const;

  /// Wrapper around the <getsockopt> system call.
  int GetOpt(int level, int option, void* val, int* len) const;

  /// Return the address of the remotely connected peer (if there is
  /// one), in the referenced <addr>.
  int GetRemoteAddr(MediaAddress& addr) const;

  /// Return the local endpoint address in the referenced <addr>.
  int GetLocalAddr(MediaAddress& addr) const;

  /// Recv an <len> byte buffer from the connected socket.
  int Recv(char* buf, uint32_t len, int flag = 0) const;

  /// Recv an <iov> of size <count> from the connected socket.
  int RecvV(iovec iov[], uint32_t count) const;

  /// Send an <len> byte buffer to the connected socket.
  int Send(const char* buf, uint32_t len, int flag = 0) const;

  /// Send an <iov> of size <count> from the connected socket.
  int SendV(const iovec iov[], uint32_t count) const;

 protected:
  int CloseWriter();
};

class MediaSocketStream : public MediaSocketBase {
 public:
  MediaSocketStream();
  ~MediaSocketStream();

  int Open(bool aReuseAddr, const MediaAddress& aLocal);
  int Open(bool aReuseAddr, uint16_t family);
  int CloseReader();

 protected:
  void set_quickack();
};

class MediaSocketDgram : public MediaSocketBase {
 public:
  MediaSocketDgram();
  ~MediaSocketDgram();

  int Open(const MediaAddress& aLocal);
  int RecvFrom(char* aBuf,
               uint32_t aLen,
               MediaAddress& aAddr,
               int aFlag = 0) const;
  int RecvFrom(char* aBuf,
               uint32_t aLen,
               char* addr_buf,
               int buf_len,
               int aFlag = 0) const;
  int SendTo(const char* aBuf,
             uint32_t aLen,
             const MediaAddress& aAddr,
             int aFlag = 0) const;
  int SendVTo(const iovec aIov[],
              uint32_t aCount,
              const MediaAddress& aAddr) const;
};

}  // namespace ma

#endif  //!__MEDIA_SOCKET_H__
