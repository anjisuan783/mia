#include "media_reactor.h"

#include <sys/resource.h>
#include <memory.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <unistd.h>

#include "common/media_log.h"
#include "media_socket.h"

#define RT_BIT_ENABLED(uint32_t, bit) (((uint32_t) & (bit)) != 0)
#define RT_BIT_DISABLED(uint32_t, bit) (((uint32_t) & (bit)) == 0)
#define RT_BIT_CMP_MASK(uint32_t, bit, mask) (((uint32_t) & (bit)) == mask)
#define RT_SET_BITS(uint32_t, bits) (uint32_t |= (bits))
#define RT_CLR_BITS(uint32_t, bits) (uint32_t &= ~(bits))

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

//////////////////////////////////////////////////////////////////////
// class MediaHandler
std::string MediaHandler::GetMaskString(MASK msk) {
  std::string str;
  if (RT_BIT_ENABLED(msk, NULL_MASK))
    str += "NULL_MASK ";
  if (RT_BIT_ENABLED(msk, ACCEPT_MASK))
    str += "ACCEPT_MASK ";
  if (RT_BIT_ENABLED(msk, CONNECT_MASK))
    str += "CONNECT_MASK ";
  if (RT_BIT_ENABLED(msk, READ_MASK))
    str += "READ_MASK ";
  if (RT_BIT_ENABLED(msk, WRITE_MASK))
    str += "WRITE_MASK ";
  if (RT_BIT_ENABLED(msk, EXCEPT_MASK))
    str += "EXCEPT_MASK ";
  if (RT_BIT_ENABLED(msk, TIMER_MASK))
    str += "TIMER_MASK ";
  if (RT_BIT_ENABLED(msk, SHOULD_CALL))
    str += "SHOULD_CALL ";
  if (RT_BIT_ENABLED(msk, CLOSE_MASK))
    str += "CLOSE_MASK ";
  if (RT_BIT_ENABLED(msk, EVENTQUEUE_MASK))
    str += "EVENTQUEUE_MASK ";

  return str;
}

class MediaHandlerRepository {
 public:
  MediaHandlerRepository();
  ~MediaHandlerRepository();

  srs_error_t Open();
  void Close();

  struct CElement {
    MediaHandler* handler_;
    MediaHandler::MASK mask_;

    CElement(MediaHandler* handler = NULL,
             MediaHandler::MASK aMask = MediaHandler::NULL_MASK)
        : handler_(handler), mask_(aMask) {}

    void Clear() {
      handler_ = NULL;
      mask_ = MediaHandler::NULL_MASK;
    }

    bool IsCleared() const { return handler_ == NULL; }
  };

  srs_error_t Find(MEDIA_HANDLE handler, CElement& el);

  srs_error_t Bind(MEDIA_HANDLE handler, const CElement& el);

  srs_error_t UnBind(MEDIA_HANDLE handler);

  bool IsInvaildHandle(MEDIA_HANDLE handler) {
    if (handler >= 0 && handler < max_) {
      return false;
    }
    return true;
  }

  int GetMaxHandlers() { return max_; }

  CElement* GetElement() { return handlers_; }

  static srs_error_t SetRlimit(int resource, int max_num, int& actual_num);

  int FillFdSets(fd_set& fdread, fd_set& fdwrite, fd_set& fdexception);

 private:
  CElement* handlers_ = nullptr;
  int max_ = 0;
};

#define DEFAULT_MAX_SOCKET_BUFSIZE 65535

class MediaPipe {
 public:
  MediaPipe();
  ~MediaPipe();

  srs_error_t Open(int aSize = DEFAULT_MAX_SOCKET_BUFSIZE);
  srs_error_t Close();

  MEDIA_HANDLE GetReadHandle() const;
  MEDIA_HANDLE GetWriteHandle() const;

private:
  MEDIA_HANDLE handles_[2];
};

class MediaReactorEpoll;

class ReactorNotifyPipe : public MediaHandler {
public:
  ReactorNotifyPipe();
  virtual ~ReactorNotifyPipe();

  srs_error_t Open(MediaReactorEpoll *reactor);
  srs_error_t Close();

  // interface MediaHandler
  MEDIA_HANDLE GetHandle() const override;
  int OnInput(MEDIA_HANDLE aFd = MEDIA_INVALID_HANDLE) override;

  srs_error_t Notify(MediaHandler *handler, MediaHandler::MASK mask);
private:
  struct CBuffer {
    CBuffer(MEDIA_HANDLE handler = MEDIA_INVALID_HANDLE,
         MediaHandler::MASK mask = MediaHandler::NULL_MASK)
      : handler_(handler), mask_(mask){ }

    MEDIA_HANDLE handler_;
    MediaHandler::MASK mask_;
  };
  
  MediaPipe notify_;
  MediaReactorEpoll *reactor_ = nullptr;
};

