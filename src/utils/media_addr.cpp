#include "media_addr.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <memory>
#include <map>

#include "common/media_log.h"
#include "media_msg_queue.h"
#include "media_time_value.h"
#include "utils/media_timer_helper.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

#define IN6ADDRSZ 16
#define INADDRSZ 4
#define INT16SZ 2

#define MEDIA_NETDB_BUF_SIZE 1024

#ifndef kAi_Addrlen
#define kAi_Addrlen INET6_ADDRSTRLEN
#endif

#define SET_ERRNO(x) (errno = (x))

namespace {

/*
 * Format an IPv4 address, more or less like inet_ntoa().
 *
 * Returns `dst' (as a const)
 * Note:
 *  - uses no statics
 *  - takes a unsigned char* not an in_addr as input
 */
static char* inet_ntop4(const unsigned char* src, char* dst, size_t size) {
  char tmp[sizeof "255.255.255.255"];
  size_t len;

  assert(size >= 16);

  tmp[0] = '\0';
  (void)snprintf(tmp, sizeof(tmp), "%d.%d.%d.%d",
                 ((int)((unsigned char)src[0])) & 0xff,
                 ((int)((unsigned char)src[1])) & 0xff,
                 ((int)((unsigned char)src[2])) & 0xff,
                 ((int)((unsigned char)src[3])) & 0xff);

  len = strlen(tmp);
  if (len == 0 || len >= size) {
    SET_ERRNO(ENOSPC);
    return (NULL);
  }
  strcpy(dst, tmp);
  return dst;
}

/*
 * Convert IPv6 binary address into presentation (printable) format.
 */
static char* inet_ntop6(const unsigned char* src, char* dst, size_t size) {
  /*
   * Note that int32_t and int16_t need only be "at least" large enough
   * to contain a value of the specified size.  On some systems, like
   * Crays, there is no such thing as an integer variable with 16 bits.
   * Keep this in mind if you think this function should have been coded
   * to use pointer overlays.  All the world's not a VAX.
   */
  char tmp[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
  char* tp;
  struct {
    long base;
    long len;
  } best, cur;
  unsigned long uint16_ts[IN6ADDRSZ / INT16SZ];
  int i;

  /* Preprocess:
   *  Copy the input (bytewise) array into a uint16_twise array.
   *  Find the longest run of 0x00's in src[] for :: shorthanding.
   */
  memset(uint16_ts, '\0', sizeof(uint16_ts));
  for (i = 0; i < IN6ADDRSZ; i++)
    uint16_ts[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));

  best.base = -1;
  cur.base = -1;
  best.len = 0;
  cur.len = 0;

  for (i = 0; i < (IN6ADDRSZ / INT16SZ); i++) {
    if (uint16_ts[i] == 0) {
      if (cur.base == -1)
        cur.base = i, cur.len = 1;
      else
        cur.len++;
    } else if (cur.base != -1) {
      if (best.base == -1 || cur.len > best.len)
        best = cur;
      cur.base = -1;
    }
  }
  if ((cur.base != -1) && (best.base == -1 || cur.len > best.len))
    best = cur;
  if (best.base != -1 && best.len < 2)
    best.base = -1;

  /* Format the result.
   */
  tp = tmp;
  for (i = 0; i < (IN6ADDRSZ / INT16SZ); i++) {
    /* Are we inside the best run of 0x00's?
     */
    if (best.base != -1 && i >= best.base && i < (best.base + best.len)) {
      if (i == best.base)
        *tp++ = ':';
      continue;
    }

    /* Are we following an initial run of 0x00s or any real hex?
     */
    if (i != 0)
      *tp++ = ':';

    /* Is this address an encapsulated IPv4?
     */
    if (i == 6 && best.base == 0 &&
        (best.len == 6 || (best.len == 5 && uint16_ts[5] == 0xffff))) {
      if (!inet_ntop4(src + 12, tp, sizeof(tmp) - (tp - tmp))) {
        SET_ERRNO(ENOSPC);
        return (NULL);
      }
      tp += strlen(tp);
      break;
    }
    tp += snprintf(tp, 5, "%lx", uint16_ts[i]);
  }

  /* Was it a trailing run of 0x00's?
   */
  if (best.base != -1 && (best.base + best.len) == (IN6ADDRSZ / INT16SZ))
    *tp++ = ':';
  *tp++ = '\0';

  /* Check for overflow, copy, and we're done.
   */
  if ((size_t)(tp - tmp) > size) {
    SET_ERRNO(ENOSPC);
    return (NULL);
  }
  strcpy(dst, tmp);
  return dst;
}

static int inet_pton4(const char* src, unsigned char* dst) {
  static const char digits[] = "0123456789";
  int saw_digit, octets, ch;
  unsigned char tmp[INADDRSZ], *tp;

  saw_digit = 0;
  octets = 0;
  tp = tmp;
  *tp = 0;
  while ((ch = *src++) != '\0') {
    const char* pch;

    if ((pch = strchr(digits, ch)) != NULL) {
      unsigned int val = *tp * 10 + (unsigned int)(pch - digits);

      if (saw_digit && *tp == 0)
        return (0);
      if (val > 255)
        return (0);
      *tp = (unsigned char)val;
      if (!saw_digit) {
        if (++octets > 4)
          return (0);
        saw_digit = 1;
      }
    } else if (ch == '.' && saw_digit) {
      if (octets == 4)
        return (0);
      *++tp = 0;
      saw_digit = 0;
    } else
      return (0);
  }
  if (octets < 4)
    return (0);
  memcpy(dst, tmp, INADDRSZ);
  return (1);
}

static int inet_pton6(const char* src, unsigned char* dst) {
  static const char xdigits_l[] = "0123456789abcdef",
                    xdigits_u[] = "0123456789ABCDEF";
  unsigned char tmp[IN6ADDRSZ], *tp, *endp, *colonp;
  const char *xdigits, *curtok;
  int ch, saw_xdigit;
  size_t val;

  memset((tp = tmp), 0, IN6ADDRSZ);
  endp = tp + IN6ADDRSZ;
  colonp = NULL;
  /* Leading :: requires some special handling. */
  if (*src == ':')
    if (*++src != ':')
      return (0);
  curtok = src;
  saw_xdigit = 0;
  val = 0;
  while ((ch = *src++) != '\0') {
    const char* pch;

    if ((pch = strchr((xdigits = xdigits_l), ch)) == NULL)
      pch = strchr((xdigits = xdigits_u), ch);
    if (pch != NULL) {
      val <<= 4;
      val |= (pch - xdigits);
      if (++saw_xdigit > 4)
        return (0);
      continue;
    }
    if (ch == ':') {
      curtok = src;
      if (!saw_xdigit) {
        if (colonp)
          return (0);
        colonp = tp;
        continue;
      }
      if (tp + INT16SZ > endp)
        return (0);
      *tp++ = (unsigned char)(val >> 8) & 0xff;
      *tp++ = (unsigned char)val & 0xff;
      saw_xdigit = 0;
      val = 0;
      continue;
    }
    if (ch == '.' && ((tp + INADDRSZ) <= endp) && inet_pton4(curtok, tp) > 0) {
      tp += INADDRSZ;
      saw_xdigit = 0;
      break; /* '\0' was seen by inet_pton4(). */
    }
    return (0);
  }
  if (saw_xdigit) {
    if (tp + INT16SZ > endp)
      return (0);
    *tp++ = (unsigned char)(val >> 8) & 0xff;
    *tp++ = (unsigned char)val & 0xff;
  }
  if (colonp != NULL) {
    /*
     * Since some memmove()'s erroneously fail to handle
     * overlapping regions, we'll do the shift by hand.
     */
    const size_t n = tp - colonp;
    size_t i;

    if (tp == endp)
      return (0);
    for (i = 1; i <= n; i++) {
      *(endp - i) = *(colonp + n - i);
      *(colonp + n - i) = 0;
    }
    tp = endp;
  }
  if (tp != endp)
    return (0);
  memcpy(dst, tmp, IN6ADDRSZ);
  return (1);
}

}  // namespace

