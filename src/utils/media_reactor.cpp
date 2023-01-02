#include "media_network.h"

#include <sys/resource.h>
#include <memory.h>
#include "common/media_log.h"

#define RT_BIT_ENABLED(dword, bit) (((dword) & (bit)) != 0)
#define RT_BIT_DISABLED(dword, bit) (((dword) & (bit)) == 0)
#define RT_BIT_CMP_MASK(dword, bit, mask) (((dword) & (bit)) == mask)
#define RT_SET_BITS(dword, bits) (dword |= (bits))
#define RT_CLR_BITS(dword, bits) (dword &= ~(bits))

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
  srs_error_t Close();

  struct CElement {
    MediaHandler* m_pEh;
    MediaHandler::MASK m_Mask;

    CElement(MediaHandler* aEh = NULL,
             MediaHandler::MASK aMask = MediaHandler::NULL_MASK)
        : m_pEh(aEh), m_Mask(aMask) {}

    void Clear() {
      m_pEh = NULL;
      m_Mask = MediaHandler::NULL_MASK;
    }

    bool IsCleared() const { return m_pEh == NULL; }
  };

  srs_error_t Find(MEDIA_HANDLE aFd, CElement& aEle);

  srs_error_t Bind(MEDIA_HANDLE aFd, const CElement& aEle);

  srs_error_t UnBind(MEDIA_HANDLE aFd);

  bool IsVaildHandle(MEDIA_HANDLE aFd) {
    if (aFd >= 0 && aFd < m_nMaxHandler)
      return true;
    else
      return false;
  }

  int GetMaxHandlers() { return m_nMaxHandler; }

  CElement* GetElement() { return m_pHandlers; }

  static srs_error_t SetRlimit(int aResource, int aMaxNum, int& aActualNum);

  int FillFdSets(fd_set& aFsRead, fd_set& aFsWrite, fd_set& aFsException);

 private:
  CElement* m_pHandlers = nullptr;
  int m_nMaxHandler = 0;
};

#define DEFAULT_MAX_SOCKET_BUFSIZE 65535

class MediaPipe {
 public:
  MediaPipe();
  ~MediaPipe();

  int Open(int aSize = DEFAULT_MAX_SOCKET_BUFSIZE);
  int Close();

  MEDIA_HANDLE GetReadHandle() const;
  MEDIA_HANDLE GetWriteHandle() const;

private:
  MEDIA_HANDLE m_Handles[2];
};

class MediaReactorEpoll;

class ReactorNotifyPipe : public MediaHandler {
public:
  ReactorNotifyPipe();
  virtual ~ReactorNotifyPipe();

  int Open(MediaReactorEpoll *aReactor);
  int Close();

  // interface MediaHandler
  MEDIA_HANDLE GetHandle() const override;
  int OnInput(MEDIA_HANDLE aFd = MEDIA_INVALID_HANDLE) override;

  int Notify(MediaHandler *aEh, MediaHandler::MASK aMask);
private:
  struct CBuffer {
    CBuffer(MEDIA_HANDLE aFd = MEDIA_INVALID_HANDLE,
         MediaHandler::MASK aMask = MediaHandler::NULL_MASK)
      : m_Fd(aFd), m_Mask(aMask){ }

    MEDIA_HANDLE m_Fd;
    MediaHandler::MASK m_Mask;
  };
  
  MediaPipe m_PipeNotify;
  MediaReactorEpoll *m_pReactor = nullptr;
};

class MediaReactorEpoll : virtual public MediaReactor, 
                          virtual public MediaMsgQueueWithMutex {
public:
  MediaReactorEpoll();
  ~MediaReactorEpoll() override;

 srs_error_t Open() override;

  srs_error_t NotifyHandler(
    MediaHandler *aEh, 
    MediaHandler::MASK aMask);

  srs_error_t RunEventLoop() override;

  srs_error_t StopEventLoop() override;
  
  srs_error_t Close() override;

  srs_error_t RegisterHandler(
    MediaHandler *aEh, 
    MediaHandler::MASK aMask) override;

  srs_error_t RemoveHandler(
    MediaHandler *aEh, 
    MediaHandler::MASK aMask = MediaHandler::ALL_EVENTS_MASK) override;

  srs_error_t Send(MediaMsg *msg) override;
  srs_error_t Post(MediaMsg* msg) override;
  int GetPendingNum() override;

protected:
  virtual int OnHandleRegister(MEDIA_HANDLE aFd, 
    MediaHandler::MASK aMask, MediaHandler *aEh);
  virtual void OnHandleRemoved(MEDIA_HANDLE aFd);

  int DoEpollCtl_i(MEDIA_HANDLE aFd, MediaHandler::MASK aMask, int aOperation);
  
protected:
  int ProcessHandleEvent(MEDIA_HANDLE aFd, MediaHandler::MASK aMask, 
           int aReason, bool aIsNotify, bool aDropConnect = false);

  int RemoveHandleWithoutFinding_i(
    MEDIA_HANDLE aFd, 
    const MediaHandlerRepository::CElement &aHe, 
    MediaHandler::MASK aMask);

  MEDIA_HANDLE m_fdEpoll;
  struct epoll_event *m_pEvents = nullptr;
  ReactorNotifyPipe m_Notify;
  int m_nEventsBeginIndex = 0;
  int m_nEventsEndIndex = 0;

  // interface MediaTimerQueue
  srs_error_t Schedule(MediaTimerHandler *aTh, 
            void* aArg,
            const MediaTimeValue &aInterval,
            uint32_t aCount) override;

  srs_error_t Cancel(MediaTimerHandler *aTh);

  CalendarTQ m_CalendarTimer;
  uint32_t m_dwWallTimerJiffies = 0;

  MediaHandlerRepository m_EhRepository;

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
  m_Handles[0] = MEDIA_INVALID_HANDLE;
  m_Handles[1] = MEDIA_INVALID_HANDLE;
}