class MediaReactorEpoll : virtual public MediaReactor, 
                          virtual public MediaMsgQueueWithMutex {
  friend class ReactorNotifyPipe;
public:
  MediaReactorEpoll();
  ~MediaReactorEpoll() override;

 srs_error_t Open() override;

  srs_error_t NotifyHandler(MediaHandler*, MediaHandler::MASK);

  srs_error_t RunEventLoop() override;

  void StopEventLoop() override;
  
  srs_error_t Close() override;

  srs_error_t RegisterHandler(
    MediaHandler *handler, 
    MediaHandler::MASK mask) override;

  srs_error_t RemoveHandler(
    MediaHandler *handler, 
    MediaHandler::MASK mask = MediaHandler::ALL_EVENTS_MASK) override;

  srs_error_t Send(MediaMsg *msg) override;
  srs_error_t Post(MediaMsg* msg) override;
  int GetPendingNum() override;

protected:
  srs_error_t OnHandleRegister(MEDIA_HANDLE handler, 
    MediaHandler::MASK mask, MediaHandler *media_handler);
  void OnHandleRemoved(MEDIA_HANDLE handler);

  srs_error_t DoEpollCtl_i(MEDIA_HANDLE handler, MediaHandler::MASK mask, int op);
  
protected:
  void ProcessHandleEvent(MEDIA_HANDLE handler, MediaHandler::MASK mask, 
      int reason, bool is_notify, bool drop = false);

  int RemoveHandleWithoutFinding_i(
      MEDIA_HANDLE handler, 
      const MediaHandlerRepository::CElement &el, 
      MediaHandler::MASK mask);

  MEDIA_HANDLE epoll_;
  struct epoll_event *events_ = nullptr;
  ReactorNotifyPipe notifier_;
  int events_begin_id_ = 0;
  int events_end_id_ = 0;

  // interface MediaTimerQueue
  srs_error_t Schedule(MediaTimerHandler *time_handler, 
            void* arg,
            const MediaTimeValue &interval,
            uint32_t count) override;

  srs_error_t Cancel(MediaTimerHandler *time_handler) override;

  CalendarTQ timer_;
  uint32_t wall_timer_jiffies_  = 0;
  MediaHandlerRepository handler_rep_;
  bool stoppedflag_ = false;
};

////////////////////////////////////////////////////////
//MediaHandler
MEDIA_HANDLE MediaHandler::GetHandle() const {
  MA_ASSERT(!"MediaHandler::GetHandle()");
  return MEDIA_INVALID_HANDLE;
}

int MediaHandler::OnInput(MEDIA_HANDLE) {
  MA_ASSERT(!"MediaHandler::OnInput()");
  return -1;
}

int MediaHandler::OnOutput(MEDIA_HANDLE) {
  MA_ASSERT(!"MediaHandler::OnOutput()");
  return -1;
}

int MediaHandler::OnException(MEDIA_HANDLE) {
  MA_ASSERT(!"MediaHandler::OnException()");
  return -1;
}

int MediaHandler::OnClose(MEDIA_HANDLE, MASK ) {
  MA_ASSERT(!"MediaHandler::OnClose()");
  return -1;
}

///////////////////////////////////////////////////////
//MediaPipe
MediaPipe::MediaPipe() {
  handles_[0] = MEDIA_INVALID_HANDLE;
  handles_[1] = MEDIA_INVALID_HANDLE;
}

MediaPipe::~MediaPipe() {
  srs_error_t err = Close();
  if (err != srs_success) {
    delete err;
  }
}

srs_error_t MediaPipe::Open(int aSize) {
  MA_ASSERT(handles_[0] == MEDIA_INVALID_HANDLE && handles_[1] == MEDIA_INVALID_HANDLE);

  int nRet = 0;
  nRet = ::socketpair(AF_UNIX, SOCK_STREAM, 0, handles_);
  if (nRet == -1) {
    return srs_error_new(ERROR_FAILURE, "socketpair() failed! err:%d, ret:%d", errno, nRet);
  }

  if (aSize > DEFAULT_MAX_SOCKET_BUFSIZE)
    aSize = DEFAULT_MAX_SOCKET_BUFSIZE;

  srs_error_t err = srs_success;

  nRet = ::setsockopt(handles_[0], SOL_SOCKET, SO_RCVBUF, &aSize, sizeof(aSize));
  if (nRet == -1) {
    err = srs_error_new(ERROR_FAILURE, "setsockopt(0) failed, err:%d, ret:%d", errno, nRet);
    goto fail;
  }
  nRet = ::setsockopt(handles_[1], SOL_SOCKET, SO_SNDBUF, &aSize, sizeof(aSize));
  if (nRet == -1) {
    err = srs_error_new(ERROR_FAILURE, "setsockopt(1) failed, err:%d, ret:%d", errno, nRet);
    goto fail;
  }
  return err;

fail:
  Close();
  return err;
}