class DnsRecord final {
 public:
  DnsRecord(const std::string& host_name);
  ~DnsRecord() = default;

  class iterator {
   public:
    using value_type = char*;
    iterator() = default;

    iterator(char* buf) { addr_ = buf; }

    iterator& operator++() {
      if (addr_) {
        addr_ += kAi_Addrlen;
      }
      return (*this);
    }

    iterator operator++(int) {
      iterator cp = *this;
      ++*this;
      return cp;
    }

    value_type operator*() const {
      if (addr_)
        return (reinterpret_cast<value_type>(addr_));
      return nullptr;
    }

    bool operator==(const iterator& rand) const { return (addr_ == r.addr_); }

    bool operator!=(const iterator& r) const { return (!(*this == r)); }

   private:
    char* addr_ = nullptr;
  };
  iterator begin() {
    MA_ASSERT(state_ == RSV_SUCCESS);
    if (state_ == RSV_SUCCESS)
      return iterator(buffer_);
    else
      return iterator(NULL);
  }

  iterator end() {
    MA_ASSERT(state_ == RSV_SUCCESS);
    return iterator(NULL);
  }

  std::string GetHostName() { return host_name_; }

 private:
  std::string host_name_;
  enum {
    RSV_IDLE,
    RSV_PROCESSING,
    RSV_SUCCESS,
    RSV_FAILED,
  } state_ = RSV_IDLE;

  MediaTimeValue resolved_;

  // contains struct hostent.
  char buffer_[MEDIA_NETDB_BUF_SIZE];

  friend class DnsManager;
};

class MediaObserver {
public:
	virtual void OnObserve(const char* aTopic, void* data = nullptr) = 0;

protected:
	virtual ~MediaObserver() = default;
};