MediaPipe::~MediaPipe() {
  Close();
}

int MediaPipe::Open(int aSize) {
  MA_ASSERT(m_Handles[0] == MEDIA_INVALID_HANDLE && m_Handles[1] == MEDIA_INVALID_HANDLE);

  int nRet = 0;
  nRet = ::socketpair(AF_UNIX, SOCK_STREAM, 0, m_Handles);
  if (nRet == -1) {
    MLOG_ERROR_THIS("socketpair() failed! err=" << errno);
    return nRet;
  }

  if (aSize > DEFAULT_MAX_SOCKET_BUFSIZE)
    aSize = DEFAULT_MAX_SOCKET_BUFSIZE;

  nRet = ::setsockopt(m_Handles[0], SOL_SOCKET, SO_RCVBUF, &aSize, sizeof(aSize));
  if (nRet == -1) {
    MLOG_ERROR_THIS("setsockopt(0) failde! err=" << errno);
    goto fail;
  }
  nRet = ::setsockopt(m_Handles[1], SOL_SOCKET, SO_SNDBUF, &aSize, sizeof(aSize));
  if (nRet == -1) {
    MLOG_ERROR_THIS("setsockopt(1) failde! err=" << errno);
    goto fail;
  }
  return ERROR_SUCCESS;

fail:
  Close();
  return ERROR_SYSTEM_FAILURE;
}

int MediaPipe::Close() {
  int nRet = 0;
  if (m_Handles[0] != MEDIA_INVALID_HANDLE) {
    nRet = ::close(m_Handles[0]);
    m_Handles[0] = MEDIA_INVALID_HANDLE;
  }
  if (m_Handles[1] != MEDIA_INVALID_HANDLE) {
    nRet |= ::close(m_Handles[1]);
    m_Handles[1] = MEDIA_INVALID_HANDLE;
  }
  return nRet == 0 ? ERROR_SUCCESS : ERROR_SOCKET_ERROR;
}

MEDIA_HANDLE MediaPipe::GetReadHandle() const {
  return m_Handles[0];
}

MEDIA_HANDLE MediaPipe::GetWriteHandle() const {
  return m_Handles[1];
}

///////////////////////////////////
// MediaHandlerRepository
MediaHandlerRepository::MediaHandlerRepository() = default;

MediaHandlerRepository::~MediaHandlerRepository() {
  Close();
}

srs_error_t MediaHandlerRepository::Open() {
  srs_error_t err = srs_success;
  if (m_pHandlers) {
    return err;
  }

  if((err = SetRlimit(RLIMIT_NOFILE, 8192, m_nMaxHandler)) != srs_success) {
    return err;
  }

  m_pHandlers = new CElement[m_nMaxHandler];

  return err;
}

srs_error_t MediaHandlerRepository::Close() {
  if (m_pHandlers) {
    delete[] m_pHandlers;
    m_pHandlers = NULL;
  }
  m_nMaxHandler = 0;
  return srs_success;
}

srs_error_t MediaHandlerRepository::SetRlimit(
    int aResource, int aMaxNum, int& aActualNum) {
  srs_error_t err = srs_success;
  rlimit rlCur;
  ::memset(&rlCur, 0, sizeof(rlCur));
  int nRet = ::getrlimit((__rlimit_resource_t)aResource, &rlCur);

  if (nRet == -1 || rlCur.rlim_cur == RLIM_INFINITY) {
    return srs_error_new(ERROR_SYSTEM_FAILURE, "getrlimit() failed, err:%d", errno);
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
        return srs_error_new(ERROR_SYSTEM_FAILURE, "setrlimit() failed, err:%d", errno);
      }
    }
  } else {
    aActualNum = rlCur.rlim_cur;
  }
  return err;
}

static void FdSet_s(fd_set& aFsRead,
                    fd_set& aFsWrite,
                    fd_set& aFsException,
                    MediaHandlerRepository::CElement& aEleGet,
                    int& aMaxFd) {
  int nSocket = (int)aEleGet.m_pEh->GetHandle();
  if (nSocket > aMaxFd)
    aMaxFd = nSocket;

  // READ, ACCEPT, and CONNECT flag will place the handle in the read set.
  if (RT_BIT_ENABLED(aEleGet.m_Mask, MediaHandler::READ_MASK) ||
      RT_BIT_ENABLED(aEleGet.m_Mask, MediaHandler::ACCEPT_MASK)) {
    FD_SET(nSocket, &aFsRead);
  }
  // WRITE and CONNECT flag will place the handle in the write set.
  if (RT_BIT_ENABLED(aEleGet.m_Mask, MediaHandler::WRITE_MASK) ||
      RT_BIT_ENABLED(aEleGet.m_Mask, MediaHandler::CONNECT_MASK)) {
    FD_SET(nSocket, &aFsWrite);
  }
}

