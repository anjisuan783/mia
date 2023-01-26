#include "utils/media_transport.h"

#include <sys/uio.h>
#include <netinet/tcp.h>

#include <string.h>
#include <memory>

#include "common/media_define.h"
#include "utils/media_msg_chain.h"
#include "utils/media_reactor.h"
#include "utils/media_socket.h"
#include "utils/media_thread.h"

namespace ma {

std::string GetSystemErrorInfo(int inErrno) {
#define RT_ERROR_BUF_SIZE 1024
  char szErrorBuf[RT_ERROR_BUF_SIZE] = "";
  sprintf(szErrorBuf, "%d:", inErrno);
  size_t len = strlen(szErrorBuf);

  strncpy(szErrorBuf + len, strerror(inErrno), RT_ERROR_BUF_SIZE - 1 - len);
  return std::string(szErrorBuf);
}

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

class TransportBase : public Transport, public MediaHandler {
 public:
  TransportBase();
  ~TransportBase() override;

  // transport implement
  srs_error_t Open(TransportSink* sink) override;
  int Close() override;
  TransportSink* GetSink() override;
  int SetOpt(int cmd, void* args) override;
  int GetOpt(int cmd, void* args) const override;
  
  MediaAddress GetLocalAddr() override;
  MediaAddress GetPeerAddr() override;

  // MediaHandler implement
  int OnClose(MEDIA_HANDLE fd, MASK mask) override;

 private:
  int OnException(MEDIA_HANDLE fd = MEDIA_INVALID_HANDLE) override {
    MA_ASSERT(false);
    return -1;
  }

 protected:
  // template method for open() and close()
  virtual srs_error_t Open_t() = 0;
  virtual int Close_t() = 0;

 protected:
  TransportSink* sink_ = nullptr;
  std::unique_ptr<MediaSocketBase> socket_;
};

class TransportTcp final : public TransportBase {
 public:
  TransportTcp(MediaThread*);
  ~TransportTcp() override;

  // MediaHandler implement
  MEDIA_HANDLE GetHandle() const override;
  int OnInput(MEDIA_HANDLE) override;
  int OnOutput(MEDIA_HANDLE) override;

  // transport implement
  int Write(MessageChain& msg, bool destroy = false) override;
  void SetSocketHandler(MEDIA_HANDLE handler) override;
  MEDIA_HANDLE GetSocketHandler() const;

  int SetBufferSize(bool recv, uint32_t size);
  int GetBufferSize(bool recv, uint32_t& size);

  int SetNodelay(bool enable);
  int SetTcpCork(bool enable);

 protected:
  srs_error_t Open_t() override;
  int Close_t() override;
  int Recv_i(char* buf, uint32_t len);
  srs_error_t RegisterHandler(MediaHandler::MASK);
  srs_error_t RemoveHandler();

