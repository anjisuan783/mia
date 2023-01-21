#include "media_msg_queue.h"

#include "common/media_log.h"
#include "media_time_value.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

void MediaMsg::OnDelete() {
  delete this;
}

class SyncMsg : public MediaMsg {
public:
	SyncMsg(MediaMsg *event, MediaMsgQueue *eq);
	~SyncMsg() override;

	// interface IRtEvent
	srs_error_t OnFire() override;
	void OnDelete() override;

	srs_error_t WaitResultAndDeleteThis();

private:
	MediaMsg *msg_ = nullptr;
	srs_error_t result_ = nullptr;
	MediaMsgQueue *eq_;
	bool destoryed_;
  std::condition_variable condition_;
  std::mutex mutex_;
};

///////////////////////////////////////////////////////////////
//SyncMsg
SyncMsg::SyncMsg(MediaMsg *msg, MediaMsgQueue *eq)
  : msg_(msg), eq_(eq), destoryed_(false) {
}

SyncMsg::~SyncMsg() {
  if (msg_)
    msg_->OnDelete();
  if (!destoryed_)
    condition_.notify_all();
}

srs_error_t SyncMsg::OnFire() {
  srs_error_t err = nullptr;
  if (destoryed_) return err;
  
  if (msg_)
    result_ = msg_->OnFire();

  condition_.notify_all();
  return err;
}

void SyncMsg::OnDelete() {
  if (destoryed_) delete this; 
  else {
    destoryed_ = true;
    if (msg_) {
      msg_->OnDelete();
      msg_ = nullptr;
    }
  }
}

srs_error_t SyncMsg::WaitResultAndDeleteThis() {
  std::unique_lock<std::mutex> lock(mutex_);
  condition_.wait(lock);

  if (eq_)
    eq_->Post(this);
  return result_;
}

/////////////////////////////////////////////////////////////
//MediaMsgQueueImp
MediaMsgQueueImp::MediaMsgQueueImp() = default;

MediaMsgQueueImp::~MediaMsgQueueImp() {
  DestoryPendingMsgs();  
}

void MediaMsgQueueImp::DestoryPendingMsgs() {
  for(auto i : msgs_)
    i->OnDelete();
  msgs_.clear();
}

srs_error_t MediaMsgQueueImp::Post(MediaMsg *msg) {
  if (stopped_) {
    msg->OnDelete();
    return srs_error_new(ERROR_INVALID_STATE, "msg queue stopped.");
  }
  msgs_.push_back(msg);
  return nullptr;
}

srs_error_t MediaMsgQueueImp::Send(MediaMsg *msg) {
  srs_error_t err = srs_success;
  if (stopped_) {
    msg->OnDelete();
    return srs_error_new(ERROR_INVALID_STATE, "msg queue stopped.");
  }

  // if send event to the current thread, just do callbacks.
  if (::pthread_equal(tid_, ::pthread_self())) {
    srs_error_t rv = msg->OnFire();
    msg->OnDelete();
    return rv;
  }

  SyncMsg* sync = new SyncMsg(msg, this);
  if (srs_success != (err = Post(sync))) {
    return err;
  }
  err = sync->WaitResultAndDeleteThis();
  return err;
}

int MediaMsgQueueImp::GetPendingNum() {
  return msgs_.size();
}

bool MediaMsgQueueImp::IsEmpty() {
  return msgs_.empty();
}

void MediaMsgQueueImp::PopMsgs(
    MsgType &msgs, uint32_t max, uint32_t *remain) {
  size_t dwTotal = msgs_.size();
  if (dwTotal == 0) return ;
  
  if (dwTotal <= max) {
    msgs.swap(msgs_);
  } else {
    for (uint32_t i = 0; i < max; i++) {
      msgs.push_back(msgs_.front());
      msgs_.pop_front();
    }
  }

  if (remain)
    *remain = msgs_.size();
}

void MediaMsgQueueImp::PopMsg(MediaMsg *&msg, uint32_t *remain) {
  if (msgs_.empty()) {
    msg = nullptr;
    return ;
  }

  msg = msgs_.front();
  msgs_.pop_front();

  if (remain)
    *remain = msgs_.size();
}

void MediaMsgQueueImp::Process(const MsgType &msgs) {
  for(auto i : msgs) 
    Process(i);
}