class DnsManager final : public MediaMsg, public MediaTimerHelpSink {
 public:
  /**
   * gets ip address from cache or kicks off an asynchronous host lookup.
   *
   * @param aRecord
   *        return DNS record corresponding to the given hostname.
   * @param aHostName
   *        the hostname or IP-address-literal to resolve.
   * @param aObserver
   *        the listener to be notified when the result is available.
   * @param aBypassCache
   *        if true, the internal DNS lookup cache will be bypassed.
   * @param aThreadListener
   *        optional parameter (may be null).  if non-null, this parameter
   *        specifies the MediaThread on which the listener's
   *        OnObserve should be called.  however, if this parameter is
   *        null, then OnObserve will be called on current thread.
   *
   * @return
   *        if RT_OK, <aRecord> is filled with corresponding record.
   *        else if ERROR_WOULD_BLOCK, <aObserver> will be callback late only
   *   once. You should call CancelResolve() before callback.
   *        else resolve hostname failed.
   */
  int AsyncResolve(std::shared_ptr<DnsRecord>& record,
                   const std::string& hostname,
                   MediaObserver* aObserver = nullptr,
                   bool bypass_cache = false,
                   MediaThread* thread_listener = nullptr);

  // <aObserver> will not notified after calling this function.
  int CancelResolve(MediaObserver* observer);

  // clear the <hostname> in the cache and resolve it again.
  // return ERROR_WOULD_BLOCK: is relsoving.
  // return ERROR_FAILURE: relsove failed.
  int RefreshHost(const std::string& hostname);

  // close and release any resoruce.
  int Shutdown();

  /**
   * gets ip address from cache or resolve it at once,
   * the current thread will be blocking when resolving.
   *
   * @param aRecord
   *        return DNS record corresponding to the given hostname.
   * @param aHostName
   *        the hostname or IP-address-literal to resolve.
   * @param aBypassCache
   *        if true, the internal DNS lookup cache will be bypassed.
   *
   * @return
   *        if RT_OK, <aRecord> is filled with corresponding record.
   *        else resolve hostname failed.
   */
  int SyncResolve(std::shared_ptr<DnsRecord>& aRecord,
                  const std::string& hostname,
                  bool bypass_cache = false);

  int GetLocalIps(DnsRecord*& aRecord);

 protected:
  // implement of MediaMsg
  srs_error_t OnFire() override;
  void OnDelete() override;

  // implement MediaTimerHelpSink
  void OnTimer(MediaTimerHelp* id) override;

 private:
  DnsManager();
  ~DnsManager();

  int BeginResolve_l(DnsRecord* aRecord);
  int DoGetHostByName_l(DnsRecord* aRecord);
  int TryAddObserver_l(MediaObserver* aObserver,
                      MediaThread* thread_listener,
                      const std::string& hostname);
  int Resolved_l(DnsRecord* aRecord, int aError, bool callback = true);
  int DoCallback_l(int aError, const std::string& hostname);
  int FindInCache_l(DnsRecord*& aRecord, const std::string& hostname);

  void SpawnDnsThread_l();
 private:
  int GetHostByname_i(DnsRecord* aRecord);
  int GetAddrInfo_i(DnsRecord* aRecord);

 private:
  typedef std::map<std::string, std::shared_ptr<DnsRecord>> CacheRecordsType;
  CacheRecordsType m_CacheRecords;
  typedef std::list<std::shared_ptr<DnsRecord>> PendingRecordsType;
  PendingRecordsType m_PendingRecords;

  class CObserverAndListener : public MediaMsg {
   public:
    CObserverAndListener(DnsManager* dnsmgr,
                         MediaObserver* aObserver,
                         MediaThread* aThreadListener,
                         int aError,
                         const std::string& hostname)
        : m_pDnsManager(dnsmgr),
          m_pObserver(aObserver),
          m_pThreadListener(aThreadListener),
          m_nError(aError),
          m_strHostName(hostname) {
      MA_ASSERT(m_pDnsManager);
      MA_ASSERT(m_pObserver);
      MA_ASSERT(m_pThreadListener);
    }

    bool operator==(const CObserverAndListener& r) {
      return m_pObserver == r.m_pObserver;
    }

    bool operator==(MediaObserver* aObserver) { return m_pObserver == aObserver; }

    // interface Mediamsg
    srs_error_t OnFire() override;

    DnsManager* m_pDnsManager;
    MediaObserver* m_pObserver;
    MediaThread* m_pThreadListener;
    int m_nError;
    std::string m_strHostName;
  };
  typedef std::vector<CObserverAndListener> ObserversType;
  ObserversType m_Observers;

  typedef std::mutex MutexType;
  MutexType m_Mutex;

  MediaThread* m_pThreadDNS = nullptr;

  MediaThread* m_pThreadNetwork;
  MediaTimerHelp m_TimerExpired;
};

// class DnsRecord
DnsRecord::DnsRecord(const std::string& host_name)
    : host_name_(host_name), resolved_(MediaTimeValue::GetDayTime()) {
  MA_ASSERT(!host_name_.empty());
  ::memset(buffer_, 0, sizeof(buffer_));
}

// class DnsManager
DnsManager::DnsManager() {
  m_pThreadNetwork = MediaThreadManager::Instance()->GetDefaultNetworkThread();
  MA_ASSERT(m_pThreadNetwork);

  m_TimerExpired.Schedule(this, MediaTimeValue(3, 0));
}

DnsManager::~DnsManager() {
  Shutdown();
}

