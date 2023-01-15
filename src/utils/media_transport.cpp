#include "utils/media_transport.h"

#include <linux/uio.h>
#include <string.h>
#include <memory>

#include "common/media_define.h"
#include "utils/media_msg_chain.h"
#include "utils/media_reactor.h"
#include "utils/media_socket.h"
#include "utils/media_thread.h"

namespace ma {

class ErrnoGuard {
 public:
  ErrnoGuard() : err_(errno) {}
  ~ErrnoGuard() { errno = err_; }

 private:
  int err_;
};

std::string GetSystemErrorInfo(int inErrno) {
#define RT_ERROR_BUF_SIZE 1024
  char szErrorBuf[RT_ERROR_BUF_SIZE] = "";
  sprintf(szErrorBuf, "%d:", inErrno);
  size_t len = strlen(szErrorBuf);

  strncpy(szErrorBuf + len, strerror(inErrno), RT_ERROR_BUF_SIZE - 1 - len);
  return std::string(szErrorBuf);
}

class TransportBase : public Transport, public MediaHandler {
 public:
  TransportBase();
  ~TransportBase() override;

  // transport implement
  int Open(TransportSink* sink) override;
  TransportSink* GetSink() override;
  int GetOpt(int cmd, void* args) const override;
  int Disconnect(int reason) override;

  // MediaHandler implement
  int OnClose(MEDIA_HANDLE fd, MASK mask) override;

 protected:
  // template method for open() and close()
  virtual int Open_t() = 0;
  virtual int Close_t(int reason) = 0;

  TransportSink* sink_ = nullptr;
};

class TransportTcp : public TransportBase {
 public:
  TransportTcp(MediaThread*);
  ~TransportTcp() override;

  // MediaHandler implement
  MEDIA_HANDLE GetHandle() const override;
  int OnInput(MEDIA_HANDLE) override;
  int OnOutput(MEDIA_HANDLE) override;

  // transport implement
  int Send(MessageChain& msg, bool destroy = false) override;
  int SetOpt(int cmd, void* args) override;

 protected:
  int Open_t() override;
  int Close_t(int aReason) override;
  int Recv_i(const char* aBuf, uint32_t aLen);
  int RegisterHandler(MediaHandler::MASK);
  void RemoveHandler();

