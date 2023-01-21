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
#include "utils/media_thread.h"

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

    bool operator==(const iterator& r) const { return (addr_ == r.addr_); }

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
  DnsManager();
  ~DnsManager();

  /**
   * gets ip address from cache or kicks off an asynchronous host lookup.
   *
   * @param record
   *        return DNS record corresponding to the given hostname.
   * @param hostname
   *        the hostname or IP-address-literal to resolve.
   * @param observer
   *        the listener to be notified when the result is available.
   * @param bypass_cache
   *        if true, the internal DNS lookup cache will be bypassed.
   * @param thread_listener
   *        optional parameter (may be null).  if non-null, this parameter
   *        specifies the MediaThread on which the listener's
   *        OnObserve should be called.  however, if this parameter is
   *        null, then OnObserve will be called on current thread.
   *
   * @return
   *        if srs_success, <record> is filled with corresponding record.
   *        else if ERROR_WOULD_BLOCK, <observer> will be callback late only
   *   once. You should call CancelResolve() before callback.
   *        else resolve hostname failed.
   */
  srs_error_t AsyncResolve(std::shared_ptr<DnsRecord>& record,
                           const std::string& hostname,
                           MediaObserver* observer = nullptr,
                           bool bypass_cache = false,
                           MediaThread* thread_listener = nullptr);

  // <aObserver> will not notified after calling this function.
  int CancelResolve(MediaObserver* observer);

  // close and release any resoruce.
  int Shutdown();

  /**
   * gets ip address from cache or resolve it at once,
   * the current thread will be blocking when resolving.
   *
   * @param record
   *        return DNS record corresponding to the given hostname.
   * @param hostname
   *        the hostname or IP-address-literal to resolve.
   * @param bypass_cache
   *        if true, the internal DNS lookup cache will be bypassed.
   *
   * @return
   *        if srs_success, <aRecord> is filled with corresponding record.
   *        else resolve hostname failed.
   */
  srs_error_t SyncResolve(std::shared_ptr<DnsRecord>& record,
                        const std::string& hostname,
                        bool bypass_cache = false);

 protected:
  // implement of MediaMsg
  srs_error_t OnFire() override;
  void OnDelete() override;

  // implement MediaTimerHelpSink
  void OnTimer(MediaTimerHelp* id) override;

 private:
  srs_error_t BeginResolve_l(std::shared_ptr<DnsRecord> record);
  int DoGetHostByName_l(DnsRecord* record);
  srs_error_t TryAddObserver_l(MediaObserver* aObserver,
                      MediaThread* thread_listener,
                      const std::string& hostname);
  srs_error_t Resolved_l(std::shared_ptr<DnsRecord> record, int error, bool callback = true);
  int DoCallback_l(int aError, const std::string& hostname);
  srs_error_t FindInCache_l(std::shared_ptr<DnsRecord>&, const std::string& hostname);

  void CreateDnsWorker();
 private:
  int GetAddrInfo_i(DnsRecord* record);

 private:
  typedef std::map<std::string, std::shared_ptr<DnsRecord>> CacheRecordsType;
  CacheRecordsType record_cache_;
  typedef std::list<std::shared_ptr<DnsRecord>> PendingRecordsType;
  PendingRecordsType pending_records_;

  class CObserverAndListener : public MediaMsg {
   public:
    CObserverAndListener(DnsManager* dnsmgr,
                         MediaObserver* obs,
                         MediaThread* listener,
                         int e,
                         const std::string& hostname)
        : manager_(dnsmgr),
          observer_(obs),
          listener_thread_(listener),
          error_(e),
          host_name_(hostname) {
      MA_ASSERT(manager_);
      MA_ASSERT(observer_);
      MA_ASSERT(listener_thread_);
    }

    bool operator==(const CObserverAndListener& r) {
      return observer_ == r.observer_;
    }

    bool operator==(MediaObserver* aObserver) { return observer_ == aObserver; }

    // interface Mediamsg
    srs_error_t OnFire() override;

    DnsManager* manager_;
    MediaObserver* observer_;
    MediaThread* listener_thread_;
    int error_;
    std::string host_name_;
  };
  typedef std::vector<CObserverAndListener> ObserversType;
  ObserversType observers_;

  typedef std::mutex MutexType;
  MutexType mutex_;

  MediaThread* dns_worker_ = nullptr;
  MediaTimerHelp expired_timer_;
};