int DnsManager::AsyncResolve(DnsRecord*& aRecord,
                              const Std::string& aHostName,
                              MediaObserver* aObserver,
                              bool aBypassCache,
                              MediaThread* aThreadListener) {
  MA_ASSERT(!aRecord);
  MLOG_INFO_THIS(" aHostName="
      << aHostName << " aObserver=" << aObserver << " aBypassCache="
      << aBypassCache << " aThreadListener=" << aThreadListener);

  std::lock_guard<std::mutex> guard(m_Mutex);
  if (!aBypassCache) {
    int rv = FindInCache_l(aRecord, aHostName);
    if (rv == ERROR_SUCCESS)
      return rv;
  }
  std::shared_ptr<DnsRecord> pRecordNew = new DnsRecord(aHostName);
  int nErr = BeginResolve_l(pRecordNew.get());
  if (nErr) {
    Resolved_l(pRecordNew.get(), nErr, false);
    return ERROR_FAILURE;
  }

  TryAddObserver_l(aObserver, aThreadListener, aHostName);
  return ERROR_SOCKET_WOULD_BLOCK;
}

int DnsManager::SyncResolve(std::shared_ptr<DnsRecord>& aRecord,
                            const std::string& aHostName,
                            bool aBypassCache) {
  MA_ASSERT(!aRecord);
  MLOG_INFO_THIS(" aHostName=" << aHostName 
      << " aBypassCache=" << aBypassCache);

  std::lock_guard<std::mutex> guard(m_Mutex);
  if (!aBypassCache) {
    int rv = FindInCache_l(aRecord, aHostName);
    if (rv = ERROR_SUCCESS)
      return rv;
  }

  std::shared_ptr<DnsRecord> pRecordNew;
  PendingRecordsType::iterator iterPending = m_PendingRecords.begin();
  for (; iterPending != m_PendingRecords.end(); ++iterPending) {
    if ((*iterPending)->m_strHostName == aHostName) {
      MLOG_WARN_THIS(" remove pending for hostname=" << aHostName);
      pRecordNew = (*iterPending);
      m_PendingRecords.erase(iterPending);

      // TODO: If it's processing, wait util reloved.
      MA_ASSERT(pRecordNew->m_State == DnsRecord::RSV_IDLE);
      break;
    }
  }

  int nErr = -998;
  if (!pRecordNew) {
    pRecordNew = new DnsRecord(aHostName);
    if (!pRecordNew)
      goto fail;
  }

  m_PendingRecords.push_front(pRecordNew);
  nErr = DoGetHostByName_l(pRecordNew.Get());

fail:
  Resolved_l(pRecordNew.get(), nErr, false);
  if (!nErr) {
    aRecord = pRecordNew.get();
    aRecord->AddReference();
    return ERROR_SUCCESS;
  } else {
    return ERROR_NETWORK_DNS_FAILURE;
  }
}

Int DnsManager::FindInCache_l(DnsRecord*& aRecord,
                                   const Std::string& aHostName) {
  MA_ASSERT(!aRecord);
  CacheRecordsType::iterator iter = m_CacheRecords.find(aHostName);
  if (iter != m_CacheRecords.end()) {
    aRecord = (*iter).second.Get();
    MA_ASSERT(aRecord);
    MA_ASSERT(aHostName == aRecord->m_strHostName);

    if (aRecord->m_State == DnsRecord::RSV_SUCCESS) {
      aRecord->AddReference();
      return RT_OK;
    } else if (aRecord->m_State == DnsRecord::RSV_FAILED) {
      aRecord = NULL;
      return RT_ERROR_NETWORK_DNS_FAILURE;
    } else {
      MLOG_ERROR_THIS(
          " error state in m_CacheRecords"
          " aHostName="
          << aHostName << " aRecord=" << aRecord
          << " state=" << aRecord->m_State);
      MA_ASSERT(false);
      return ERROR_UNEXPECTED;
    }
  }
  return ERROR_NOT_FOUND;
}

int DnsManager::RefreshHost(const Std::string& aHostName) {
  MLOG_INFO_THIS("DnsManager::RefreshHost,"
      " aHostName=" << aHostName);

  std::shared_ptr<DnsRecord> m_pOldRecord;
  std::lock_guard<std::mutex> guard(m_Mutex);
  CacheRecordsType::iterator iter = m_CacheRecords.find(aHostName);
  if (iter != m_CacheRecords.end()) {
    m_pOldRecord = (*iter).second;
    MA_ASSERT(m_pOldRecord->m_State == DnsRecord::RSV_SUCCESS ||
               m_pOldRecord->m_State == DnsRecord::RSV_FAILED);
    MA_ASSERT(m_pOldRecord->m_strHostName == aHostName);
    m_CacheRecords.erase(iter);
  }
  if (!m_pOldRecord) {
    m_pOldRecord = new DnsRecord(aHostName);
  } else {
    m_pOldRecord->m_State = DnsRecord::RSV_IDLE;
  }

  int nErr = BeginResolve_l(m_pOldRecord.Get());
  if (nErr) {
    Resolved_l(m_pOldRecord.Get(), nErr, false);
    return ERROR_FAILURE;
  }
  return ERROR_SOCKET_WOULD_BLOCK;
}

int DnsManager::GetLocalIps(std::shared_ptr<DnsRecord>& aRecord) {
  char szBuf[512];
  int nErr = ::gethostname(szBuf, sizeof(szBuf));
  if (nErr != 0) {

    MLOG_ERROR_THIS("gethostname() failed! err=" << errno);
    return ERROR_FAILURE;
  }
  int rv = SyncResolve(aRecord, szBuf);
  return rv;
}