srs_error_t MediaPipe::Close() {
  int nRet = 0;
  if (handles_[0] != MEDIA_INVALID_HANDLE) {
    nRet = ::close(handles_[0]);
    handles_[0] = MEDIA_INVALID_HANDLE;
  }
  if (handles_[1] != MEDIA_INVALID_HANDLE) {
    nRet |= ::close(handles_[1]);
    handles_[1] = MEDIA_INVALID_HANDLE;
  }
  return nRet == 0 ? srs_success : 
      srs_error_new(ERROR_SOCKET_ERROR, "close handler failed, ret:%d", nRet);
}

MEDIA_HANDLE MediaPipe::GetReadHandle() const {
  return handles_[0];
}

MEDIA_HANDLE MediaPipe::GetWriteHandle() const {
  return handles_[1];
}

///////////////////////////////////
// MediaHandlerRepository
MediaHandlerRepository::MediaHandlerRepository() = default;

MediaHandlerRepository::~MediaHandlerRepository() {
  Close();
}

srs_error_t MediaHandlerRepository::Open() {
  srs_error_t err = srs_success;
  if (handlers_) {
    return err;
  }

  if((err = SetRlimit(RLIMIT_NOFILE, 8192, max_)) != srs_success) {
    return err;
  }

  handlers_ = new CElement[max_];

  return err;
}

void MediaHandlerRepository::Close() {
  if (handlers_) {
    delete[] handlers_;
    handlers_ = NULL;
  }
  max_ = 0;
}

srs_error_t MediaHandlerRepository::SetRlimit(
    int aResource, int aMaxNum, int& aActualNum) {
  srs_error_t err = srs_success;
  rlimit rlCur;
  ::memset(&rlCur, 0, sizeof(rlCur));
  int nRet = ::getrlimit((__rlimit_resource_t)aResource, &rlCur);

  if (nRet == -1 || rlCur.rlim_cur == RLIM_INFINITY) {
    return srs_error_new(ERROR_FAILURE, "getrlimit() failed, err:%d", errno);
  }

  aActualNum = aMaxNum;
  if (aActualNum > static_cast<int>(rlCur.rlim_cur)) {
    rlimit rlNew;
    ::memset(&rlNew, 0, sizeof(rlNew));
    rlNew.rlim_cur = aActualNum;
    rlNew.rlim_max = aActualNum;
    nRet = ::setrlimit((__rlimit_resource_t)aResource,&rlNew);

    if (nRet == -1) {
      if (errno == EPERM) {
        aActualNum = rlCur.rlim_cur;
      } else {
        return srs_error_new(ERROR_FAILURE, "setrlimit() failed, err:%d", errno);
      }
    }
  } else {
    aActualNum = rlCur.rlim_cur;
  }
  return err;
}

static void FdSet_s(fd_set& fdread,
                    fd_set& fdwrite,
                    fd_set&,
                    MediaHandlerRepository::CElement& el_get,
                    int& maxfd) {
  int nSocket = el_get.handler_->GetHandle();
  if (nSocket > maxfd)
    maxfd = nSocket;

  // READ, ACCEPT, and CONNECT flag will place the handle in the read set.
  if (RT_BIT_ENABLED(el_get.mask_, MediaHandler::READ_MASK) ||
      RT_BIT_ENABLED(el_get.mask_, MediaHandler::ACCEPT_MASK)) {
    FD_SET(nSocket, &fdread);
  }
  // WRITE and CONNECT flag will place the handle in the write set.
  if (RT_BIT_ENABLED(el_get.mask_, MediaHandler::WRITE_MASK) ||
      RT_BIT_ENABLED(el_get.mask_, MediaHandler::CONNECT_MASK)) {
    FD_SET(nSocket, &fdwrite);
  }
}

int MediaHandlerRepository::FillFdSets(fd_set& fdread,
                                       fd_set& fdwrite,
                                       fd_set& fdexception) {
  int max_fd = -1;
  for (int i = 0; i < max_; i++) {
    CElement& ele_get = handlers_[i];
    if (!ele_get.IsCleared())
      FdSet_s(fdread, fdwrite, fdexception, ele_get, max_fd);
  }
  return max_fd;
}

srs_error_t MediaHandlerRepository::Find(MEDIA_HANDLE handler, CElement& el) {
  // CAcceptor maybe find fd after closed when program shutting down.
  if (!handlers_) {
    return srs_error_new(ERROR_FAILURE, "not initialize");
  }

  if (IsInvaildHandle(handler)) {
    return srs_error_new(ERROR_INVALID_ARGS, "handler:%d", handler);
  }

  CElement& el_find = handlers_[handler];
  if (el_find.IsCleared()) {
    return srs_error_new(ERROR_NOT_FOUND, "el_find.IsCleared, handler:%d", handler);
  }

  el = el_find;
  return srs_success;
}