 protected:
  MediaThread* thread_ = nullptr;
  iovec* iov_ = nullptr;
  char* io_buffer_ = nullptr;
  MediaSocketStream socket_;
  bool need_on_send_ = false;
  MediaAddress m_peerAddrInProxyCase;
};

// class TransportBase
TransportBase::TransportBase() = default;

TransportBase::~TransportBase() = default;

int TransportBase::Open(TransportSink* sink) {
  if (!sink)
    return ERROR_INVALID_ARGS;

  if (sink_) {
    sink_ = sink;
    return ERROR_SUCCESS;
  }

  sink_ = sink;

  int ret = Open_t();
  if (ERROR_SUCCESS != ret) {
    Close_t(ERROR_SUCCESS);
    sink_ = NULL;
  }
  return ret;
}

TransportSink* TransportBase::GetSink() {
  return sink_;
}

int TransportBase::GetOpt(int cmd, void* args) const {
  return ERROR_INVALID_ARGS;
}

int TransportBase::Disconnect(int aReason) {
  int ret = Close_t(aReason);
  sink_ = nullptr;
  return ret;
}

int TransportBase::OnClose(MEDIA_HANDLE, MASK) {
  Close_t(ERROR_SUCCESS);
  TransportSink* copy = sink_;
  sink_ = NULL;

  if (copy)
    copy->OnDisconnect(
        srs_error_new(ERROR_SOCKET_CLOSED, "socket has been closed"));
  return ERROR_SUCCESS;
}

extern NetThreadManager g_netthdmgr;

// class TransportTcp
TransportTcp::TransportTcp(MediaThread* t) : thread_(t) {
  int ret =
      g_netthdmgr.GetIOBuffer(thread_->GetThreadHandle(), iov_, io_buffer_);
  assert(ret == 0);
}

TransportTcp::~TransportTcp() {
  Close_t(ERROR_SUCCESS);
}

MEDIA_HANDLE TransportTcp::GetHandle() const {
  return socket_.GetHandle();
}

int TransportTcp::OnInput(MEDIA_HANDLE) {
  int nRecv = Recv_i(io_buffer_, MEDIA_SOCK_IOBUFFER_SIZE);
  if (nRecv <= 0)
    return nRecv;

  MessageChain msg(nRecv, io_buffer_,
                   MessageChain::DONT_DELETE | MessageChain::WRITE_LOCKED,
                   nRecv);

  if (sink_)
    sink_->OnRecv(msg);

  return 0;
}

int TransportTcp::OnOutput(MEDIA_HANDLE fd) {
  if (!need_on_send_ || socket_.GetHandle() == MEDIA_INVALID_HANDLE)
    return 0;

  need_on_send_ = false;
  if (sink_)
    sink_->OnSend();

  return 0;
}

int TransportTcp::Send(MessageChain& in_msg, bool destroy) {
  if (socket_.GetHandle() == MEDIA_INVALID_HANDLE)
    return ERROR_NOT_AVAILABLE;

  if (need_on_send_)
    return ERROR_PARTIAL_DATA;

  const MessageChain* tmpData = &in_msg;
  uint32_t iovNum = 0;
  uint32_t fillLen = 0;
  int sentTotal = 0;
  int rv = 0;

  do {
    iovNum = tmpData->FillIov(iov_, IOV_MAX, fillLen, tmpData);
    if (iovNum == 0)
      break;

    rv = socket_.SendV(iov_, iovNum);
    if (rv < 0) {
      if (errno == EWOULDBLOCK)) {
          need_on_send_ = true;
          break;
        }
      else {
        MLOG_WARN_THIS("sendv failed!"
                       << ", fd=" << socket_.GetHandle()
                       << ", err=" << GetSystemErrorInfo(errno) << ", rv=" << rv
                       << ", fillLen=" << fillLen);
        return ERROR_NETWORK_SOCKET_ERROR;
      }
    }
    sentTotal += rv;
    if ((uint32_t)rv < fillLen) {
      need_on_send_ = true;
      break;
    }
  } while (tmpData);

  if (need_on_send_) {
    in_msg.AdvanceChainedReadPtr(sentTotal);
    return ERROR_PARTIAL_DATA;
  }

  if (destroy)
    in_msg.DestroyChained();

  return ERROR_SUCCESS;
}