int MediaHandlerRepository::FillFdSets(fd_set& aFsRead,
                                       fd_set& aFsWrite,
                                       fd_set& aFsException) {
  int nMaxFd = -1;
  for (int i = 0; i < m_nMaxHandler; i++) {
    CElement& eleGet = m_pHandlers[i];
    if (!eleGet.IsCleared())
      FdSet_s(aFsRead, aFsWrite, aFsException, eleGet, nMaxFd);
  }
  return nMaxFd;
}

srs_error_t MediaHandlerRepository::Find(MEDIA_HANDLE aFd, CElement& aEle) {
  // CAcceptor maybe find fd after closed when program shutting down.
  if (!m_pHandlers) {
    return srs_error_new(ERROR_SYSTEM_FAILURE, "not initialize");
  }

  if (IsVaildHandle(aFd)) {
    return srs_error_new(ERROR_SYSTEM_INVALID_ARGS, "fd:%d", aFd);
  }

  CElement& eleFind = m_pHandlers[aFd];
  if (eleFind.IsCleared()) {
    return srs_error_new(ERROR_SYSTEM_NOT_FOUND, "eleFind.IsCleared");
  }

  aEle = eleFind;
  return srs_success;
}

srs_error_t MediaHandlerRepository::Bind(MEDIA_HANDLE aFd, const CElement& aEle) {
  if (IsVaildHandle(aFd)) {
    return srs_error_new(ERROR_SYSTEM_INVALID_ARGS, "fd:%d", aFd);
  }
  if (aEle.IsCleared()) {
    return srs_error_new(ERROR_SYSTEM_NOT_FOUND, "aEle.IsCleared");
  }

  if (!m_pHandlers) {
    return srs_error_new(ERROR_SYSTEM_FAILURE, "not initialize");
  }
  CElement& eleBind = m_pHandlers[aFd];

  bool bNotBound = eleBind.IsCleared();
  eleBind = aEle;

  return bNotBound ? srs_success : srs_error_new(ERROR_SYSTEM_EXISTED, "found aFd:%d", aFd);
}

srs_error_t MediaHandlerRepository::UnBind(MEDIA_HANDLE aFd) {
  if (IsVaildHandle(aFd)) {
    return srs_error_new(ERROR_SYSTEM_INVALID_ARGS, "fd:%d", aFd);
  }

  if (!m_pHandlers) {
    return srs_error_new(ERROR_SYSTEM_FAILURE, "not initialize");
  }
  m_pHandlers[aFd].Clear();

  return srs_success;
}

///////////////////////////////////////////////////////////////////
//ReactorNotifyPipe
ReactorNotifyPipe::ReactorNotifyPipe() = default;

ReactorNotifyPipe::~ReactorNotifyPipe() {
  Close();
}

int ReactorNotifyPipe::Open(MediaReactorEpoll *aReactor) {
  RT_IPC_SAP ipcNonblock;
  
  m_pReactor = aReactor;

  int rv = m_PipeNotify.Open();
  if (rv != ERROR_SUCCESS) 
    goto fail;

  ipcNonblock.SetHandle(m_PipeNotify.GetReadHandle());
  if (ipcNonblock.Enable(RT_IPC_SAP::NON_BLOCK) == -1) {
    MLOG_ERROR_THIS("Enable(NON_BLOCK) failed! err=" << errno);
    rv = ERROR_SOCKET_ERROR;
    goto fail;
  }
  
  rv = m_pReactor->RegisterHandler(this, MediaHandler::READ_MASK);
  if (rv != ERROR_SUCCESS) 
    goto fail;
  
  MLOG_TRACE_THIS("read_fd=" << m_PipeNotify.GetReadHandle() << " write_fd=" << m_PipeNotify.GetWriteHandle());
  return ERROR_SUCCESS;

fail:
  Close();
  return rv;
}

MEDIA_HANDLE ReactorNotifyPipe::GetHandle() const {
  return m_PipeNotify.GetReadHandle();
}

int ReactorNotifyPipe::OnInput(MEDIA_HANDLE aFd) {
  MA_ASSERT(aFd == m_PipeNotify.GetReadHandle());
  
  CBuffer bfNew;
  int nRecv = ::recv(m_PipeNotify.GetReadHandle(), 
    (char*)&bfNew, sizeof(bfNew), 0);

  if (nRecv < (int)sizeof(bfNew)) {
    MLOG_ERROR_TRACE(" nRecv=" << nRecv <<
      " fd=" << m_PipeNotify.GetReadHandle() << 
      " err=" << errno);
    return 0;
  }

  // we use sigqueue to notify close
  // so that we needn't this pipi to stop the reactor.

  if (bfNew.m_Fd == m_PipeNotify.GetReadHandle())
    return 0;

  if (m_pReactor)
    m_pReactor->ProcessHandleEvent(bfNew.m_Fd, bfNew.m_Mask, ERROR_SUCCESS, true);

  return 0;
}