// class DnsRecord
DnsRecord::DnsRecord(const std::string& host_name)
    : host_name_(host_name), resolved_(MediaTimeValue::GetDayTime()) {
  MA_ASSERT(!host_name_.empty());
  ::memset(buffer_, 0, sizeof(buffer_));
}

// class DnsManager
DnsManager::DnsManager() {
  expired_timer_.Schedule(this, MediaTimeValue(3, 0));
}

DnsManager::~DnsManager() {
  Shutdown();
}

srs_error_t DnsManager::AsyncResolve(std::shared_ptr<DnsRecord>& record,
                             const std::string& hostname,
                             MediaObserver* observer,
                             bool bypass_cache,
                             MediaThread* listener) {
  MA_ASSERT(!record);
  MLOG_INFO_THIS(" hostname=" << hostname << " observer=" << observer 
      << " bypass_cache=" << bypass_cache << " listener=" << listener->GetTid());

  srs_error_t err = srs_success;

  std::lock_guard<std::mutex> guard(mutex_);
  if (!bypass_cache) {
    if ((err = FindInCache_l(record, hostname)) != srs_success)
      return err;
  }
  std::shared_ptr<DnsRecord> r_new = std::make_shared<DnsRecord>(hostname);
  if (srs_success != (err = BeginResolve_l(r_new))) {
    return err;
  }
  if (srs_success != (err = Resolved_l(r_new, 0, false))) {
    return err;
  }
  if (srs_success != (err = TryAddObserver_l(observer, listener, hostname))) {
    return err;
  }
  return srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "try to resolve async");
}

srs_error_t DnsManager::SyncResolve(std::shared_ptr<DnsRecord>& record,
    const std::string& hostname, bool bypassCache) {
  MLOG_INFO_THIS("hostname=" << hostname << (bypassCache?" bypassCache":""));
  srs_error_t err = srs_success;
  std::lock_guard<std::mutex> guard(mutex_);
  if (!bypassCache) {
    if ((err = FindInCache_l(record, hostname)) != srs_success)
      return err;
  }

  std::shared_ptr<DnsRecord> new_record;
  for (auto iterPending = pending_records_.begin(); 
      iterPending != pending_records_.end(); ++iterPending) {
    if ((*iterPending)->GetHostName() == hostname) {
      MLOG_WARN_THIS("remove pending for hostname:" << hostname);
      new_record = (*iterPending);
      pending_records_.erase(iterPending);

      // TODO: If it's processing, wait util reloved.
      MA_ASSERT(new_record->state_ == DnsRecord::RSV_IDLE);
      break;
    }
  }

  if (!new_record) {
    new_record = std::make_shared<DnsRecord>(hostname);
  }

  pending_records_.push_front(new_record);
  int nErr = DoGetHostByName_l(new_record.get());

  err = Resolved_l(new_record, nErr, false);
  if (srs_success == err) {
    record = std::move(new_record);
    return err;
  }

  return err;
}