int TransportTcp::SetOpt(int cmd, void* aArg) {
  switch (cmd) {
    case OPT_TRANSPORT_FD: {
      // we allow user to set TCP socket to MEDIA_INVALID_HANDLE,
      // mainly used by CRtConnectorProxyT.
      MEDIA_HANDLE hdNew = *(static_cast<MEDIA_HANDLE*>(aArg));
      socket_.SetHandle(hdNew);
      return ERROR_SUCCESS;
    }
    case OPT_TRANSPORT_PEER_ADDR:
      // In the case of connection via proxy.
      m_peerAddrInProxyCase = *(static_cast<MediaAddress*>(aArg));
      return ERROR_SUCCESS;

    case OPT_TRANSPORT_TCP_KEEPALIVE: {
      uint32_t dwTime = *static_cast<uint32_t*>(aArg);
      int nKeep = dwTime > 0 ? 1 : 0;
      if (socket_.SetOption(SOL_SOCKET, SO_KEEPALIVE, &nKeep,
                                sizeof(nKeep)) == -1) {
        MLOG_ERROR_THIS(
            "TransportTcp::SetOption, SetOption(SO_KEEPALIVE) failed!"
            " dwTime="
            << dwTime << " err=" << errno);
        return RT_ERROR_NETWORK_SOCKET_ERROR;
      }
      if (dwTime > 0) {
        int keepIdle = (int)dwTime;
        int keepInterval = 1;
        int keepCount = 10;
        if (socket_.SetOption(SOL_TCP, TCP_KEEPIDLE, (void*)&keepIdle,
                                  sizeof(keepIdle)) == -1) {
          MLOG_ERROR_THIS("SetOption(TCP_KEEPIDLE) failed!"
                          << " keepIdle=" << keepIdle << ", err=" << errno);
          return RT_ERROR_NETWORK_SOCKET_ERROR;
        }
        if (socket_.SetOption(SOL_TCP, TCP_KEEPINTVL, (void*)&keepInterval,
                                  sizeof(keepInterval)) == -1) {
          MLOG_ERROR_THIS("SetOption(TCP_KEEPINTVL) failed!"
                          << " keepInterval=" << keepInterval
                          << ", err=" << errno);
          return RT_ERROR_NETWORK_SOCKET_ERROR;
        }
        if (socket_.SetOption(SOL_TCP, TCP_KEEPCNT, (void*)&keepCount,
                                  sizeof(keepCount)) == -1) {
          MLOG_ERROR_THIS("SetOption(TCP_KEEPCNT) failed!"
                          << " keepCount=" << keepCount << ", err=" << errno);
          return ERROR_NETWORK_SOCKET_ERROR;
        }
      }
      return ERROR_SUCCESS;
    }

    case OPT_TRANSPORT_RCV_BUF_LEN:
      if (socket_.SetOption(SOL_SOCKET, SO_RCVBUF, aArg, sizeof(uint32_t)) ==
          -1)
        return ERROR_NETWORK_SOCKET_ERROR;
      else
        return ERROR_SUCCESS;

    case OPT_TRANSPORT_SND_BUF_LEN:
      if (socket_.SetOption(SOL_SOCKET, SO_SNDBUF, aArg, sizeof(uint32_t)) ==
          -1)
        return RT_ERROR_NETWORK_SOCKET_ERROR;
      else
        return ERROR_SUCCESS;

    case OPT_TRANSPORT_TOS:
      return SetTos2Socket(socket_, aArg);

    case OPT_TRANSPORT_TCP_NODELAY: {
      int ret =
          socket_.SetOption(IPPROTO_TCP, TCP_NODELAY, aArg, sizeof(int));
      if (ret == -1)
        MLOG_ERROR_THIS(
            "TransportTcp::SetOption, SetOption(TCP_NODELAY) failed! err="
            << errno);
      return ret;
    }
    case OPT_TRANSPORT_TCP_CORK: {
      int ret =
          socket_.SetOption(IPPROTO_TCP, TCP_CORK, &aArg, sizeof(int));
      if (ret == -1)
        MLOG_ERROR_THIS(
            "TransportTcp::SetOption, SetOption(TCP_CORK) failed! err="
            << errno);
      return ret;
    } break;
    default:
      MLOG_WARN_THIS(" unknow aCommand="<< aCommand << " aArg=" << aArg);
      return ERROR_INVALID_ARGS;
  }
  return ERROR_SUCCESS;
}