void MediaMsgQueueImp::Process(MediaMsg *msg) {
  msg->OnFire();
  msg->OnDelete();
}

void MediaMsgQueueImp::Stop() {
  stopped_ = true;
}

/////////////////////////////////////////////////////////////
//MediaMsgQueueWithMutex
MediaMsgQueueWithMutex::~MediaMsgQueueWithMutex() = default;

srs_error_t MediaMsgQueueWithMutex::Post(MediaMsg *msg) {
  std::lock_guard<std::mutex> guard(mutex_);
  return MediaMsgQueueImp::Post(msg);
}

srs_error_t MediaMsgQueueWithMutex::
PostWithOldSize(MediaMsg *msg, uint32_t *old_size) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (old_size)
    *old_size = MediaMsgQueueImp::GetPendingNum();
  return MediaMsgQueueImp::Post(msg);
}

void MediaMsgQueueWithMutex::PopMsgs(
    MediaMsgQueueImp::MsgType &msgs, 
    uint32_t max, 
    uint32_t *remain) {
  std::lock_guard<std::mutex> guard(mutex_);
  MediaMsgQueueImp::PopMsgs(msgs, max, remain);
}

void MediaMsgQueueWithMutex::PopMsg(MediaMsg *&msg, uint32_t *remain) {
  std::lock_guard<std::mutex> guard(mutex_);
  MediaMsgQueueImp::PopMsg(msg, remain);
}

int MediaMsgQueueWithMutex::GetPendingNum() {
  std::lock_guard<std::mutex> guard(mutex_);
  return MediaMsgQueueImp::GetPendingNum();
}

bool MediaMsgQueueWithMutex::IsEmpty() {
  std::lock_guard<std::mutex> guard(mutex_);
  return MediaMsgQueueImp::IsEmpty();
}

void MediaMsgQueueWithMutex::DestoryPendingMsgs() {
  std::lock_guard<std::mutex> guard(mutex_);
  MediaMsgQueueImp::DestoryPendingMsgs();
}

/////////////////////////////////////////////////////////////
//MediaTQBase
int MediaTQBase::Check(MediaTimeValue *aRemainTime) {
  int nCout = 0;
  MediaTimeValue tvCur = MediaTimeValue::GetDayTime();

  /// Follow up linux kernel bug on gettimeofday() jumping into the future. 
  if (tick_value_ > tvCur) {
    MLOG_ERROR_THIS("time fall back! last=" << tick_value_.GetTimeInMsec()
      <<" cur=" << tick_value_.GetTimeInMsec()
      <<" diff=" << (tick_value_.GetTimeInMsec() - tvCur.GetTimeInMsec()));
  } else if (tvCur.GetSec()>tick_value_.GetSec()+3600 && tick_value_.GetSec() != 0) {
    MLOG_ERROR_THIS("big jump ahead! last=" << tick_value_.GetSec()
      <<" cur=" << tvCur.GetSec() <<" diff=" << tvCur.GetSec()-tick_value_.GetSec());
  }
  tick_value_ = tvCur;

  while(true) {
    MediaTimerHandler *handler = nullptr;
    void* token = nullptr;
    {
      MediaTimeValue tvEarliest;
      std::lock_guard<std::mutex> theGuard(mutex_);
      int nRet = GetEarliestTime_l(tvEarliest);
      if (nRet == -1) {
        if (aRemainTime)
          *aRemainTime = MediaTimeValue::time_max_;
        break;
      } else if (tvEarliest > tvCur) {
        if (aRemainTime){
          tvCur = MediaTimeValue::GetDayTime();
          *aRemainTime = tvEarliest - tvCur;
        }
        break;
      }

      Node ndFirst;
      nRet = PopFirstNode_l(ndFirst);
      MA_ASSERT(nRet == 0);

      handler = ndFirst.handler_;
      token = ndFirst.token_;
      if (ndFirst.count_ != (uint32_t)-1)
        ndFirst.count_--;

      if (ndFirst.count_ > 0 && ndFirst.tv_interval_ > MediaTimeValue::time_zero_) {
        do {
          ndFirst.tv_expired_ += ndFirst.tv_interval_;
        } while (ndFirst.tv_expired_ <= tvCur);

        RePushNode_l(ndFirst);
      }
    }

    MA_ASSERT(handler);
    handler->OnTimeout(tvCur, token);
    nCout++;
  }

  return nCout;
}