srs_error_t DnsManager::FindInCache_l(std::shared_ptr<DnsRecord>& record,
                              const std::string& hostname) {
  srs_error_t err = srs_success;
  CacheRecordsType::iterator iter = record_cache_.find(hostname);
  if (iter != record_cache_.end()) {
    record = (*iter).second;
    MA_ASSERT(hostname == record->host_name_);

    if (record->state_ == DnsRecord::RSV_SUCCESS) {
      return err;
    } else if (record->state_ == DnsRecord::RSV_FAILED) {
      return srs_error_new(ERROR_SYSTEM_DNS_RESOLVE, "dns resolve failed.");
    } else {
      MA_ASSERT(false);
      return srs_error_new(ERROR_UNEXPECTED, 
          "error state in record_cache_, host:%s, state:%d", hostname.c_str(), record->state_);
    }
  }
  return srs_error_new(ERROR_UNEXPECTED,"hostname:%s not found", hostname.c_str());
}

srs_error_t DnsManager::BeginResolve_l(std::shared_ptr<DnsRecord> record) {
  srs_error_t err = srs_success;
  for (auto& iter : pending_records_) {
    if (iter->GetHostName() == record->GetHostName()) {
      return err;
    }
  }
  pending_records_.push_back(record);

  if (!dns_worker_)
    CreateDnsWorker();

  err = dns_worker_->MsgQueue()->Post(this);
  return err;
}

int DnsManager::DoGetHostByName_l(DnsRecord* record) {
  MA_ASSERT(record);
  MA_ASSERT(record->state_ == DnsRecord::RSV_IDLE);
  record->state_ = DnsRecord::RSV_PROCESSING;

  // unlock the mutex because gethostbyname() will block the current thread.
  mutex_.unlock();
  int nError = GetAddrInfo_i(record);
  mutex_.lock();
  return nError;
}

int DnsManager::GetAddrInfo_i(DnsRecord* record) {
  int nError = 0;
  ::memset(record->buffer_, 0, sizeof(record->buffer_));

  struct addrinfo hints, *pTmp, *pResult;
  memset(&hints, 0, sizeof(hints));

  hints.ai_flags = AI_CANONNAME;
  nError = ::getaddrinfo(record->host_name_.c_str(), "", &hints, &pResult);
  if (nError == ERROR_SUCCESS) {
    char* pResutBuffer = record->buffer_;
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

srs_error_t DnsManager::TryAddObserver_l(MediaObserver* observer,
    MediaThread* listener,const std::string& hostname) {
  if (!observer)
    return srs_error_new(ERROR_INVALID_ARGS, "invalid args");

  if (!listener) {
    listener = MediaThreadManager::Instance()->CurrentThread();
    MA_ASSERT(listener);
  }

  for (auto iter = observers_.begin(); iter != observers_.end(); ++iter) {
    if ((*iter).observer_ == observer) {
      return srs_error_new(ERROR_EXISTED, 
          "observer already exist.observer=%lu, listener_threadid=%d", observer, listener->GetTid());
    }
  }

  observers_.push_back(CObserverAndListener(this, observer, listener, 0, hostname));
  return srs_success;
}

srs_error_t DnsManager::Resolved_l(std::shared_ptr<DnsRecord> record, int error, bool aCallback) {
  MA_ASSERT(record->state_ == DnsRecord::RSV_PROCESSING);
  MLOG_INFO_THIS("record=" << record << " hostname=" << record->host_name_
      << " error=" << error);

  srs_error_t err = srs_success;

  if (!error) {
    // it's successful.
    record->state_ = DnsRecord::RSV_SUCCESS;
  } else {
    record->state_ = DnsRecord::RSV_FAILED;
  }
  record->resolved_ = MediaTimeValue::GetDayTime();

  record_cache_[record->host_name_] = std::move(record);

  PendingRecordsType::iterator iter = pending_records_.begin();
  for (; iter!=pending_records_.end(); ++iter) {
    if ((*iter).get() == record.get())
      break;
  }

  if (iter != pending_records_.end()) {
    pending_records_.erase(iter);
  } else {
    return srs_error_new(ERROR_SYSTEM_DNS_RESOLVE, "can't find pending."
        " maybe it's removed due to Sync and Aysnc resolve the same hostname."
        " hsotname:%s", record->host_name_.c_str());
  }

  if (aCallback)
    DoCallback_l(error, record->host_name_);
  return err;
}

int DnsManager::DoCallback_l(int error, const std::string& hostname) {
  if (observers_.empty())
    return ERROR_SUCCESS;

  ObserversType obvOnCall(observers_);

  // don't hold the mutex when doing callback
  mutex_.unlock();
 
  for (auto iter = obvOnCall.begin(); iter != obvOnCall.end(); ++iter) {
    if ((*iter).host_name_ != hostname)
      continue;

    if (MediaThreadManager::IsEqualCurrentThread((*iter).listener_thread_)) {
      MediaObserver* observer = (*iter).observer_;
      if (observer) {
        // allow OnObserver() once.
        int rv = CancelResolve(observer);
        if (rv == ERROR_SUCCESS) {
          observer->OnObserve("DnsManager", &error);
        }
      }
    } else {
      MediaMsgQueue* queue = (*iter).listener_thread_->MsgQueue();
      if (queue) {
        CObserverAndListener* msg = new CObserverAndListener(*iter);
        msg->error_ = error;
        srs_error_t err = queue->Post(msg);
        if (err) {
          MLOG_ERROR_THIS("post dns result failed, listener tid=" 
              <<  (*iter).listener_thread_->GetTid() << ", desc=" << srs_error_desc(err));
          delete err;
        }
      }
    }
  }
  mutex_.lock();
  return ERROR_SUCCESS;
}

int DnsManager::CancelResolve(MediaObserver* observer) {
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto iter = observers_.begin(); iter != observers_.end(); ++iter) {
    if ((*iter).observer_ == observer) {
      observers_.erase(iter);
      return ERROR_SUCCESS;
    }
  }
  return ERROR_NOT_FOUND;
}

int DnsManager::Shutdown() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (dns_worker_) {
    dns_worker_->Stop();
    dns_worker_->Destroy();
    dns_worker_ = NULL;
  }

  observers_.clear();
  pending_records_.clear();
  record_cache_.clear();
  return ERROR_SUCCESS;
}