int ReactorNotifyPipe::
Notify(MediaHandler *aEh, MediaHandler::MASK aMask) {
  // this function can be invoked in the different thread.
  if (m_PipeNotify.GetWriteHandle() == MEDIA_INVALID_HANDLE) {
    return ERROR_SYSTEM_INVALID_STATE;
  }

  MEDIA_HANDLE fdNew = MEDIA_INVALID_HANDLE;
  if (aEh) {
    fdNew = aEh->GetHandle();
    RT_ASSERTE(fdNew != MEDIA_INVALID_HANDLE);
  }
  
  CBuffer bfNew(fdNew, aMask);
  int nSend = ::send(m_PipeNotify.GetWriteHandle(), 
      (char*)&bfNew, sizeof(bfNew), 0);
  if (nSend < (int)sizeof(bfNew)) {
    MLOG_ERROR_TRACE(" nSend=" << nSend <<
      " fd=" << m_PipeNotify.GetWriteHandle() <<
      " err=" << errno);
    return ERROR_SYSTEM_FAILURE;
  }
  return ERROR_SUCCESS;
}

int ReactorNotifyPipe::Close() {
  if (m_pReactor) {
    m_pReactor->RemoveHandler(this, READ_MASK);
    m_pReactor = NULL;
  }
  return m_PipeNotify.Close();
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
  : m_fdEpoll(MEDIA_INVALID_HANDLE),
    m_CalendarTimer(g_dwDefaultTimerTickInterval, 
                    1000*60*60*2, 
                    static_cast<MediaMsgQueueWithMutex*>(this)) {
}

MediaReactorEpoll::~MediaReactorEpoll() {
  Close();
}

srs_error_t MediaReactorEpoll::Open() {
  int rv;
  MediaMsgQueueWithMutex::ResetThd();
  stoppedflag_ = false;
  rv = m_EhRepository.Open();

  if (RT_FAILED(rv))
    goto fail;

  MA_ASSERT(m_EhRepository.GetMaxHandlers() > 0);

  m_fdEpoll = ::epoll_create(m_EhRepository.GetMaxHandlers());
  if (m_fdEpoll < 0) {
    RT_ERROR_TRACE_THIS("MediaReactorEpoll::Open, epoll_create() failed!"
      " max_handler=" << m_EhRepository.GetMaxHandlers() << 
      " m_fdEpoll=" << m_fdEpoll << 
      " err=" << errno);
    m_fdEpoll = MEDIA_INVALID_HANDLE;
    rv = RT_ERROR_FAILURE;
    goto fail;
  }

  RT_ASSERTE(!m_pEvents);
  m_pEvents = new struct epoll_event[m_EhRepository.GetMaxHandlers()];
  
  rv = m_Notify.Open(this);
  if (RT_FAILED(rv))
    goto fail;

  if (!s_IsTimerSet)
  {
    if (::signal(SIGALRM, s_TimerTickFun) == SIG_ERR) 
    {
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::Open, signal(SIGALARM) failed! err=" << errno);
      rv = RT_ERROR_FAILURE;
      goto fail;
    }
    
    struct itimerval itvInterval;
    itvInterval.it_value.tv_sec = 0;
    itvInterval.it_value.tv_usec = 100;
    itvInterval.it_interval.tv_sec = 0;
    itvInterval.it_interval.tv_usec = g_dwDefaultTimerTickInterval * 1000;
    if (::setitimer(ITIMER_REAL, &itvInterval, NULL) == -1) 
    {
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::Open, setitimer() failed! err=" << errno);
      rv = RT_ERROR_FAILURE;
      goto fail;
    }
    
    m_CalendarTimer.m_Est.Reset2CurrentThreadId();
    m_dwWallTimerJiffies = s_dwTimerJiffies;
    s_IsTimerSet = true;
  }
  
  CRtStopFlag::SetStartFlag();
  RT_DEBUG_TRACE_THIS("MediaReactorEpoll::Open, successful,"
    " max_handler=" << m_EhRepository.GetMaxHandlers() << 
    " m_fdEpoll=" << m_fdEpoll);
  return RT_OK;

fail:
  Close();
  return rv;
}

int MediaReactorEpoll::NotifyHandler(MediaHandler *aEh, MediaHandler::MASK aMask)
{
  return m_Notify.Notify(aEh, aMask);
}

int MediaReactorEpoll::RunEventLoop()
{
  RT_DEBUG_TRACE_THIS("MediaReactorEpoll::RunEventLoop");
  Est_.EnsureSingleThread();

  while (!CRtStopFlag::IsFlagStopped()) 
  {
    // <s_dwTimerJiffies> alaways be greater than <m_dwWallTimerJiffies> even if it equals 0, 
    // becausae <m_dwWallTimerJiffies> is increased following by <s_dwTimerJiffies>.
    DWORD dwTimerJiffiesTmp = s_dwTimerJiffies;
    DWORD dwTicks = dwTimerJiffiesTmp - m_dwWallTimerJiffies;
    if( dwTicks >= 0xFFFF0000)
    {
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::RunEventLoop,  expected error."
        " dwTimerJiffiesTmp=" << dwTimerJiffiesTmp << 
        " m_dwWallTimerJiffies=" << m_dwWallTimerJiffies << 
        " dwTicks=" << dwTicks);
      continue;
    }
    if (dwTicks > 33)
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::RunEventLoop, time too long."
        " dwTimerJiffiesTmp=" << dwTimerJiffiesTmp << 
        " m_dwWallTimerJiffies=" << m_dwWallTimerJiffies << 
        " dwTicks=" << dwTicks);
    
    m_dwWallTimerJiffies += dwTicks;

    while (dwTicks-- > 0) m_CalendarTimer.TimerTick();

    int nRetFds = ::epoll_wait(m_fdEpoll, m_pEvents, m_EhRepository.GetMaxHandlers(), (int)g_dwDefaultTimerTickInterval);
    if (nRetFds < 0) 
    {
      if (errno == EINTR) continue;
      
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::RunEventLoop, epoll_wait() failed!"
        " max_handler=" << m_EhRepository.GetMaxHandlers() << 
        " m_fdEpoll=" << m_fdEpoll << 
        " nTimeout=" << g_dwDefaultTimerTickInterval << 
        " err=" << errno);
      return RT_ERROR_FAILURE; 
    }

    m_nEventsEndIndex = nRetFds;
    struct epoll_event *pEvent = m_pEvents;
    for (m_nEventsBeginIndex = 0; m_nEventsBeginIndex < m_nEventsEndIndex; m_nEventsBeginIndex++, pEvent++) 
    {
      // TODO: 
      // 1. test UDP, packets merge?
      // 2. test send data before wait.
      // 3. test SSL, read return value less than parameter
      // 4. PUSH & FIN before epoll_wait().
      // 5. PUSH & FIN before epoll_ctl().

      int rvError = RT_OK;
      int fdSig = pEvent->data.fd;
      // fdSing may be modified to MEDIA_INVALID_HANDLE due to OnHandleRemoved() function.
      if (fdSig == MEDIA_INVALID_HANDLE) continue;
        
      MediaHandler::MASK maskSig = MediaHandler::NULL_MASK;
      long lSigEvent = pEvent->events;
      if (lSigEvent & (EPOLLERR | EPOLLHUP)) 
      {
        rvError = RT_ERROR_NETWORK_SOCKET_CLOSE;
        RT_SET_BITS(maskSig, MediaHandler::CLOSE_MASK);
      }
      else 
      {
        if (lSigEvent & EPOLLIN)
          maskSig |= MediaHandler::READ_MASK | MediaHandler::ACCEPT_MASK | MediaHandler::CONNECT_MASK;
        if (lSigEvent & EPOLLOUT)
          maskSig |= MediaHandler::WRITE_MASK | MediaHandler::CONNECT_MASK;
      }
      
      ProcessHandleEvent(fdSig, maskSig, rvError, false);
    }
    
    m_nEventsBeginIndex = 0;
    m_nEventsEndIndex = 0;
  }
  
  return RT_OK;
}