int DnsManager::BeginResolve_l(DnsRecord* aRecord) {
  MA_ASSERT_RETURN(aRecord, -999);

  PendingRecordsType::iterator iterPending = m_PendingRecords.begin();
  for (; iterPending != m_PendingRecords.end(); ++iterPending) {
    if ((*iterPending)->m_strHostName == aRecord->m_strHostName) {
      return 0;
    }
  }

  std::shared_ptr<DnsRecord> pRecordNew = aRecord;
  m_PendingRecords.push_back(pRecordNew);

  int rv = ERROR_SUCCESS;
  if (!m_pThreadDNS)
    rv = SpawnDnsThread_l();
  if (rv == ERROR_SUCCESS)
    rv = m_pThreadDNS->MsgQueue()->Post(this);
  return rv==ERROR_SUCCESS ? 0 : -1;
}

int DnsManager::DoGetHostByName_l(DnsRecord* aRecord) {
  MA_ASSERT(aRecord);
  MA_ASSERT(aRecord->m_State == DnsRecord::RSV_IDLE);
  aRecord->m_State = DnsRecord::RSV_PROCESSING;

  // unlock the mutex because gethostbyname() will block the current thread.
  m_Mutex.unlock();
  int nError = GetAddrInfo_i(aRecord);

  if (nError != ERROR_SUCCESS) {
    // nError = EADDRNOTAVAIL;
    MLOG_ERROR_THIS(" failed hostName: " << aRecord->m_strHostName
                   << " errInfo: " << strerror(nError));
  }
  m_Mutex.lock();
  return nError;
}

int DnsManager::GetHostByname_i(DnsRecord* aRecord) {
  int nError = 0;
  ::memset(aRecord->m_szBuffer, 0, sizeof(aRecord->m_szBuffer));
  struct hostent* pHeResult = ::gethostbyname(aRecord->m_strHostName.c_str());
  if (pHeResult) {
    char* pResutBuffer = aRecord->m_szBuffer;
    struct sockaddr_in addr;

    for (int i = 0; pHeResult->h_addr_list[i]; i++) {
      ::memset(&addr, 0, sizeof(sockaddr_in));
      addr.sin_family = AF_INET;
      addr.sin_addr = *((struct in_addr*)pHeResult->h_addr_list[i]);
      ::memcpy(pResutBuffer, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(sockaddr_in));
      pResutBuffer += kAi_Addrlen;
    }
  } else {
    nError = errno;
    if (!nError)
      nError = EADDRNOTAVAIL;
  }
  return nError;
}

int DnsManager::GetAddrInfo_i(DnsRecord* aRecord) {
  int nError = 0;
  ::memset(aRecord->m_szBuffer, 0, sizeof(aRecord->m_szBuffer));

  struct addrinfo hints, *pTmp, *pResult;
  memset(&hints, 0, sizeof(hints));
  // hints.ai_family = PF_UNSPEC;
  // hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_CANONNAME;
  nError = ::getaddrinfo(aRecord->m_strHostName.c_str(), "", &hints, &pResult);
  if (nError == ERROR_SUCCESS) {
    char* pResutBuffer = aRecord->m_szBuffer;
    for (pTmp = pResult; pTmp != NULL; pTmp = pTmp->ai_next) {
      ::memcpy(pResutBuffer, pTmp->ai_addr, pTmp->ai_addrlen);
      pResutBuffer += kAi_Addrlen;
    }
    freeaddrinfo(pResult);
  } else
    nError = errno;
  if (!pResult) {
    if (!nError)
      nError = EADDRNOTAVAIL;
  }
  return nError;
}

int DnsManager::TryAddObserver_l(MediaObserver* aObserver,
                                  MediaThread* aThreadListener,
                                  const Std::string& aHostName) {
  if (!aObserver)
    return ERROR_INVALID_ARGS;

  if (!aThreadListener) {
    aThreadListener = MediaThreadManager::Instance()->CurrentThread();
    MA_ASSERT(aThreadListener);
  }

  ObserversType::iterator iter = m_Observers.begin();
  for (; iter != m_Observers.end(); ++iter) {
    if ((*iter).m_pObserver == aObserver) {
      MLOG_WARN_THIS("observer already exist.aObserver="
          << aObserver << " aThreadListener=" << aThreadListener);
      return ERROR_EXISTED;
    }
  }

  CObserverAndListener obsNew(this, aObserver, aThreadListener, 0, aHostName);
  m_Observers.push_back(obsNew);
  return ERROR_SUCCESS;
}