srs_error_t MediaHandlerRepository::Bind(MEDIA_HANDLE handler, const CElement& el) {
  if (IsInvaildHandle(handler)) {
    return srs_error_new(ERROR_INVALID_ARGS, "handler:%d", handler);
  }
  if (el.IsCleared()) {
    return srs_error_new(ERROR_NOT_FOUND, "el.IsCleared");
  }

  if (!handlers_) {
    return srs_error_new(ERROR_FAILURE, "not initialize");
  }
  CElement& eleBind = handlers_[handler];

  bool bNotBound = eleBind.IsCleared();
  eleBind = el;

  return bNotBound ? srs_success : srs_error_new(ERROR_EXISTED, "found handler:%d", handler);
}

srs_error_t MediaHandlerRepository::UnBind(MEDIA_HANDLE handler) {
  if (IsInvaildHandle(handler)) {
    return srs_error_new(ERROR_INVALID_ARGS, "handler:%d", handler);
  }

  if (!handlers_) {
    return srs_error_new(ERROR_FAILURE, "not initialize");
  }
  handlers_[handler].Clear();

  return srs_success;
}

///////////////////////////////////////////////////////////////////
//ReactorNotifyPipe
ReactorNotifyPipe::ReactorNotifyPipe() = default;

ReactorNotifyPipe::~ReactorNotifyPipe() {
  Close();
}

srs_error_t ReactorNotifyPipe::Open(MediaReactorEpoll *reactor) {
  srs_error_t err = srs_success;
  MEDIA_IPC_SAP ipcNonblock;
  
  reactor_ = reactor;

  if (srs_success != (err = notify_.Open())) {
    return srs_error_wrap(err, "%s", srs_error_desc(err).c_str());
  }

  ipcNonblock.SetHandle(notify_.GetReadHandle());
  if (ipcNonblock.Enable(MEDIA_IPC_SAP::NON_BLOCK) == -1) {
    return err = srs_error_new(ERROR_SOCKET_ERROR, 
        "Enable(NON_BLOCK) failed! err:", errno);
  }
  
  err = reactor_->RegisterHandler(this, MediaHandler::READ_MASK);
  if (err != srs_success) {
    return srs_error_wrap(err, "reactor register handler READ_MASK failed.");
  }
  
  MLOG_TRACE_THIS("read_fd=" << notify_.GetReadHandle() 
      << " write_fd=" << notify_.GetWriteHandle());
  return err;
}

MEDIA_HANDLE ReactorNotifyPipe::GetHandle() const {
  return notify_.GetReadHandle();
}

int ReactorNotifyPipe::OnInput(MEDIA_HANDLE aFd) {
  MA_ASSERT(aFd == notify_.GetReadHandle());
  
  CBuffer bfNew;
  int nRecv = ::recv(notify_.GetReadHandle(), 
    (char*)&bfNew, sizeof(bfNew), 0);

  if (nRecv < (int)sizeof(bfNew)) {
    MLOG_ERROR_THIS(" nRecv=" << nRecv <<
      " fd=" << notify_.GetReadHandle() << 
      " err=" << errno);
    return 0;
  }

  // we use sigqueue to notify close
  // so that we needn't this pipi to stop the reactor.

  if (bfNew.handler_ == notify_.GetReadHandle())
    return 0;

  if (reactor_)
    reactor_->ProcessHandleEvent(bfNew.handler_, bfNew.mask_, ERROR_SUCCESS, true);

  return 0;
}

srs_error_t ReactorNotifyPipe::
Notify(MediaHandler *handler, MediaHandler::MASK mask) {
  if (notify_.GetWriteHandle() == MEDIA_INVALID_HANDLE) {
    return srs_error_new(ERROR_INVALID_STATE, "not initialized");
  }

  MEDIA_HANDLE fdNew = MEDIA_INVALID_HANDLE;
  if (handler) {
    fdNew = handler->GetHandle();
    MA_ASSERT(fdNew != MEDIA_INVALID_HANDLE);
  }
  
  CBuffer bfNew(fdNew, mask);
  int nSend = ::send(notify_.GetWriteHandle(), 
      (char*)&bfNew, sizeof(bfNew), 0);
  if (nSend < (int)sizeof(bfNew)) {
    return srs_error_new(ERROR_FAILURE, 
        "nSend=%d, fd=%d, errno:%d", nSend, notify_.GetWriteHandle(), errno);
  }
  return srs_success;
}

srs_error_t ReactorNotifyPipe::Close() {
  if (reactor_) {
    srs_error_t err = reactor_->RemoveHandler(this, READ_MASK);
    reactor_ = NULL;
    if(err != srs_success) {
      MLOG_ERROR("reactor RemoveHandler failed" << srs_error_desc(err));
      delete err;
    }
  }
  return notify_.Close();
}