int MediaReactorEpoll::ProcessHandleEvent(MEDIA_HANDLE aFd, MediaHandler::MASK aMask, 
           int aReason, bool aIsNotify, bool aDropConnect)
{
  Est_.EnsureSingleThread();
  if (aFd == MEDIA_INVALID_HANDLE) 
  {
    RT_ASSERTE(aMask == MediaHandler::EVENTQUEUE_MASK);

    DWORD dwRemainSize = 0;
    CRtEventQueueBase::EventsType listEvents;
    int rv = CRtEventQueueUsingMutex::PopPendingEventsWithoutWait(
      listEvents, CRtEventQueueBase::MAX_GET_ONCE, &dwRemainSize);
    if (RT_SUCCEEDED(rv))
      rv = CRtEventQueueBase::ProcessEvents(listEvents);

    if (dwRemainSize)
      NotifyHandler(NULL, MediaHandler::EVENTQUEUE_MASK);
    return rv;
  }

#ifndef RT_DISABLE_EVENT_REPORT
  CRtTimeValue tvCur = CRtTimeValue::GetTimeOfDay();
#endif // !RT_DISABLE_EVENT_REPORT

  MediaHandlerRepository::CElement eleFind;
  int rv = m_EhRepository.Find(aFd, eleFind);
  if (RT_FAILED(rv)) 
  {
    if (!aDropConnect) 
    {
      RT_WARNING_TRACE("MediaReactorEpoll::ProcessHandleEvent, handle not registed."
        " aFd=" << aFd <<
        " aMask=" << MediaHandler::GetMaskString(aMask) <<
        " aReason=" << aReason <<
        " rv=" << rv);
    }
    return rv;
  }

  if (RT_BIT_DISABLED(aMask, MediaHandler::CLOSE_MASK)) 
  {
    MediaHandler::MASK maskActual = eleFind.m_Mask & aMask;
    // needn't check the registered mask if it is notify.
    if (!maskActual && !aIsNotify) {
      RT_WARNING_TRACE("MediaReactorEpoll::ProcessHandleEvent, mask not registed."
        " aFd=" << aFd <<
        " aMask=" << MediaHandler::GetMaskString(aMask) <<
        " m_Mask=" << eleFind.m_Mask <<
        " aReason=" << aReason);
      return RT_OK;
    }
    
    int nOnCall = 0;
    if (aDropConnect && maskActual & MediaHandler::CONNECT_MASK)
    {
      RT_WARNING_TRACE("MediaReactorEpoll::ProcessHandleEvent, drop connect."
        " aFd=" << aFd <<
        " aMask=" << MediaHandler::GetMaskString(aMask) <<
        " m_Mask=" << eleFind.m_Mask);
      nOnCall = -1;
    }
    else 
    {
      if (maskActual & MediaHandler::ACCEPT_MASK
        || maskActual & MediaHandler::READ_MASK)
      {
        nOnCall = eleFind.m_pEh->OnInput(aFd);
      }
      if ((nOnCall == 0 || nOnCall == -2) && 
        (maskActual & MediaHandler::CONNECT_MASK
        || maskActual & MediaHandler::WRITE_MASK))
      {
        nOnCall = eleFind.m_pEh->OnOutput(aFd);
      }
    }

    if (nOnCall == 0) 
    {
      rv = RT_OK;
    }
    else if (nOnCall == -2) 
    {
      rv = RT_ERROR_WOULD_BLOCK;
    } 
    else 
    {
      // maybe the handle is reregiested or removed when doing callbacks. 
      // so we have to refind it.
      MediaHandlerRepository::CElement eleFindAgain;
      rv = m_EhRepository.Find(aFd, eleFindAgain);
      if (RT_FAILED(rv) || eleFind.m_pEh != eleFindAgain.m_pEh) {
        //RT_ERROR_TRACE("MediaReactorEpoll::ProcessHandleEvent,"
        //  " callback shouldn't return fail after the fd is reregiested or removed!"
        //  " aFd=" << aFd << 
        //  " EHold=" << eleFind.m_pEh << 
        //  " EHnew=" << eleFindAgain.m_pEh << 
        //  " find=" << rv);
        //////////////////////////////////////////////////////////////////////////
        /// Webb: It is possible for RUDP to receive a bad handshake pdu from peer side and 
        ///       then RemoveHandler() from the reactor. 
        //RT_ASSERTE(false);
      }
      else 
      {
        rv = RemoveHandleWithoutFinding_i(aFd, eleFindAgain, 
          MediaHandler::ALL_EVENTS_MASK | MediaHandler::SHOULD_CALL);
      }
      rv = RT_ERROR_FAILURE;
    }
  }
  else 
  {
    rv = RemoveHandleWithoutFinding_i(aFd, eleFind, 
      MediaHandler::ALL_EVENTS_MASK | MediaHandler::SHOULD_CALL);
    rv = RT_ERROR_FAILURE;
  }

#ifndef RT_DISABLE_EVENT_REPORT
  CRtTimeValue tvSub = CRtTimeValue::GetTimeOfDay() - tvCur;
  if (tvSub > CRtEventQueueBase::s_tvReportInterval) {
    RT_ERROR_TRACE_THIS("MediaReactorEpoll::ProcessHandleEvent, report,"
      " sec=" << tvSub.GetSec() << 
      " usec=" << tvSub.GetUsec() <<
      " aFd=" << aFd <<
      " aMask=" << MediaHandler::GetMaskString(aMask) <<
      " maskFind=" << MediaHandler::GetMaskString(eleFind.m_Mask) << 
      " ehFind=" << eleFind.m_pEh << 
      " aReason=" << aReason);
  }
#endif // !RT_DISABLE_EVENT_REPORT
  return rv;
}