int DnsManager::Resolved_l(DnsRecord* aRecord,
                          int aError,
                          bool aCallback) {
  MA_ASSERT(aRecord);
  MA_ASSERT(aRecord->m_State == DnsRecord::RSV_PROCESSING);
  MLOG_INFO_THIS(" pRecord="
      << aRecord << " hostname=" << aRecord->m_strHostName
      << " aError=" << aError);

  if (!aError) {
    // it's successful.
    aRecord->m_State = DnsRecord::RSV_SUCCESS;
  } else {
    aRecord->m_State = DnsRecord::RSV_FAILED;
  }
  aRecord->m_tvResolve = MediaTimeValue::GetTimeOfDay();

  m_CacheRecords[aRecord->m_strHostName] = aRecord;

  PendingRecordsType::iterator iter =
      std::find(m_PendingRecords.begin(), m_PendingRecords.end(), aRecord);
  if (iter != m_PendingRecords.end()) {
    m_PendingRecords.erase(iter);
  } else {
    MLOG_ERROR_THIS("can't find pending."
        " maybe it's removed due to Sync and Aysnc resolve the same hostname."
        " hsotname" << aRecord->m_strHostName);
    MA_ASSERT(false);
  }

  if (aCallback)
    DoCallback_l(aError, aRecord->m_strHostName);
  return ERROR_SUCCESS;
}

int DnsManager::DoCallback_l(int aError, const Std::string& aHostName) {
  if (m_Observers.empty())
    return ERROR_SUCCESS;

  ObserversType obvOnCall(m_Observers);

  std::string strHostName(aHostName);

  // don't hold the mutex when doing callback
  m_Mutex.unlock();
  ObserversType::iterator iter = obvOnCall.begin();
  for (; iter != obvOnCall.end(); ++iter) {
    if ((*iter).m_strHostName != strHostName)
      continue;

    if (MediaThreadManager::IsEqualCurrentThread((*iter).m_pThreadListener)) {
      MediaObserver* pObserver = (*iter).m_pObserver;
      if (pObserver) {
        // allow OnObserver() once.
        int rv = CancelResolve(pObserver);
        if (rv == ERROR_SUCCESS) {
          int nErr = aError;
          pObserver->OnObserve("DnsManager", &nErr);
        }
      }
    } else {
      MediaMsgQueue* queue = (*iter).m_pThreadListener->MsgQueue();
      if (queue) {
        CObserverAndListener* pEventNew = new CObserverAndListener(*iter);
        pEventNew->m_nError = aError;
        queue->Post(pEventNew);
      }
    }
  }
  m_Mutex.lock();
  return ERROR_SUCCESS;
}

int DnsManager::CancelResolve(MediaObserver* aObserver) {
  std::lock_guard<std::mutex> guard(m_Mutex);
  ObserversType::iterator iter = m_Observers.begin();
  for (; iter != m_Observers.end(); ++iter) {
    if ((*iter).m_pObserver == aObserver) {
      m_Observers.erase(iter);
      return ERROR_SUCCESS;
    }
  }

  return ERROR_NOT_FOUND;
}

int DnsManager::Shutdown() {
  std::lock_guard<std::mutex> guard(m_Mutex);
  if (m_pThreadDNS) {
    m_pThreadDNS->Stop();
    m_pThreadDNS->Destroy();
    m_pThreadDNS = NULL;
  }

  m_Observers.clear();
  m_PendingRecords.clear();
  m_CacheRecords.clear();
  return ERROR_SUCCESS;
}

int DnsManager::CObserverAndListener::OnFire() {
  MA_ASSERT(MediaThreadManager::IsEqualCurrentThread(m_pThreadListener));

  int rv = m_pDnsManager->CancelResolve(m_pObserver);
  if (rv == ERROR_SUCCESS && m_pObserver)
    m_pObserver->OnObserve("DnsManager", &m_nError);
  return ERROR_SUCCESS;
}

void DnsManager::SpawnDnsThread_l() {
  MA_ASSERT(!m_pThreadDNS);
  m_pThreadDNS = MediaThreadManager::Instance()->CreateTaskThread(m_pThreadDNS);
}

int DnsManager::OnFire() {
  MA_ASSERT(MediaThreadManager::IsEqualCurrentThread(m_pThreadDNS));
  std::lock_guard<std::mutex> guard(m_Mutex);
  while (!m_PendingRecords.empty()) {
    // must use Std::shared_ptr to backup it, because DoGetHostByName_l()
    // maybe unlock the mutex and it's may remove in other thread.
    std::shared_ptr<DnsRecord> pRecord = (*m_PendingRecords.begin());
    int nErr = DoGetHostByName_l(pRecord.Get());
    Resolved_l(pRecord.Get(), nErr);
  }
  return ERROR_SUCCESS;
}

void DnsManager::OnDelete() {
}

void DnsManager::OnTimer(MediaTimerHelp* id) {
  if (m_CacheRecords.empty())
    return;

  MediaTimeValue tvCurrent = MediaTimeValue::GetDayTime();
  MediaTimeValue tvExpireInterval(3, 0);
  std::lock_guard<std::mutex> guard(m_Mutex);
  CacheRecordsType::iterator iter = m_CacheRecords.begin();
  while (iter != m_CacheRecords.end()) {
    DnsRecord* pRecord = (*iter).second.Get();
    if ((pRecord->m_State == DnsRecord::RSV_SUCCESS ||
         pRecord->m_State == DnsRecord::RSV_FAILED) &&
        (tvCurrent - pRecord->m_tvResolve > tvExpireInterval)) {
      CacheRecordsType::iterator iterTmp = iter++;
      m_CacheRecords.erase(iterTmp);
    } else {
      ++iter;
    }
  }
}