int TransportTcp::GetOpt(int cmd, void* args) const {
  switch (cmd) {
    case OPT_TRANSPORT_FIO_NREAD:
      if (socket_.Control(FIONREAD, aArg) == -1) {
        MLOG_WARN_THIS(
            "TransportTcp::GetOption, (OPT_TRANSPORT_FIO_NREAD) failed! err="
            << errno);
        return RT_ERROR_NETWORK_SOCKET_ERROR;
      }
      return ERROR_SUCCESS;
    case OPT_TRANSPORT_FD:
      *(static_cast<MEDIA_HANDLE*>(aArg)) = socket_.GetHandle();
      return ERROR_SUCCESS;
    case OPT_TRANSPORT_LOCAL_ADDR:
      if (socket_.GetLocalAddr(*(static_cast<MediaAddress*>(args))) == -1) {
        MLOG_WARN_THIS(
            "TransportTcp::GetOption, (OPT_TRANSPORT_LOCAL_ADDR) failed! err="
            << errno);
        return RT_ERROR_NETWORK_SOCKET_ERROR;
      } else
        return ERROR_SUCCESS;

    case OPT_TRANSPORT_PEER_ADDR:
      if (m_peerAddrInProxyCase.GetPort() != 0) {
        // In the case of connection via proxy.
        *(static_cast<MediaAddress*>(aArg)) = m_peerAddrInProxyCase;
        return ERROR_SUCCESS;
      }
      if (socket_.GetRemoteAddr(*(static_cast<MediaAddress*>(aArg))) == -1) {
        MLOG_WARN_THIS(
            "TransportTcp::GetOption, (OPT_TRANSPORT_PEER_ADDR) failed! err="
            << errno);
        return ERROR_NETWORK_SOCKET_ERROR;
      } else
        return ERROR_SUCCESS;

    case OPT_TRANSPORT_SOCK_ALIVE: {
      if (socket_.GetHandle() == MEDIA_INVALID_HANDLE) {
        *static_cast<BOOL*>(aArg) = FALSE;
        return ERROR_NOT_INITIALIZED;
      }
      char cTmp;
      int nRet = socket_.Recv(&cTmp, sizeof(cTmp), MSG_PEEK);
      if (nRet > 0 ||
          (nRet < 0 && (errno == EWOULDBLOCK)))
        *static_cast<BOOL*>(aArg) = TRUE;
      else
        *static_cast<BOOL*>(aArg) = FALSE;
      return ERROR_SUCCESS;
    }

    case OPT_TRANSPORT_SND_BUF_LEN: {
      int nLen = sizeof(uint32_t);
      if (socket_.GetOpt(SOL_SOCKET, SO_SNDBUF, args, &nLen) == -1)
        return ERROR_NETWORK_SOCKET_ERROR;
      else
        return ERROR_SUCCESS;
    }
    case OPT_TRANSPORT_RCV_BUF_LEN: {
      int nLen = sizeof(uint32_t);
      if (socket_.GetOption(SOL_SOCKET, SO_RCVBUF, args, &nLen) == -1)
        return RT_ERROR_NETWORK_SOCKET_ERROR;
      else
        return ERROR_SUCCESS;
    }

    case OPT_BIND_THREAD: {
      *static_cast<pthread_t*>(args) = thread_->GetThreadHandle();
      return ERROR_SUCCESS;
    }

    default:
      return TransportBase::GetOpt(cmd, args);
  }
}

int TransportTcp::Open_t() {
  int nodelay = 1;
  SetOpt(OPT_TRANSPORT_TCP_NODELAY, &nodelay);
  return this->RegisterHandler(MediaHandler::READ_MASK |
                               MediaHandler::WRITE_MASK);
}

int TransportTcp::Close_t(int reason) {
  if (socket_.GetHandle() != MEDIA_INVALID_HANDLE) {
    RemoveHandler();
    socket_.Close(reason);
  }
  return ERROR_SUCCESS;
}

int TransportTcp::Recv_i(char* buf, uint32_t len) {
  int nRecv = socket_.Recv(buf, len);

  if (nRecv < 0) {
    if (errno == EWOULDBLOCK)
      return -2;
    else {
      ErrnoGuard egTmp;
      MLOG_WARN_THIS(
          "recv() failed!"
          " fd="
          << socket_.GetHandle() << " err=" << GetSystemErrorInfo(errno));
      return -1;
    }
  }
  if (nRecv == 0) {
    MLOG_WARN_THIS("recv() 0! fd=" << socket_.GetHandle());
    // it is a graceful disconnect
    return -1;
  }
  return nRecv;
}

int TransportTcp::RegisterHandler(MediaHandler::MASK mask) {
  int rv = thread_->Reactor()->RegisterHandler(this, mask);
  if (ERROR_SUCCESS != rv && rv != ERROR_EXISTED) {
    MLOG_ERROR_THIS("RegisterHandler(" << mask << ") failed! rv=" << rv);
    return rv;
  }

  return ERROR_SUCCESS
}

void TransportTcp::RemoveHandler() {
  if (thread_) {
    thread_->Reactor()->RemoveHandler(this);
    thread_ = nullptr;
  }
}

}  // namespace ma