/////////////////////////////////////////////////////////////////
//MediaReactorEpoll
static uint32_t s_dwTimerJiffies;
static bool s_IsTimerSet = false;
const uint32_t g_dwDefaultTimerTickInterval = 30; //millisecond

static void s_TimerTickFun(int) {
  ++s_dwTimerJiffies;
}

MediaReactorEpoll::MediaReactorEpoll()
  : epoll_(MEDIA_INVALID_HANDLE),
    timer_(g_dwDefaultTimerTickInterval, 
                    1000*60*60*2, 
                    static_cast<MediaMsgQueueWithMutex*>(this)) {
}

MediaReactorEpoll::~MediaReactorEpoll() {
  Close();
}

srs_error_t MediaReactorEpoll::Open() {
  stoppedflag_ = false;
  srs_error_t err = handler_rep_.Open();

  if (srs_success != err) {
    return srs_error_wrap(err, "HandlerRepository open failed.");
  }

  MA_ASSERT(handler_rep_.GetMaxHandlers() > 0);

  epoll_ = ::epoll_create(handler_rep_.GetMaxHandlers());
  if (epoll_ < 0) {
    epoll_ = MEDIA_INVALID_HANDLE;
    err = srs_error_new(ERROR_SOCKET_ERROR, 
        "epoll_create() failed, max_handler:%d, err:%d", 
        handler_rep_.GetMaxHandlers(), errno);
    
    return err;
  }

  MA_ASSERT(!events_);
  events_ = new struct epoll_event[handler_rep_.GetMaxHandlers()];
  
  
  if (srs_success != (err = notifier_.Open(this))) {
    return srs_error_wrap(err, "notifier open failed.");
  }

  if (!s_IsTimerSet) {
    if (::signal(SIGALRM, s_TimerTickFun) == SIG_ERR) {
      return srs_error_new(ERROR_FAILURE, "signal(SIGALARM) failed! err%d", errno);
    }
    
    struct itimerval itvInterval;
    itvInterval.it_value.tv_sec = 0;
    itvInterval.it_value.tv_usec = 100;
    itvInterval.it_interval.tv_sec = 0;
    itvInterval.it_interval.tv_usec = g_dwDefaultTimerTickInterval * 1000;
    if (::setitimer(ITIMER_REAL, &itvInterval, NULL) == -1) {
      ::signal(SIGALRM, SIG_IGN);
      return srs_error_new(ERROR_FAILURE, "setitimer() failed! err:%d", errno);
    }
    
    timer_.ResetThead();
    wall_timer_jiffies_  = s_dwTimerJiffies;
    s_IsTimerSet = true;
  }
  
  stoppedflag_ = false;
  MLOG_TRACE_THIS("successful, max_handler=" << handler_rep_.GetMaxHandlers() << 
    " epoll_=" << epoll_);
  return err;
}

srs_error_t MediaReactorEpoll::NotifyHandler(MediaHandler *aEh, MediaHandler::MASK aMask) {
  return notifier_.Notify(aEh, aMask);
}

srs_error_t MediaReactorEpoll::RunEventLoop() {
  while (!stoppedflag_) {
    // <s_dwTimerJiffies> alaways be greater than <wall_timer_jiffies_ > even if it equals 0, 
    // becausae <wall_timer_jiffies_ > is increased following by <s_dwTimerJiffies>.
    uint32_t dwTimerJiffiesTmp = s_dwTimerJiffies;
    uint32_t dwTicks = dwTimerJiffiesTmp - wall_timer_jiffies_ ;
    if( dwTicks >= 0xFFFF0000) {
      MLOG_ERROR_THIS("expected error."
        " dwTimerJiffiesTmp=" << dwTimerJiffiesTmp << 
        " wall_timer_jiffies_ =" << wall_timer_jiffies_  << 
        " dwTicks=" << dwTicks);
      continue;
    }
    if (dwTicks > 33)
      MLOG_ERROR_THIS("time too long."
        " dwTimerJiffiesTmp=" << dwTimerJiffiesTmp << 
        " wall_timer_jiffies_ =" << wall_timer_jiffies_  << 
        " dwTicks=" << dwTicks);
    
    wall_timer_jiffies_  += dwTicks;

    while (dwTicks-- > 0) timer_.TimerTick();

    int nRetFds = ::epoll_wait(epoll_, events_, handler_rep_.GetMaxHandlers(), (int)g_dwDefaultTimerTickInterval);
    if (nRetFds < 0)  {
      if (errno == EINTR) continue;

      return srs_error_new(ERROR_FAILURE, "epoll_wait() failed!"
        " max_handler:%d, epoll:%d, nTimeout:%d, err:%d",
        handler_rep_.GetMaxHandlers(), epoll_, g_dwDefaultTimerTickInterval, errno);
    }

    events_end_id_ = nRetFds;
    struct epoll_event *pEvent = events_;
    for (events_begin_id_ = 0; events_begin_id_ < events_end_id_; events_begin_id_++, pEvent++) {
      int rvError = ERROR_SUCCESS;
      int fdSig = pEvent->data.fd;
      // fdSing may be modified to MEDIA_INVALID_HANDLE due to OnHandleRemoved() function.
      if (fdSig == MEDIA_INVALID_HANDLE) continue;
        
      MediaHandler::MASK maskSig = MediaHandler::NULL_MASK;
      long lSigEvent = pEvent->events;
      if (lSigEvent & (EPOLLERR | EPOLLHUP)) {
        rvError = ERROR_SOCKET_CLOSED;
        RT_SET_BITS(maskSig, MediaHandler::CLOSE_MASK);
      } else {
        if (lSigEvent & EPOLLIN)
          maskSig |= MediaHandler::READ_MASK | MediaHandler::ACCEPT_MASK | MediaHandler::CONNECT_MASK;
        if (lSigEvent & EPOLLOUT)
          maskSig |= MediaHandler::WRITE_MASK | MediaHandler::CONNECT_MASK;
      }
      
      ProcessHandleEvent(fdSig, maskSig, rvError, false);
    }
    
    events_begin_id_ = 0;
    events_end_id_ = 0;
  }
  
  return srs_success;
}