DnsManager g_dns;

// class MediaAddress
char* MediaAddress::Inet_ntop(int af, const void* src, char* buf, size_t size) {
  switch (af) {
    case AF_INET:
      return inet_ntop4((const unsigned char*)src, buf, size);
    case AF_INET6:
      return inet_ntop6((const unsigned char*)src, buf, size);
    default:
      assert(false);
      return NULL;
  }
}

int MediaAddress::Inet_pton(int af, const char* src, void* dst) {
  switch (af) {
    case AF_INET:
      return (inet_pton4(src, (unsigned char*)dst));
    case AF_INET6:
      return (inet_pton6(src, (unsigned char*)dst));
    default:
      SET_ERRNO(EAFNOSUPPORT);
      return (-1);
  }
}

MediaAddress MediaAddress::addr_null_;

MediaAddress::MediaAddress() {
  host_name_.reserve(64);
  ::memset(&sock_addr6_, 0, sizeof(sockaddr_in6));
  sock_addr_.sin_family = AF_INET;
}

MediaAddress::MediaAddress(uint16_t family) {
  host_name_.reserve(64);
  ::memset(&sock_addr6_, 0, sizeof(sockaddr_in6));
  sock_addr_.sin_family = family;
}

MediaAddress::MediaAddress(const char* aHostName, uint16_t aPort) {
  host_name_.reserve(64);
  Set(aHostName, aPort);
}

MediaAddress::MediaAddress(const char* aIpAddrAndPort) {
  host_name_.reserve(64);
  SetV4(aIpAddrAndPort);
}

MediaAddress::~MediaAddress() {}

int MediaAddress::Set(const char* host_name, uint16_t port) {
  if (!host_name || std::string(host_name).empty() || !port)
    return ERROR_INVALID_ARGS;

  ::memset(&sock_addr6_, 0, sizeof(sockaddr_in6));
  sock_addr_.sin_family = AF_INET;
  sock_addr_.sin_port = ::htons(port);
  int ret = SetIpAddr(host_name);
  if (ERROR_SUCCESS != ret) {
    host_name_ = host_name;
    ret = TryResolve();
  }
  return ret;
}

int MediaAddress::SetV4(const char* ip_port) {
  uint16_t prot = 0;
  MA_ASSERT_RETURN(ip_port, ERROR_INVALID_ARGS);
  char* szFind = const_cast<char*>(::strchr(ip_port, ':'));
  if (!szFind) {
    MLOG_WARN_THIS("unknow ip_port=" << ip_port);
    szFind = const_cast<char*>(ip_port) + strlen(ip_port);
    prot = 0;
  } else {
    prot = static_cast<uint16_t>(::atoi(szFind + 1));
  }

  // 256 bytes is enough, otherwise the ip string is possiblly wrong.
  char szBuf[256];
  int addr_len = szFind - ip_port;
  MA_ASSERT_RETURN((size_t)addr_len < sizeof(szBuf), ERROR_INVALID_ARGS);
  ::memcpy(szBuf, ip_port, addr_len);
  szBuf[addr_len] = '\0';

  return Set(szBuf, prot);
}

int MediaAddress::TryResolve() {
  if (IsResolved()) {
    MLOG_ERROR_THIS("IsResolved");
    return ERROR_SUCCESS;
  }

  // try to get ip addr from DNS
  Std::shared_ptr<DnsRecord> pRecord;
  int ret = g_dns.AsyncResolve(pRecord.ParaOut(), host_name_);

  if (ERROR_SUCCESS == ret) {
    char strIpAddr[kAi_Addrlen] = {0};
    MA_ASSERT_RETURN(NULL != *(pRecord->begin()), ERROR_FAILURE);
    ::memcpy(strIpAddr, *(pRecord->begin()), kAi_Addrlen);
    ((sockaddr_in*)strIpAddr)->sin_port = sock_addr_.sin_port;
    this->SetIpAddr(reinterpret_cast<sockaddr*>(strIpAddr));
  } else {
    assert(!IsResolved());
  }
  return ret;
}

void MediaAddress::SetPort(uint16_t aPort) {
  sock_addr_.sin_port = htons(aPort);
}

std::string MediaAddress::GetIpDisplayName() const {
  if (!IsResolved())
    return host_name_;

  if (sock_addr_.sin_family == AF_INET) {
    char szBuf[INET_ADDRSTRLEN] = {0};
    const char* pAddr = Inet_ntop(sock_addr_.sin_family, &sock_addr_.sin_addr,
                                  szBuf, sizeof(szBuf));
    return std::string(pAddr);
  }

  if (sock_addr_.sin_family == AF_INET6) {
    char szBuf[INET6_ADDRSTRLEN] = {0};
    const char* pAddr = Inet_ntop(sock_addr_.sin_family, &sock_addr6_.sin6_addr,
                                  szBuf, sizeof(szBuf));
    return std::string(pAddr);
  }
  return std::string("");
}

std::string MediaAddress::GetIpAndPort() const {
  char ipAndPort[64] = {0};
  if (sock_addr_.sin_family == AF_INET)
    snprintf(ipAndPort, 64, "%s:%d", GetIpDisplayName().c_str(), GetPort());
  else if (sock_addr_.sin_family == AF_INET6)
    snprintf(ipAndPort, 64, "[%s]:%d", GetIpDisplayName().c_str(), GetPort());
  return std::string(ipAndPort);
}