int MediaReactorEpoll::
RemoveHandleWithoutFinding_i(MEDIA_HANDLE aFd, 
               const MediaHandlerRepository::CElement &aHe, 
               MediaHandler::MASK aMask)
{
  MediaHandler::MASK maskNew = aMask & MediaHandler::ALL_EVENTS_MASK;
  MediaHandler::MASK maskEh = aHe.m_Mask;
  MediaHandler::MASK maskSelect = (maskEh & maskNew) ^ maskEh;
  if (maskSelect == maskEh) 
  {
    RT_WARNING_TRACE("MediaReactorEpoll::RemoveHandleWithoutFinding_i, mask is equal. aMask=" << aMask);
    return RT_OK;
  }
  
  if (maskSelect == MediaHandler::NULL_MASK) 
  {
    int rv = m_EhRepository.UnBind(aFd);
    if (RT_FAILED(rv)) 
      RT_WARNING_TRACE("MediaReactorEpoll::RemoveHandleWithoutFinding_i, UnBind() failed!"
        " aFd=" << aFd <<
        " aMask=" << MediaHandler::GetMaskString(aMask) <<
        " rv=" << rv);
    
    OnHandleRemoved(aFd);
    if (aMask & MediaHandler::SHOULD_CALL) 
      aHe.m_pEh->OnClose(aFd, maskEh);
    
    return RT_OK;
  }
  else 
  {
    MediaHandlerRepository::CElement eleBind = aHe;
    eleBind.m_Mask = maskSelect;
    int rvBind = m_EhRepository.Bind(aFd, eleBind);
    RT_ASSERTE(rvBind == RT_ERROR_FOUND);
    return rvBind;
  }
}

