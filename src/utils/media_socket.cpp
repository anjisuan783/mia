#include "media_socket.h"
#include "common/media_log.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

//////////////////////////////////////////////////////////////////////
// class Media_IPC_SAP
MEDIA_HANDLE Media_IPC_SAP::GetHandle() const  {
  return m_Handle;
}

void Media_IPC_SAP::SetHandle(MEDIA_HANDLE aNew) {
  MA_ASSERT(m_Handle == MEDIA_INVALID_HANDLE || aNew == MEDIA_INVALID_HANDLE);
  m_Handle = aNew;
}

int Media_IPC_SAP::Enable(int aValue) const  {
  switch(aValue) {
  case NON_BLOCK: {
    int nVal = ::fcntl(m_Handle, F_GETFL, 0);
    if (nVal == -1)
      return -1;
    nVal |= O_NONBLOCK;
    if (::fcntl(m_Handle, F_SETFL, nVal) == -1)
      return -1;
    return 0;
  }

  default:
    MLOG_ERROR_THIS("Media_IPC_SAP::Enable, aValue=" << aValue);
    return -1;
  }
}

int Media_IPC_SAP::Disable(int aValue) const {
  switch(aValue) {
  case NON_BLOCK: {
    int nVal = ::fcntl(m_Handle, F_GETFL, 0);
    if (nVal == -1)
      return -1;
    nVal &= ~O_NONBLOCK;
    if (::fcntl(m_Handle, F_SETFL, nVal) == -1)
      return -1;
    return 0;
  }

  default:
    MLOG_ERROR_THIS("Media_IPC_SAP::Disable, aValue=" << aValue);
    return -1;
  }
}

int Media_IPC_SAP::Control(int aCmd, void *aArg) const {
  int nRet;
  nRet = ::ioctl(m_Handle, aCmd, aArg);
  return nRet;
}

} //namespace ma