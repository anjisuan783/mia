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
  void SetHandle(MEDIA_HANDLE aNew);

  int Enable(int aValue) const;
  int Disable(int aValue) const;
  int Control(int aCmd, void* aArg) const;

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
  MediaSocketBase();
  ~MediaSocketBase();

 public:
  /// Wrapper around the BSD-style <socket> system call (no QoS).
  int Open(int aFamily, int aType, int aProtocol, bool aReuseAddr);

  /// Close down the socket handle.
  int Close(int reason = ERROR_SUCCESS);

  /// Wrapper around the <setsockopt> system call.
  int SetOpt(int aLevel,
                int aOption,
                const void* aOptval,
                int aOptlen) const;

  /// Wrapper around the <getsockopt> system call.
  int GetOpt(int aLevel, int aOption, void* aOptval, int* aOptlen) const;

  /// Return the address of the remotely connected peer (if there is
  /// one), in the referenced <aAddr>.
  int GetRemoteAddr(MediaAddress& aAddr) const;

  /// Return the local endpoint address in the referenced <aAddr>.
  int GetLocalAddr(MediaAddress& aAddr) const;

  /// Recv an <aLen> byte buffer from the connected socket.
  int Recv(char* aBuf, uint32_t aLen, int aFlag = 0) const;

  /// Recv an <aIov> of size <aCount> from the connected socket.
  int RecvV(iovec aIov[], uint32_t aCount) const;

  /// Send an <aLen> byte buffer to the connected socket.
  int Send(const char* aBuf, uint32_t aLen, int aFlag = 0) const;

  /// Send an <aIov> of size <aCount> from the connected socket.
  int SendV(const iovec aIov[], uint32_t aCount) const;

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
