#include "media_socket.h"

#include <fcntl.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "common/media_log.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

//////////////////////////////////////////////////////////////////////
// class Media_IPC_SAP
MEDIA_HANDLE MEDIA_IPC_SAP::GetHandle() const {
  return handler_;
}

void MEDIA_IPC_SAP::SetHandle(MEDIA_HANDLE aNew) {
  MA_ASSERT(handler_ == MEDIA_INVALID_HANDLE || aNew == MEDIA_INVALID_HANDLE);
  handler_ = aNew;
}

int MEDIA_IPC_SAP::Enable(int aValue) const {
  switch (aValue) {
    case NON_BLOCK: {
      int nVal = ::fcntl(handler_, F_GETFL, 0);
      if (nVal == -1)
        return -1;
      nVal |= O_NONBLOCK;
      if (::fcntl(handler_, F_SETFL, nVal) == -1)
        return -1;
      return 0;
    }

    default:
      MLOG_ERROR_THIS("Media_IPC_SAP::Enable, aValue=" << aValue);
      return -1;
  }
}

int MEDIA_IPC_SAP::Disable(int aValue) const {
  switch (aValue) {
    case NON_BLOCK: {
      int nVal = ::fcntl(handler_, F_GETFL, 0);
      if (nVal == -1)
        return -1;
      nVal &= ~O_NONBLOCK;
      if (::fcntl(handler_, F_SETFL, nVal) == -1)
        return -1;
      return 0;
    }

    default:
      MLOG_ERROR_THIS("Media_IPC_SAP::Disable, aValue=" << aValue);
      return -1;
  }
}

int MEDIA_IPC_SAP::Control(int cmd, void* arg) const {
  return ::ioctl(handler_, cmd, arg);
}

// MediaSocketBase
int MediaSocketBase::Open(int family, int type, int protocol, bool reuse) {
  int nRet = -1;
  Close();

  handler_ = (MEDIA_HANDLE)::socket(family, type, protocol);
  if (handler_ != MEDIA_INVALID_HANDLE) {
    nRet = 0;
    if (family != PF_UNIX && reuse) {
      int nReuse = 1;
      nRet = SetOpt(SOL_SOCKET, SO_REUSEADDR, &nReuse, sizeof(nReuse));
    }
  }

  if (nRet == -1) {
    ErrnoGuard theGuard;
    Close();
  }
  return nRet;
}

int MediaSocketBase::GetRemoteAddr(MediaAddress& addr) const {
  int nSize = (int)addr.GetSize();
  int nGet = ::getpeername(
      (MEDIA_HANDLE)handler_,
      reinterpret_cast<sockaddr*>(const_cast<sockaddr_in*>(addr.GetPtr())),
      reinterpret_cast<socklen_t*>(&nSize));
  return nGet;
}

int MediaSocketBase::GetLocalAddr(MediaAddress& addr) const {
  int nSize = (int)addr.GetSize();
  int nGet = ::getsockname(
      (MEDIA_HANDLE)handler_,
      reinterpret_cast<sockaddr*>(const_cast<sockaddr_in*>(addr.GetPtr())),
      reinterpret_cast<socklen_t*>(&nSize));

  return nGet;
}

int MediaSocketBase::Close() {
  int nRet = 0;
  if (handler_ != MEDIA_INVALID_HANDLE) {
    nRet = ::close(handler_);
    handler_ = MEDIA_INVALID_HANDLE;
  }
  return nRet;
}

MediaSocketBase::MediaSocketBase() = default;

MediaSocketBase::~MediaSocketBase() {
  Close();
}

int MediaSocketBase::SetOpt(int level, int opt, const void* val, int len) const {
  if (level == SOL_SOCKET && (opt == SO_SNDBUF || opt == SO_RCVBUF)) {
    int name[] = {CTL_NET, NET_CORE, NET_CORE_WMEM_MAX};
    if (opt == SO_RCVBUF) {
      name[2] = NET_CORE_RMEM_MAX;
    }
    uint32_t oldval = 0;
    size_t oldlen = sizeof(uint32_t);
    uint32_t newval = *(uint32_t*)val;
    size_t newlen = sizeof(uint32_t);
    int rv = sysctl(name, 3, &oldval, &oldlen, 0, 0);
    if (rv == -1) {
      MLOG_WARN_THIS("set " << ((opt == SO_SNDBUF) ? "wmem_max" : "rmem_max")
                     << " fail! errno=" << errno);
    } else if (oldval < newval) {
      rv = sysctl(name, 3, 0, 0, &newval, newlen);
      if (rv == -1) {
        MLOG_WARN_THIS("set " << ((opt == SO_SNDBUF) ? "wmem_max" : "rmem_max")
                       << " fail! errno=" << errno);
      } else {
        MLOG_INFO_THIS(((opt == SO_SNDBUF) ? "wmem_max" : "rmem_max")
                      << "=" << newval);
      }
    }
  }
  return ::setsockopt((MEDIA_HANDLE)handler_, level, opt, val, len);
}