srs_error_t MediaTQBase::Schedule(MediaTimerHandler *handler, void* token, 
    const MediaTimeValue &aInterval, uint32_t aCount) {
  MA_ASSERT_RETURN(handler, srs_error_new(ERROR_INVALID_ARGS, "invalid handler"));
  MA_ASSERT_RETURN(aInterval > MediaTimeValue::time_zero_ || aCount == 1, 
      srs_error_new(ERROR_INVALID_ARGS, "invalid interval(%d) or count(%u)",
          aInterval.GetTimeInMsec(), aCount));
  
  int nRet;
  {
    std::lock_guard<std::mutex> theGuard(mutex_);
    MediaTimeValue tvTmp;
    
    Node ndNew(handler, token);
    ndNew.tv_interval_ = aInterval;
    ndNew.tv_expired_ = MediaTimeValue::GetDayTime() + aInterval;
    if (aCount > 0) {
      ndNew.count_ = aCount;
    } else {
      ndNew.count_ = (uint32_t)-1;
    }
    
    nRet = PushNode_l(ndNew);
  }

  if (nRet == 0) {
    return srs_success;
  } 
  if (nRet == 1)
    return srs_error_new(ERROR_EXISTED, "existed handler.");
  
  return srs_error_new(ERROR_FAILURE, "nRet:%d", nRet);
}

srs_error_t MediaTQBase::Cancel(MediaTimerHandler *handler) {
  MA_ASSERT_RETURN(handler, srs_error_new(ERROR_INVALID_ARGS, "invalid handler"));
  std::lock_guard<std::mutex> theGuard(mutex_);

  int nRet = EraseNode_l(handler);
  if (nRet == 0)
    return srs_success;
  
  if (nRet == 1)
    return srs_error_new(ERROR_EXISTED, "existed handler.");
  
  return srs_error_new(ERROR_FAILURE, "nRet:%d", nRet);
}

MediaTimeValue MediaTQBase::GetEarliest() {
  MediaTimeValue tvRet;
  std::lock_guard<std::mutex> theGuard(mutex_);

  int nRet = GetEarliestTime_l(tvRet);
  if (nRet == 0)
    return tvRet;
  
  return MediaTimeValue::time_max_;
}

///////////////////////////////////////////////////////////////////////
//OrderedListTQ
OrderedListTQ::OrderedListTQ() = default;

OrderedListTQ::~OrderedListTQ() = default;

int OrderedListTQ::EraseNode_l(MediaTimerHandler *handler) {
  auto iter = nodes_.begin();
  while (iter != nodes_.end()) {
    if ((*iter).handler_ == handler) {
      nodes_.erase(iter);
      return 0;
    }
    ++iter;
  }
  return 1;
}

int OrderedListTQ::PopFirstNode_l(Node &aPopNode) {
  MA_ASSERT_RETURN(!nodes_.empty(), -1);
  aPopNode = nodes_.front();
  nodes_.pop_front();
  return 0;
}

int OrderedListTQ::RePushNode_l(const Node &aPushNode) {
  auto iter = nodes_.begin();
  for(; iter != nodes_.end(); ++iter) {
    if ((*iter).tv_expired_ >= aPushNode.tv_expired_) {
      nodes_.insert(iter, aPushNode);
      break;
    }
  }
  if (iter == nodes_.end())
    nodes_.insert(iter, aPushNode);
  return 0;
}

int OrderedListTQ::GetEarliestTime_l(MediaTimeValue &aEarliest) const {
  if (!nodes_.empty()) {
    aEarliest = nodes_.front().tv_expired_;
    return 0;
  }
  
  return -1;
}

int OrderedListTQ::EnsureSorted() {
  return 0;
}

int OrderedListTQ::PushNode_l(const Node &aPushNode) {
  bool bFoundEqual = false;
  bool bInserted = false;
  auto iter = nodes_.begin();
  while (iter != nodes_.end()) {
    if (iter->handler_ == aPushNode.handler_) {
      MA_ASSERT(!bFoundEqual);
      iter = nodes_.erase(iter);
      bFoundEqual = true;
      if (bInserted || iter == nodes_.end())
        break;
    }

    if (!bInserted && (*iter).tv_expired_ >= aPushNode.tv_expired_) {
      iter = nodes_.insert(iter, aPushNode);
      ++iter;
      bInserted = true;
      if (bFoundEqual)
        break;
    }
    ++iter;
  }

  if (!bInserted)
    nodes_.push_back(aPushNode);

  return bFoundEqual ? 1 : 0;
}