srs_error_t DnsManager::CObserverAndListener::OnFire() {
  MA_ASSERT(MediaThreadManager::IsEqualCurrentThread(listener_thread_));

  int rv = manager_->CancelResolve(observer_);
  if (rv == ERROR_SUCCESS && observer_)
    observer_->OnObserve("DnsManager", &error_);
  return srs_success;
}

void DnsManager::CreateDnsWorker() {
  if(!dns_worker_)
    dns_worker_ = MediaThreadManager::Instance()->CreateTaskThread("dns");
}

srs_error_t DnsManager::OnFire() {
  srs_error_t err = srs_success;
  MA_ASSERT(MediaThreadManager::IsEqualCurrentThread(dns_worker_));
  std::lock_guard<std::mutex> guard(mutex_);
  while (!pending_records_.empty()) {
    // must use std::shared_ptr to backup it, because DoGetHostByName_l()
    // maybe unlock the mutex and it's may remove in other thread.
    std::shared_ptr<DnsRecord> record = (*pending_records_.begin());
    int nError = DoGetHostByName_l(record.get());
    if (nError != ERROR_SUCCESS) {
      MLOG_ERROR_THIS(" failed hostName: " << record->host_name_
          << " errInfo: " << strerror(nError));
   }
    err = Resolved_l(record, nError);
  }
  return err;
}

void DnsManager::OnDelete() {
}