int MediaReactorEpoll::StopEventLoop()
{
  RT_DEBUG_TRACE_THIS("MediaReactorEpoll::StopEventLoop");
  
  // this function can be invoked in the different thread.
  CRtStopFlag::stoppedflag_ = true;
  CRtEventQueueBase::Stop();
  return RT_OK;
}

int MediaReactorEpoll::Close()
{
  if (s_IsTimerSet)
  {
    if (::signal(SIGALRM, SIG_IGN) == SIG_ERR) 
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::Close, signal(SIGALARM) failed! err=" << errno);
    
    struct itimerval itvInterval;
    itvInterval.it_value.tv_sec = 0;
    itvInterval.it_value.tv_usec = 0;
    itvInterval.it_interval.tv_sec = 0;
    itvInterval.it_interval.tv_usec = 0;

    if (::setitimer(ITIMER_REAL, &itvInterval, NULL) == -1)
      RT_ERROR_TRACE_THIS("MediaReactorEpoll::Close, setitimer() failed! err=" << errno);
    s_IsTimerSet = false;
  }

  m_dwWallTimerJiffies = 0;

  if (m_pEvents) 
  {
    delete []m_pEvents;
    m_pEvents = NULL;
  }
  
  m_Notify.Close();

  if (m_fdEpoll != MEDIA_INVALID_HANDLE) 
  {
    ::close(m_fdEpoll);
    m_fdEpoll = MEDIA_INVALID_HANDLE;
  }

  m_EhRepository.Close();
  CRtEventQueueBase::DestoryPendingEvents();
  return RT_OK;
}

int MediaReactorEpoll::OnHandleRegister(MEDIA_HANDLE aFd, MediaHandler::MASK aMask, MediaHandler *aEh)
{
  if (m_fdEpoll==MEDIA_INVALID_HANDLE) 
  {
    RT_WARNING_TRACE_THIS("MediaReactorEpoll::OnHandleRegister, epoll not initialized!");
    return RT_ERROR_NOT_INITIALIZED;
  }

  // Need NOT do CheckPollIn() because epoll_ctl() will do it interval.
  return DoEpollCtl_i(aFd, aMask, EPOLL_CTL_ADD);;
}

void MediaReactorEpoll::OnHandleRemoved(MEDIA_HANDLE aFd)
{
//  RT_DEBUG_TRACE_THIS("MediaReactorEpoll::OnHandleRemoved,"
//    " aFd=" << aFd << 
//    " m_nEventsBeginIndex=" << m_nEventsBeginIndex << 
//    " m_nEventsEndIndex=" << m_nEventsEndIndex);

  if (m_fdEpoll==MEDIA_INVALID_HANDLE) 
  {
    RT_WARNING_TRACE_THIS("MediaReactorEpoll::OnHandleRemoved, epoll not initialized!");
    return;
  }

  if (::epoll_ctl(m_fdEpoll, EPOLL_CTL_DEL, aFd, NULL) < 0) 
  {
    RT_ERROR_TRACE_THIS("MediaReactorEpoll::OnHandleRemoved, epoll_ctl() failed!"
      " m_fdEpoll=" << m_fdEpoll << 
      " aFd=" << aFd << 
      " err=" << errno);
  }
  
  if (m_nEventsEndIndex == 0)
    return;

  int i = m_nEventsBeginIndex + 1;
  struct epoll_event *pEvent = m_pEvents + i;
  for ( ; i < m_nEventsEndIndex; i++, pEvent++) 
  {
    if (pEvent->data.fd == aFd) {
      RT_WARNING_TRACE_THIS("MediaReactorEpoll::OnHandleRemoved,"
        " find same fd=" << aFd << 
        " m_nEventsBeginIndex=" << m_nEventsBeginIndex << 
        " m_nEventsEndIndex=" << m_nEventsEndIndex << 
        " i=" << i);
      pEvent->data.fd = MEDIA_INVALID_HANDLE;
      return;
    }
  }
}