///////////////////////////////////////////////////////////////////////
//CalendarTQ
CalendarTQ::CalendarTQ(uint32_t slot_interval, uint32_t max_time, MediaMsgQueue *queue)
  : interval_(slot_interval)
  , current_slot_(0)
  , msg_queue_(queue)
  , event_slot_(nullptr)
{
  if (interval_ < 10)
    interval_ = 10;

  if (max_time >= interval_)
    max_slot_count_ = max_time / interval_ - 1;
  if (max_slot_count_ < 10)
    max_slot_count_ = 10;

  slots_ = new SlotType*[max_slot_count_ + 1];
  ::memset(slots_, 0, sizeof(SlotType *) * (max_slot_count_+1));

  MA_ASSERT(queue);
}

CalendarTQ::~CalendarTQ() {
  SlotType *pFirst = event_slot_;
  while (pFirst) {
    SlotType *pTmp = pFirst;
    pFirst = pTmp->next_;
    DeleteSlot_i(pTmp);
  }
  
  for (uint32_t i = 0; i <= max_slot_count_; i++) {
    SlotType *pFirst = slots_[i];
    while (pFirst) {
      SlotType *pTmp = pFirst;
      pFirst = pTmp->next_;
      DeleteSlot_i(pTmp);
    }
  }
  delete []slots_;
}

srs_error_t CalendarTQ::Schedule(MediaTimerHandler* handler,
                      void* token,
                      const MediaTimeValue& interval,
                      uint32_t count) {
  // find slot first.
  bool bFound = false;
  SlotType *pFind = NULL;
  if (!pFind) {
    // alloc slot if not found.
    ValueType valueNew(handler, token, interval, 
        count > 0 ? count : (uint32_t)-1);
    pFind = NewSlot_i(valueNew);
  } else {
    MA_ASSERT(pFind->value_.handler_ == handler);
    pFind->value_.token_ = token;
    pFind->value_.time_interval_ = interval;
    pFind->value_.count_ = count > 0 ? count : (uint32_t)-1;
    pFind->next_ = nullptr;
    bFound = true;
  }
  
  srs_error_t err = srs_success;
  if (interval == MediaTimeValue::time_zero_) {
    // if interval is 0, use event queue instead.
    MA_ASSERT(count == 1);
    bool bNeedPost = event_slot_ ? false : true;
    pFind->next_ = event_slot_;
    event_slot_ = pFind;
    
    if (bNeedPost) {
      err = msg_queue_->Post(this);
      if (srs_success!= err) {
        event_slot_ = event_slot_->next_;
        DeleteSlot_i(pFind);
      }
    }
    if (bFound) {
      err = srs_error_wrap(err, "handler existed:%llu", handler);
    }
    return err;
  }

  InsertUnique_i(interval, pFind);
  if (bFound) {
    err = srs_error_new(ERROR_EXISTED, "handler existed:%llu", handler);
  }
  return err;
}

void CalendarTQ::InsertUnique_i(const MediaTimeValue &aInterval, SlotType *aInsert) {
  int64_t dwMs = aInterval.GetTimeInMsec();
  uint32_t distance = dwMs / interval_;
  if (dwMs % interval_)
    ++distance;

  // max_slot_count_ - 1 to avoid OnTimer infinite times.
  if (distance > max_slot_count_ - 1) {
    MLOG_ERROR_THIS("exceed max interval."
      " interval_s=" << aInterval.GetSec() << 
      " interval_us=" << aInterval.GetUsec() << 
      " distance=" << distance << 
      " max_slot_count_=" << max_slot_count_);
    distance = max_slot_count_;
  }

  if (distance > max_slot_count_ - current_slot_) {
    distance -= max_slot_count_ - current_slot_;
  } else {
    distance += current_slot_;
  }

  aInsert->next_ = slots_[distance];
  slots_[distance] = aInsert;
  handlers_[aInsert->value_.handler_] = distance;
}