void DnsManager::OnTimer(MediaTimerHelp* id) {
  if (record_cache_.empty())
    return;

  MediaTimeValue tvCurrent = MediaTimeValue::GetDayTime();
  MediaTimeValue tvExpireInterval(3, 0);
  std::lock_guard<std::mutex> guard(mutex_);
  CacheRecordsType::iterator iter = record_cache_.begin();
  while (iter != record_cache_.end()) {
    DnsRecord* record = (*iter).second.get();
    if ((record->state_ == DnsRecord::RSV_SUCCESS ||
         record->state_ == DnsRecord::RSV_FAILED) &&
        (tvCurrent - record->resolved_ > tvExpireInterval)) {
      iter = record_cache_.erase(iter);
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

MediaAddress::MediaAddress(const char* hostname, uint16_t port) {
  host_name_.reserve(64);
  Set(hostname, port);
}

MediaAddress::MediaAddress(const char* ip_port) {
  host_name_.reserve(64);
  SetV4(ip_port);
}

MediaAddress::~MediaAddress() {}

int MediaAddress::Set(const char* host_name, uint16_t port) {
  if (!host_name || std::string(host_name).empty() || !port)
    return ERROR_INVALID_ARGS;

  ::memset(&sock_addr6_, 0, sizeof(sockaddr_in6));
  sock_addr_.sin_family = AF_INET;
  sock_addr_.sin_port = htons(port);
  int ret = SetIpAddr(host_name);
  if (ERROR_SUCCESS != ret) {
    host_name_ = host_name;
    srs_error_t err = srs_success;
    if (srs_success != (err = TryResolve())) {
      ret = srs_error_code(err);
      MLOG_ERROR_THIS(srs_error_desc(err));
      delete err;
    }
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

srs_error_t MediaAddress::TryResolve() {
  srs_error_t err = srs_success;
  if (IsResolved()) {
    MLOG_ERROR_THIS("IsResolved");
    return err;
  }

  // try to get ip addr from DNS
  std::shared_ptr<DnsRecord> record;

  if (srs_success == (err = g_dns.AsyncResolve(record, host_name_))) {
    char ip_addr[kAi_Addrlen] = {0};
    ::memcpy(ip_addr, *(record->begin()), kAi_Addrlen);
    ((sockaddr_in*)ip_addr)->sin_port = sock_addr_.sin_port;
    this->SetIpAddr(reinterpret_cast<sockaddr*>(ip_addr));
  } else {
    MA_ASSERT(!IsResolved());
  }
  return err;
}

void MediaAddress::SetPort(uint16_t port) {
  sock_addr_.sin_port = htons(port);
}

std::string MediaAddress::ToString() const {
  std::stringstream ss;
  if (!IsResolved()) {
    ss << host_name_;
  } else {
    if (sock_addr_.sin_family == AF_INET) {
      char szBuf[INET_ADDRSTRLEN] = {0};
      const char* pAddr = Inet_ntop(sock_addr_.sin_family, &sock_addr_.sin_addr,
                                    szBuf, sizeof(szBuf));
      ss << std::string(pAddr);
    } else if (sock_addr_.sin_family == AF_INET6) {
      char szBuf[INET6_ADDRSTRLEN] = {0};
      const char* pAddr = Inet_ntop(sock_addr_.sin_family, &sock_addr6_.sin6_addr,
                                    szBuf, sizeof(szBuf));
      ss << std::string(pAddr);
    }
  }
  ss << ":" << GetPort();
  return ss.str();
}

uint16_t MediaAddress::GetPort() const {
  return ntohs(sock_addr_.sin_port);
}

bool MediaAddress::operator==(const MediaAddress& right) const {
  MA_ASSERT(IsResolved());

  // don't compare m_SockAddr.sin_zero due to getpeername() or getsockname()
  // will fill it with non-zero value.

  if (sock_addr_.sin_family == AF_INET)
    return (::memcmp(&sock_addr_, &right.sock_addr_,
                     sizeof(sock_addr_) - sizeof(sock_addr_.sin_zero)) == 0);

  int ret = memcmp(&sock_addr6_.sin6_addr, &right.sock_addr6_.sin6_addr,
                   sizeof(in6_addr));
  if (ret == 0)
    return (sock_addr6_.sin6_port == right.sock_addr6_.sin6_port);
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
    MLOG_ERROR("getaddrinfo Errinfo: " << gai_strerror(nRet));
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