void MediaReactorEpoll::ProcessHandleEvent(MEDIA_HANDLE handler, MediaHandler::MASK mask, 
    int reason, bool is_notify, bool drop) {
  srs_error_t err = srs_success;
  if (handler == MEDIA_INVALID_HANDLE) {
    MA_ASSERT(mask == MediaHandler::EVENTQUEUE_MASK);

    uint32_t dwRemainSize = 0;
    MediaMsgQueueImp::MsgType msgs;
    MediaMsgQueueWithMutex::PopMsgs(msgs, MAX_GET_ONCE, &dwRemainSize);
    MediaMsgQueueImp::Process(msgs);

    if (dwRemainSize) {
      err = NotifyHandler(nullptr, MediaHandler::EVENTQUEUE_MASK);
      if (err != srs_success) {
        MLOG_ERROR_THIS("NotifyHandler error, desc:" << srs_error_desc(err));
        delete err;
      }
    }
    return;
  }

  MediaHandlerRepository::CElement eleFind;
  err = handler_rep_.Find(handler, eleFind);
  if (err != srs_success) {
    if (!drop) {
      MLOG_WARN_THIS("handler not registed."
        " handler=" << handler <<
        " mask=" << MediaHandler::GetMaskString(mask) <<
        " reason=" << reason <<
        " desc=" << srs_error_desc(err));
      delete err;
    }
    return;
  }

  if (RT_BIT_DISABLED(mask, MediaHandler::CLOSE_MASK)) {
    MediaHandler::MASK maskActual = eleFind.mask_ & mask;
    // needn't check the registered mask if it is notify.
    if (!maskActual && !is_notify) {
      MLOG_WARN_THIS("mask not registed."
        " handler=" << handler <<
        " mask=" << MediaHandler::GetMaskString(mask) <<
        " found_mask=" << eleFind.mask_ <<
        " reason=" << reason);
      return;
    }
    
    int nOnCall = 0;
    if (drop && maskActual & MediaHandler::CONNECT_MASK) {
      MLOG_WARN_THIS("drop connect."
        " handler=" << handler <<
        " mask=" << MediaHandler::GetMaskString(mask) <<
        " found_mask=" << eleFind.mask_);
      nOnCall = -1;
    } else {
      if (maskActual & MediaHandler::ACCEPT_MASK
        || maskActual & MediaHandler::READ_MASK) {
        nOnCall = eleFind.handler_->OnInput(handler);
      }
      if ((nOnCall == 0 || nOnCall == -2) && 
        (maskActual & MediaHandler::CONNECT_MASK
        || maskActual & MediaHandler::WRITE_MASK)) {
        nOnCall = eleFind.handler_->OnOutput(handler);
      }
    }

    if (nOnCall == 0) {
    } else if (nOnCall == -2) {
    } else {
      // maybe the handle is reregiested or removed when doing callbacks. 
      // so we have to refind it.
      MediaHandlerRepository::CElement eleFindAgain;
      srs_error_t err = handler_rep_.Find(handler, eleFindAgain);
      if (err == srs_success && eleFind.handler_ == eleFindAgain.handler_) {
        RemoveHandleWithoutFinding_i(handler, eleFindAgain, 
          MediaHandler::ALL_EVENTS_MASK | MediaHandler::SHOULD_CALL);
      }

      if (err) delete err;
    }
  } else {
    RemoveHandleWithoutFinding_i(handler, eleFind, 
      MediaHandler::ALL_EVENTS_MASK | MediaHandler::SHOULD_CALL);
  }
}