int MediaSocketBase::GetOpt(int level, int opt, void* val, int* len) const {
  return ::getsockopt((MEDIA_HANDLE)handler_, level, opt, val,
      reinterpret_cast<socklen_t*>(len));
}

int MediaSocketBase::Recv(char* buf, uint32_t len, int flag) const {
  int nRet = ::recv((MEDIA_HANDLE)handler_, buf, len, flag);
  if (nRet == -1 && errno == EAGAIN)
    errno = EWOULDBLOCK;

  return nRet;
}

int MediaSocketBase::RecvV(iovec iov[], uint32_t count) const {
  return ::readv(handler_, iov, count);
}

int MediaSocketBase::Send(const char* aBuf, uint32_t aLen, int aFlag) const {
  int nRet = ::send((MEDIA_HANDLE)handler_, aBuf, aLen, aFlag);
  if (nRet == -1 && errno == EAGAIN)
    errno = EWOULDBLOCK;
  return nRet;
}

int MediaSocketBase::SendV(const iovec aIov[], uint32_t aCount) const {
  return ::writev(handler_, aIov, aCount);
}

// class MediaSocketStream
MediaSocketStream::MediaSocketStream() = default;
MediaSocketStream::~MediaSocketStream() {
  Close();
}

int MediaSocketStream::Open(bool aReuseAddr, const MediaAddress& aLocal) {
  if (MediaSocketBase::Open(aLocal.GetType(), SOCK_STREAM, 0, aReuseAddr) == -1)
    return -1;

  if (::bind((MEDIA_HANDLE)handler_,
             reinterpret_cast<const sockaddr*>(aLocal.GetPtr()),
             static_cast<socklen_t>(aLocal.GetSize())) == -1) {
    ErrnoGuard theGuard;
    Close();
    return -1;
  }

  this->set_quickack();
  return 0;
}

int MediaSocketStream::Open(bool resuse, uint16_t family) {
  int ret = MediaSocketBase::Open(family, SOCK_STREAM, 0, resuse);
  if (ret == -1)
    return ret;
  this->set_quickack();
  return ret;
}

void MediaSocketStream::set_quickack() {
  int val = 0;
  if (::setsockopt(handler_, IPPROTO_TCP, TCP_QUICKACK, (void*)&val, 4) != 0)
    MLOG_WARN_THIS("set quickack failed, err=" << errno);
}

int MediaSocketBase::CloseWriter() {
  return ::shutdown((MEDIA_HANDLE)handler_, SD_SEND);
}

int MediaSocketStream::CloseReader() {
  return ::shutdown((MEDIA_HANDLE)handler_, SD_RECEIVE);
}

// MediaSocketDgram
MediaSocketDgram::MediaSocketDgram() = default;

MediaSocketDgram::~MediaSocketDgram() {
  Close();
}
int MediaSocketDgram::Open(const MediaAddress& aLocal) {
  if (MediaSocketBase::Open(aLocal.GetType(), SOCK_DGRAM, 0, false) == -1)
    return -1;

  if (::bind((MEDIA_HANDLE)handler_,
             reinterpret_cast<const sockaddr*>(aLocal.GetPtr()),
             static_cast<socklen_t>(aLocal.GetSize())) == -1) {
    ErrnoGuard theGuard;
    Close();
    return -1;
  }
  return 0;
}

int MediaSocketDgram::RecvFrom(char* aBuf,
                               uint32_t aLen,
                               MediaAddress& aAddr,
                               int aFlag) const {
  int nSize = (int)aAddr.GetSize();
  return ::recvfrom((MEDIA_HANDLE)handler_, aBuf, aLen, aFlag,
                        (sockaddr*)aAddr.GetPtr(),
                        reinterpret_cast<socklen_t*>(&nSize));
}

int MediaSocketDgram::RecvFrom(char* aBuf,
                               uint32_t aLen,
                               char* addr_buf,
                               int buf_len,
                               int aFlag) const {
  return ::recvfrom((MEDIA_HANDLE)handler_, aBuf, aLen, aFlag, (sockaddr*)addr_buf,
                 reinterpret_cast<socklen_t*>(&buf_len));
}

int MediaSocketDgram::SendTo(const char* aBuf,
                             uint32_t aLen,
                             const MediaAddress& aAddr,
                             int aFlag) const {
  return ::sendto((MEDIA_HANDLE)handler_, aBuf, aLen, aFlag,
                      reinterpret_cast<const sockaddr*>(aAddr.GetPtr()),
                      static_cast<socklen_t>(aAddr.GetSize()));
}

int MediaSocketDgram::SendVTo(const iovec aIov[],
                              uint32_t aCount,
                              const MediaAddress& aAddr) const {
  msghdr send_msg;
  send_msg.msg_iov = (iovec*)aIov;
  send_msg.msg_iovlen = aCount;
  send_msg.msg_name = (struct sockaddr*)aAddr.GetPtr();
  send_msg.msg_namelen = aAddr.GetSize();

  send_msg.msg_control = 0;
  send_msg.msg_controllen = 0;
  send_msg.msg_flags = 0;
  int nRet = ::sendmsg(handler_, &send_msg, 0);
  return nRet;
}

}  // namespace ma