 protected:
  MediaThread* thread_ = nullptr;
  iovec* iov_ = nullptr;
  char* io_buffer_ = nullptr;
  bool need_on_send_ = false;
};

// class TransportBase
TransportBase::TransportBase() = default;

TransportBase::~TransportBase() = default;

srs_error_t TransportBase::Open(TransportSink* sink) {
  srs_error_t err = srs_success;
  if (!sink)
    return srs_error_new(ERROR_INVALID_ARGS, "sink is nul");

  if (sink_) {
    sink_ = sink;
    return err;
  }

  sink_ = sink;

  if (srs_success != (err = Open_t())) {
    Close_t();
    sink_ = NULL;
  }
  return err;
}

TransportSink* TransportBase::GetSink() {
  return sink_;
}

int TransportBase::SetOpt(int, void*) {
  return ERROR_INVALID_ARGS;
}

int TransportBase::GetOpt(int, void*) const {
  return ERROR_INVALID_ARGS;
}

int TransportBase::Close() {
  int ret = Close_t();
  sink_ = nullptr;
  return ret;
}

int TransportBase::OnClose(MEDIA_HANDLE, MASK) {
  Close_t();
  TransportSink* copy = sink_;
  sink_ = NULL;

  if (copy)
    copy->OnClose(
        srs_error_new(ERROR_SOCKET_CLOSED, "socket has been closed"));
  return ERROR_SUCCESS;
}

MediaAddress TransportBase::GetPeerAddr() {
  MediaAddress addr;
  if (socket_->GetRemoteAddr(addr) == -1) {
    MLOG_WARN_THIS("get peer address failed! err=" << errno);
  }
  return addr;
}

MediaAddress TransportBase::GetLocalAddr() {
  MediaAddress addr;
  if (socket_->GetLocalAddr(addr) == -1) {
    MLOG_WARN_THIS("get local address failed! err=" << errno);
  }
  return addr;
}

extern NetThreadManager g_netthdmgr;

// class TransportTcp
TransportTcp::TransportTcp(MediaThread* t) 
    : thread_(t) {
  socket_ = std::move(std::make_unique<MediaSocketStream>());
  int ret =
      g_netthdmgr.GetIOBuffer(thread_->GetThreadHandle(), iov_, io_buffer_);
  assert(ret == 0);
}

TransportTcp::~TransportTcp() {
  Close_t();
}

MEDIA_HANDLE TransportTcp::GetHandle() const {
  return socket_->GetHandle();
}

int TransportTcp::OnInput(MEDIA_HANDLE) {
  int nRecv = Recv_i(io_buffer_, MEDIA_SOCK_IOBUFFER_SIZE);
  if (nRecv <= 0)
    return nRecv;

  MessageChain msg(nRecv, io_buffer_,
                   MessageChain::DONT_DELETE | MessageChain::WRITE_LOCKED,
                   nRecv);

  if (sink_)
    return sink_->OnRead(msg);

  return 0;
}

int TransportTcp::OnOutput(MEDIA_HANDLE fd) {
  if (!need_on_send_ || socket_->GetHandle() == MEDIA_INVALID_HANDLE)
    return 0;

  need_on_send_ = false;
  if (sink_)
    return sink_->OnWrite();

  return 0;
}

int TransportTcp::Write(MessageChain& in_msg, bool destroy) {
  if (need_on_send_)
    return ERROR_SOCKET_WOULD_BLOCK;

  const MessageChain* tmpData = &in_msg;
  uint32_t iovNum = 0;
  uint32_t fillLen = 0;
  int sentTotal = 0;
  int rv = 0;

  do {
    iovNum = tmpData->FillIov(iov_, IOV_MAX, fillLen, tmpData);
    if (iovNum == 0)
      break;

    rv = socket_->SendV(iov_, iovNum);
    if (rv < 0) {
      if (errno == EWOULDBLOCK) {
        need_on_send_ = true;
        break;
      } else {
        MLOG_WARN_THIS("sendv failed!"
                      << ", fd=" << socket_->GetHandle()
                      << ", err=" << GetSystemErrorInfo(errno) << ", rv=" << rv
                      << ", fillLen=" << fillLen);
        return ERROR_SOCKET_ERROR;
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
    return ERROR_SOCKET_WOULD_BLOCK;
  }

  if (destroy)
    in_msg.DestroyChained();

  return ERROR_SUCCESS;
}

void TransportTcp::SetSocketHandler(MEDIA_HANDLE handler) {
  socket_->SetHandle(handler);
  if (socket_->Enable(MEDIA_IPC_SAP::NON_BLOCK) == -1) {
    MLOG_ERROR_THIS("Enable(NON_BLOCK) failed! err=" << GetSystemErrorInfo(errno));
  }
}

MEDIA_HANDLE TransportTcp::GetSocketHandler() const {
  return socket_->GetHandle();
}

int TransportTcp::SetBufferSize(bool recv, uint32_t size) {
  if (socket_->SetOpt(SOL_SOCKET, 
      (recv ? SO_RCVBUF : SO_SNDBUF), &size, sizeof(uint32_t)) == -1) {
    return ERROR_SOCKET_ERROR;
  }
  return ERROR_SUCCESS;
}

int TransportTcp::GetBufferSize(bool recv, uint32_t& size) {
  int len = sizeof(uint32_t);
  if (socket_->GetOpt(SOL_SOCKET, 
      (recv ? SO_RCVBUF : SO_SNDBUF), &size, &len) == -1) {
    return ERROR_SOCKET_ERROR;
  }
  return ERROR_SUCCESS;
}

int TransportTcp::SetNodelay(bool enable) {
  int op = enable ? 1 : 0;
  int ret = socket_->SetOpt(IPPROTO_TCP, TCP_NODELAY, &op, sizeof(int));
  if (ret == -1)
    MLOG_ERROR_THIS("set TCP_NODELAY failed! err=" << errno);
  return ret;
}

int TransportTcp::SetTcpCork(bool enable) {
  int op = enable ? 1 : 0;
  int ret = socket_->SetOpt(IPPROTO_TCP, TCP_CORK, &op, sizeof(int));
  if (ret == -1)
    MLOG_ERROR_THIS("set TCP_CORK failed! err=" << errno);
  return ret;
}

srs_error_t TransportTcp::Open_t() {
  return RegisterHandler(MediaHandler::READ_MASK | MediaHandler::WRITE_MASK);
}

int TransportTcp::Close_t() {
  if (socket_->GetHandle() != MEDIA_INVALID_HANDLE) {
    srs_error_t err = RemoveHandler();
    if (err) {
      MLOG_ERROR_THIS("RemoveHandler failed! desc:" << srs_error_desc(err));
      delete err;
    }
    socket_->Close();
  }
  return ERROR_SUCCESS;
}

int TransportTcp::Recv_i(char* buf, uint32_t len) {
  int nRecv = socket_->Recv(buf, len);

  if (nRecv < 0) {
    if (errno == EWOULDBLOCK)
      return -2;
    else {
      ErrnoGuard egTmp;
      MLOG_WARN_THIS("recv() failed! fd=" << socket_->GetHandle()
           << ", err=" << GetSystemErrorInfo(errno));
      return -1;
    }
  }
  if (nRecv == 0) {
    MLOG_WARN_THIS("recv() 0! fd=" << socket_->GetHandle());
    // it is a graceful disconnect
    return -1;
  }
  return nRecv;
}

srs_error_t TransportTcp::RegisterHandler(MediaHandler::MASK mask) {
  srs_error_t err = thread_->Reactor()->RegisterHandler(this, mask);
  if (srs_success != err) {
    return srs_error_wrap(err, "RegisterHandler mask:%d failed!", mask);
  }

  return err;
}

srs_error_t TransportTcp::RemoveHandler() {
  srs_error_t err = srs_success;
  if (thread_) {
    err = thread_->Reactor()->RemoveHandler(this);
    thread_ = nullptr;
    if (srs_success != err) {
      return err;
    }
  }
  return err;
}

// TransportFactory
std::shared_ptr<Transport> TransportFactory::CreateTransport(
    MediaThread* worker, bool tcp) {
  std::shared_ptr<Transport> result;
  if (tcp) {
    result = std::dynamic_pointer_cast<Transport>(std::make_shared<TransportTcp>(worker));
  }
  return std::move(result);
}

}  // namespace ma