int MediaReactorEpoll::
RemoveHandleWithoutFinding_i(MEDIA_HANDLE handler, 
    const MediaHandlerRepository::CElement &el, 
    MediaHandler::MASK mask) {
  MediaHandler::MASK maskNew = mask & MediaHandler::ALL_EVENTS_MASK;
  MediaHandler::MASK mask_el = el.mask_;
  MediaHandler::MASK maskSelect = (mask_el & maskNew) ^ mask_el;
  if (maskSelect == mask_el) {
    MLOG_WARN_THIS("mask is equal. mask=" << mask);
    return ERROR_SUCCESS;
  }

  srs_error_t err = srs_success;

  if (maskSelect == MediaHandler::NULL_MASK) {
    err = handler_rep_.UnBind(handler);
    if (err != srs_success) {
      MLOG_WARN_THIS("UnBind() failed!"
        " handler=" << handler <<
        " aMask=" << MediaHandler::GetMaskString(mask) <<
        " desc=" << srs_error_desc(err));
      delete err;
    }
    
    OnHandleRemoved(handler);
    if (mask & MediaHandler::SHOULD_CALL) 
      el.handler_->OnClose(handler, mask_el);
    
    return ERROR_SUCCESS;
  }
  
  MediaHandlerRepository::CElement eleBind = el;
  eleBind.mask_ = maskSelect;
  err = handler_rep_.Bind(handler, eleBind);
  MA_ASSERT(srs_error_code(err) == ERROR_EXISTED);
  delete err;
  return ERROR_EXISTED;
}

void MediaReactorEpoll::StopEventLoop() {
  MLOG_TRACE_THIS("");
  // this function can be invoked in the different thread.
  MediaMsgQueueImp::Stop();
}

srs_error_t MediaReactorEpoll::Close() {
  if (s_IsTimerSet) {
    if (::signal(SIGALRM, SIG_IGN) == SIG_ERR) 
      MLOG_ERROR_THIS("signal(SIGALARM) failed! err=" << errno);
    
    struct itimerval itvInterval;
    itvInterval.it_value.tv_sec = 0;
    itvInterval.it_value.tv_usec = 0;
    itvInterval.it_interval.tv_sec = 0;
    itvInterval.it_interval.tv_usec = 0;

    if (::setitimer(ITIMER_REAL, &itvInterval, NULL) == -1)
      MLOG_ERROR_THIS("setitimer() failed! err=" << errno);
    s_IsTimerSet = false;
  }

  wall_timer_jiffies_  = 0;

  if (events_) {
    delete []events_;
    events_ = NULL;
  }
  
  srs_error_t err = notifier_.Close();

  if (epoll_ != MEDIA_INVALID_HANDLE) {
    ::close(epoll_);
    epoll_ = MEDIA_INVALID_HANDLE;
  }

  MediaMsgQueueImp::DestoryPendingMsgs();
  handler_rep_.Close();
  return err;
}

srs_error_t MediaReactorEpoll::OnHandleRegister(
    MEDIA_HANDLE aFd, MediaHandler::MASK aMask, MediaHandler *aEh) {
  if (epoll_== MEDIA_INVALID_HANDLE) {
    return srs_error_new(ERROR_INVALID_STATE, "epoll not initialized");
  }

  // Need NOT do CheckPollIn() because epoll_ctl() will do it interval.
  return DoEpollCtl_i(aFd, aMask, EPOLL_CTL_ADD);
}

void MediaReactorEpoll::OnHandleRemoved(MEDIA_HANDLE aFd) {
  if (epoll_==MEDIA_INVALID_HANDLE) {
    MLOG_WARN_THIS("epoll not initialized!");
    return;
  }

  if (::epoll_ctl(epoll_, EPOLL_CTL_DEL, aFd, NULL) < 0) {
    MLOG_ERROR_THIS("epoll_ctl() failed!"
      " epoll_=" << epoll_ << 
      " aFd=" << aFd << 
      " err=" << errno);
  }
  
  if (events_end_id_ == 0)
    return;

  int i = events_begin_id_ + 1;
  struct epoll_event *pEvent = events_ + i;
  for ( ; i < events_end_id_; i++, pEvent++) {
    if (pEvent->data.fd == aFd) {
      MLOG_WARN_THIS("find same fd=" << aFd << 
        " events_begin_id_=" << events_begin_id_ << 
        " events_end_id_=" << events_end_id_ << 
        " i=" << i);
      pEvent->data.fd = MEDIA_INVALID_HANDLE;
      return;
    }
  }
}