int MediaReactorEpoll::
RegisterHandler(MediaHandler *aEh, MediaHandler::MASK aMask)
{
  Est_.EnsureSingleThread();
  int rv;
  RT_ASSERTE_RETURN(aEh, RT_ERROR_INVALID_ARG);
  
  MediaHandler::MASK maskNew = aMask & MediaHandler::ALL_EVENTS_MASK;
  if (maskNew == MediaHandler::NULL_MASK)
  {
    RT_WARNING_TRACE("MediaReactorEpoll::RegisterHandler, NULL_MASK. aMask=" << MediaHandler::GetMaskString(aMask));
    return RT_ERROR_INVALID_ARG;
  }
  
  MediaHandlerRepository::CElement eleFind;
  MEDIA_HANDLE fdNew = aEh->GetHandle();
  rv = m_EhRepository.Find(fdNew, eleFind);
  if (maskNew == eleFind.m_Mask && aEh == eleFind.m_pEh)
    return RT_OK;
  
  if (eleFind.IsCleared()) 
  {
    rv = OnHandleRegister(fdNew, maskNew, aEh);
    
    // needn't remove handle when OnHandleRegister() failed
    // because the handle didn't be inserted at all
    if (RT_FAILED(rv))
      return rv;
  }
  
  MediaHandlerRepository::CElement eleNew(aEh, maskNew);
  rv = m_EhRepository.Bind(fdNew, eleNew);

  if (rv == RT_ERROR_FOUND) 
  {
    rv = DoEpollCtl_i(aEh->GetHandle(), aMask, EPOLL_CTL_MOD);
    if (RT_SUCCEEDED(rv))
      rv = RT_ERROR_FOUND;
  }
  return rv;
}

int MediaReactorEpoll::
RemoveHandler(MediaHandler *aEh, MediaHandler::MASK aMask)
{
  Est_.EnsureSingleThread();
  int rv;
  RT_ASSERTE_RETURN(aEh, RT_ERROR_INVALID_ARG);
  
  MediaHandler::MASK maskNew = aMask & MediaHandler::ALL_EVENTS_MASK;
  if (maskNew == MediaHandler::NULL_MASK) 
  {
    RT_WARNING_TRACE("MediaReactorEpoll::RemoveHandler, NULL_MASK. aMask=" << MediaHandler::GetMaskString(aMask));
    return RT_ERROR_INVALID_ARG;
  }
  
  MediaHandlerRepository::CElement eleFind;
  MEDIA_HANDLE fdNew = aEh->GetHandle();
  rv = m_EhRepository.Find(fdNew, eleFind);

  if (RT_FAILED(rv))
    return rv;
  
  rv = RemoveHandleWithoutFinding_i(fdNew, eleFind, maskNew);
  if (rv == RT_ERROR_FOUND) 
  {
    rv = DoEpollCtl_i(aEh->GetHandle(), aMask, EPOLL_CTL_MOD);
    if (RT_SUCCEEDED(rv))
      rv = RT_ERROR_FOUND;
  }
  return rv;
}

int MediaReactorEpoll::DoEpollCtl_i(MEDIA_HANDLE aFd, MediaHandler::MASK aMask, int aOperation)
{
  struct epoll_event epEvent;
  ::memset(&epEvent, 0, sizeof(epEvent));
  epEvent.events = EPOLLERR | EPOLLHUP;
  if(!(aMask & MediaHandler::ACCEPT_MASK)) //Add 17/02 2006 accept with level model
    epEvent.events |= EPOLLET;
  
  epEvent.data.fd = aFd;

  if (aMask & MediaHandler::CONNECT_MASK)
    epEvent.events |= EPOLLIN | EPOLLOUT;
  if (aMask & MediaHandler::READ_MASK || aMask & MediaHandler::ACCEPT_MASK)
    epEvent.events |= EPOLLIN;
  if (aMask & MediaHandler::WRITE_MASK)
    epEvent.events |= EPOLLOUT;

  if (::epoll_ctl(m_fdEpoll, aOperation, aFd, &epEvent) < 0) 
  {
    RT_ERROR_TRACE_THIS("MediaReactorEpoll::DoEpollCtl_i, epoll_ctl() failed!"
      " m_fdEpoll=" << m_fdEpoll << 
      " aFd=" << aFd << 
      " aOperation=" << aOperation << 
      " err=" << errno);
    return RT_ERROR_FAILURE;
  }
  else
    return RT_OK;
}

int MediaReactorEpoll::
ScheduleTimer(IRtTimerHandler *aTh, LPVOID aArg, 
        const CRtTimeValue &aInterval, DWORD aCount)
{
  return m_CalendarTimer.ScheduleTimer(aTh, aArg, aInterval, aCount);
}

int MediaReactorEpoll::CancelTimer(IRtTimerHandler *aTh)
{
  return m_CalendarTimer.CancelTimer(aTh);
}

int MediaReactorEpoll::SendEvent(IRtEvent *aEvent)
{
  return CRtEventQueueUsingMutex::SendEvent(aEvent);
}

// this function can be invoked in the different thread.
int MediaReactorEpoll::PostEvent(IRtEvent* aEvent, EPriority aPri)
{
  DWORD dwOldSize = 0;
  int rv = CRtEventQueueUsingMutex::
    PostEventWithOldSize(aEvent, aPri, &dwOldSize);
  if (RT_SUCCEEDED(rv) && dwOldSize == 0)
    return NotifyHandler(NULL, MediaHandler::EVENTQUEUE_MASK);
  return rv;
}

// this function can be invoked in the different thread.
DWORD MediaReactorEpoll::GetPendingEventsCount()
{
  return CRtEventQueueUsingMutex::GetPendingEventsCount();
}

} //namespace ma