uint16_t MediaAddress::GetPort() const {
  return ntohs(sock_addr_.sin_port);
}

bool MediaAddress::operator==(const MediaAddress& aRight) const {
  MA_ASSERT(IsResolved());

  // don't compare m_SockAddr.sin_zero due to getpeername() or getsockname()
  // will fill it with non-zero value.

  if (sock_addr_.sin_family == AF_INET)
    return (::memcmp(&sock_addr_, &aRight.sock_addr_,
                     sizeof(sock_addr_) - sizeof(sock_addr_.sin_zero)) == 0);

  int ret = memcmp(&sock_addr6_.sin6_addr, &aRight.sock_addr6_.sin6_addr,
                   sizeof(in6_addr));
  if (ret == 0)
    return (sock_addr6_.sin6_port == aRight.sock_addr6_.sin6_port);
  return false;
}

bool MediaAddress::operator<(const MediaAddress& right) const {
  assert(IsResolved());
  if (sock_addr_.sin_family == AF_INET) {
    return sock_addr_.sin_addr.s_addr < right.sock_addr_.sin_addr.s_addr ||
           (sock_addr_.sin_addr.s_addr == right.sock_addr_.sin_addr.s_addr &&
            sock_addr_.sin_port < right.sock_addr_.sin_port);
  }

  int ret = memcmp(&sock_addr6_.sin6_addr, &right.sock_addr6_.sin6_addr,
                   sizeof(in6_addr));
  if (ret < 0)
    return true;
  if (ret > 0)
    return false;

  return sock_addr6_.sin6_port < right.sock_addr6_.sin6_port;
}

int MediaAddress::SetIpAddr(const char* ip) {
  bool bIpv4 = true;
  struct in_addr ip4;
  struct in6_addr ip6;

  bool bAddrOk = Inet_pton(AF_INET, ip, &ip4) > 0 ? true : false;

  if (!bAddrOk) {
    bIpv4 = false;
    bAddrOk = Inet_pton(AF_INET6, ip, &ip6) > 0 ? true : false;
  }

  if (!bAddrOk) {
    MLOG_WARN_THIS("wrong aIpAddr=" << ip);
    return ERROR_FAILURE;
  }

  if (bIpv4)
    return SetIpAddr(AF_INET, &ip4);
  return SetIpAddr(AF_INET6, &ip6);
}

int MediaAddress::SetIpAddr(uint16_t family, void* addr) {
  if (family != AF_INET6 && family != AF_INET && !addr)
    return ERROR_INVALID_ARGS;

  // empty host_name_ to indicate resovled successfully.
  host_name_.resize(0);
  sock_addr_.sin_family = family;
  if (family == AF_INET) {
    sock_addr_.sin_addr.s_addr = *(uint32_t*)addr;
  } else {
    memcpy(&sock_addr6_.sin6_addr, addr, sizeof(sock_addr6_.sin6_addr));
  }
  return ERROR_SUCCESS;
}

void MediaAddress::SetIpAddr(struct sockaddr* addr) {
  host_name_.resize(0);
  assert(NULL != addr);
  size_t size = (addr->sa_family == AF_INET6 ? sizeof(sockaddr_in6)
                                             : sizeof(sockaddr_in));
  memcpy(&sock_addr_, addr, size);
}

uint32_t MediaAddress::GetSize() const {
  return (sock_addr_.sin_family == AF_INET ? sizeof(sockaddr_in)
                                           : sizeof(sockaddr_in6));
}

const sockaddr_in* MediaAddress::GetPtr() const {
  if (!IsResolved())
    return NULL;
  return (sockaddr_in*)&sock_addr_;
}

bool MediaAddress::IsIpv4Legal(const char* ip) {
  size_t count = 0;
  for (size_t i = 0; i < strlen(ip); i++) {
    if (!(ip[i] >= '0' && ip[i] <= '9')) {
      if (ip[i] == '.') {
        count++;
        continue;
      }
      return false;
    }
  }

  return (count == 3) ? true : false;
}

void MediaAddress::GetIpWithHostName(const char* hostName,
                                     std::vector<std::string>& result) {
  struct addrinfo hints, *res0, *res;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  int nRet = getaddrinfo(hostName, "", &hints, &res0);
  if (nRet) {
    MLOG_ERROR_THIS("getaddrinfo Errinfo: " << gai_strerror(nRet));
    return;
  }

  const char* pszTemp = NULL;
  for (res = res0; res; res = res->ai_next) {
    char buf[32] = {0};
    if (res->ai_family == AF_INET6) {
      struct sockaddr_in6* addr6 = (struct sockaddr_in6*)res->ai_addr;
      pszTemp = Inet_ntop(AF_INET6, &addr6->sin6_addr, buf, sizeof(buf));
    } else {
      struct sockaddr_in* addr = (struct sockaddr_in*)res->ai_addr;
      pszTemp = Inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
    }

    result.push_back(std::string(pszTemp));
  }

  freeaddrinfo(res0);
}
}  // namespace ma