srs_error_t MediaReactorEpoll::
RegisterHandler(MediaHandler *handler, MediaHandler::MASK mask) {
  srs_error_t err = srs_success;
  
  MediaHandler::MASK maskNew = mask & MediaHandler::ALL_EVENTS_MASK;
  if (maskNew == MediaHandler::NULL_MASK) {
    return srs_error_new(ERROR_INVALID_ARGS, "NULL_MASK. mask:%s", 
        MediaHandler::GetMaskString(mask).c_str());
  }
  
  MediaHandlerRepository::CElement eleFind;
  MEDIA_HANDLE fdNew = handler->GetHandle();
  err = handler_rep_.Find(fdNew, eleFind);

  if (srs_success != err) {
    if (srs_error_code(err) != ERROR_NOT_FOUND) {
      return srs_error_wrap(err, "found handler.");
    }
    delete err;
    err = srs_success;
  }

  if (maskNew == eleFind.mask_ && handler == eleFind.handler_) {
    return err;
  }

  if (eleFind.IsCleared()) {
    // needn't remove handle when OnHandleRegister() failed
    // because the handle didn't be inserted at all
    if ((err = OnHandleRegister(fdNew, maskNew, handler)) != srs_success)
      return err;
  }
  
  MediaHandlerRepository::CElement eleNew(handler, maskNew);
  err = handler_rep_.Bind(fdNew, eleNew);

  if (srs_error_code(err) == ERROR_EXISTED) {
    delete err;
    err = DoEpollCtl_i(handler->GetHandle(), mask, EPOLL_CTL_MOD);
  }
  return err;
}

srs_error_t MediaReactorEpoll::
RemoveHandler(MediaHandler *aEh, MediaHandler::MASK aMask) { 
  MediaHandler::MASK maskNew = aMask & MediaHandler::ALL_EVENTS_MASK;
  if (maskNew == MediaHandler::NULL_MASK) {
    return srs_error_new(ERROR_INVALID_ARGS, "NULL_MASK. mask:%s",  
        MediaHandler::GetMaskString(aMask).c_str());
  }
  
  MediaHandlerRepository::CElement eleFind;
  MEDIA_HANDLE fdNew = aEh->GetHandle();
  srs_error_t err = handler_rep_.Find(fdNew, eleFind);

  if (err != srs_success)
    return err;
  
  int rv = RemoveHandleWithoutFinding_i(fdNew, eleFind, maskNew);
  if (rv == ERROR_EXISTED) {
    err = DoEpollCtl_i(aEh->GetHandle(), aMask, EPOLL_CTL_MOD);
  }
  return err;
}

srs_error_t MediaReactorEpoll::DoEpollCtl_i(
    MEDIA_HANDLE handler, MediaHandler::MASK mask, int aOperation) {
  struct epoll_event epEvent;
  ::memset(&epEvent, 0, sizeof(epEvent));
  epEvent.events = EPOLLERR | EPOLLHUP;
  if(!(mask & MediaHandler::ACCEPT_MASK)) //Add 17/02 2006 accept with level model
    epEvent.events |= EPOLLET;
  
  epEvent.data.fd = handler;

  if (mask & MediaHandler::CONNECT_MASK)
    epEvent.events |= EPOLLIN | EPOLLOUT;
  if (mask & MediaHandler::READ_MASK || mask & MediaHandler::ACCEPT_MASK)
    epEvent.events |= EPOLLIN;
  if (mask & MediaHandler::WRITE_MASK)
    epEvent.events |= EPOLLOUT;

  srs_error_t err = srs_success;

  if (::epoll_ctl(epoll_, aOperation, handler, &epEvent) < 0) {
    err = srs_error_new(ERROR_SOCKET_ERROR, 
        "epoll_ctl() failed, epoll_:%d, hander:%d, op:%d, err:%d",
        epoll_, handler, aOperation, errno);
  }

  return err;
}

srs_error_t MediaReactorEpoll::Schedule(MediaTimerHandler *handler, void* arg, 
    const MediaTimeValue &interval, uint32_t count) {
  return timer_.Schedule(handler, arg, interval, count);
}

srs_error_t MediaReactorEpoll::Cancel(MediaTimerHandler *handler) {
  return timer_.Cancel(handler);
}

srs_error_t MediaReactorEpoll::Send(MediaMsg* msg) {
  return MediaMsgQueueWithMutex::Send(msg);
}

srs_error_t MediaReactorEpoll::Post(MediaMsg* msg) {
  uint32_t dwOldSize = 0;
  srs_error_t err = srs_success;
  err = MediaMsgQueueWithMutex::PostWithOldSize(msg, &dwOldSize);
  if (err == srs_success && dwOldSize == 0)
    return NotifyHandler(NULL, MediaHandler::EVENTQUEUE_MASK);
  return err;
}

int MediaReactorEpoll::GetPendingNum() {
  return MediaMsgQueueWithMutex::GetPendingNum();
}

// CreateReactor
MediaReactor* CreateReactor() {
  return new MediaReactorEpoll;
}

} //namespace ma