srs_error_t CalendarTQ::Cancel(MediaTimerHandler *handler) {
  srs_error_t err = srs_success;
  SlotType *pFind = RemoveUniqueHandler_i(handler);
  if (pFind) {
    HanlersType::size_type nErase = 
      handlers_.erase(pFind->value_.handler_);
    MA_ASSERT(nErase == 1);

    DeleteSlot_i(pFind);
    return err;
  }
  
  return srs_error_new(ERROR_NOT_FOUND, "handler:%llu", handler);
}

void CalendarTQ::TimerTick() {
  uint32_t dwCur = current_slot_;
  SlotType *pFirst = slots_[dwCur];
  if (pFirst)
    slots_[dwCur] = pFirst->next_;

  MediaTimeValue tvCur = MediaTimeValue::GetDayTime();
  while (pFirst) {
    MA_ASSERT(pFirst->value_.time_interval_ > MediaTimeValue::time_zero_);
    HandlerType handlerOn = pFirst->value_.handler_;
    void* token = pFirst->value_.token_;
    
    if (--pFirst->value_.count_ > 0) {
      InsertUnique_i(pFirst->value_.time_interval_, pFirst);
    } else {
      HanlersType::size_type nErase = 
        handlers_.erase(pFirst->value_.handler_);
      MA_ASSERT(nErase == 1);
      
      DeleteSlot_i(pFirst);
    }

    handlerOn->OnTimeout(tvCur, token);

    pFirst = slots_[dwCur];
    if (pFirst)
      slots_[dwCur] = pFirst->next_;
  }

  // advance <current_slot_> after process timer callback.
  if (current_slot_ == max_slot_count_)
    current_slot_ = 0;
  else
    ++current_slot_;
}

srs_error_t CalendarTQ::OnFire() {
  MediaTimeValue tvCur = MediaTimeValue::GetDayTime();
  SlotType *pFirst = event_slot_;
  event_slot_ = nullptr;
  
  while (pFirst) {
    SlotType *pTmp = pFirst;
    MA_ASSERT(pTmp->value_.time_interval_ == MediaTimeValue::time_zero_);
    pTmp->value_.handler_->OnTimeout(tvCur, pTmp->value_.token_);

    pFirst = pTmp->next_;
    DeleteSlot_i(pTmp);
  }
  return nullptr;
}

void CalendarTQ::OnDelete() {
}

CalendarTQ::SlotType* CalendarTQ::
RemoveUniqueHandler_i(const HandlerType &handler) {
  auto iter = handlers_.find(handler);
  if (iter == handlers_.end()) {
    // remove handler in event slot.
    SlotType *pMove = event_slot_;
    SlotType *pPreTmp = NULL;
    while (pMove) {
      if (pMove->value_.handler_ == handler) {
        if (pPreTmp)
          pPreTmp->next_ = pMove->next_;
        else
          event_slot_ = pMove->next_;
        DeleteSlot_i(pMove);
        break;
      }
      else {
        pPreTmp = pMove;
        pMove = pMove->next_;
      }
    }

    return NULL;
  }

  uint32_t dwIndex = (*iter).second;
  MA_ASSERT(dwIndex <= max_slot_count_);

  SlotType *pRet = RemoveUniqueSlot_i(slots_[dwIndex], handler);
  return pRet;
}

CalendarTQ::SlotType* CalendarTQ::
RemoveUniqueSlot_i(SlotType *&aFirst, const HandlerType &handler) {
  if (aFirst) {
    if (aFirst->value_.handler_ == handler) {
      SlotType *pRet = aFirst;
      aFirst = aFirst->next_;
      return pRet;
    }

    SlotType *pCur = aFirst;
    SlotType *pNext = pCur->next_;
    while (pNext) {
      if (pNext->value_.handler_ == handler) {
        pCur->next_ = pNext->next_;
        return pNext;
      }
      else {
        pCur = pNext;
        pNext = pNext->next_;
      }
    }
  }
  return nullptr;
}

CalendarTQ::SlotType* 
CalendarTQ::NewSlot_i(const ValueType &value) {
  SlotType *pNew = alloc_.allocate(1, nullptr);
  if (pNew) {
    pNew->next_ = nullptr;
    new (&pNew->value_) ValueType(value);
  }
  return pNew;
}

void CalendarTQ::DeleteSlot_i(SlotType *aSlot) {
  aSlot->value_.~ValueType();
  alloc_.deallocate(aSlot, 1);
}

} //namespace ma
