//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//


#ifndef __MEDIA_EVENT_QUEUE_H__
#define __MEDIA_EVENT_QUEUE_H__

#include <deque>
#include <mutex>
#include <list>
#include <map>
#include <condition_variable>
#include <type_traits>
#include <functional>

#include "media_time_value.h"
#include "common/media_kernel_error.h"

namespace ma {

class MediaTimeValue;

class MediaMsg {
 public:
  virtual srs_error_t OnFire() = 0;
  virtual void OnDelete();

 protected:
  virtual ~MediaMsg() = default;
};

template <class Closure>
class ClosureMsg : public MediaMsg {
 public:
  explicit ClosureMsg(Closure&& c)
    : c_(std::forward<Closure>(c)) {}
 private:
  srs_error_t OnFire() override {
    return c_();
  }

  typename std::decay<Closure>::type c_;
};

template <class Closure>
MediaMsg* ToQueueMsg(Closure&& c) {
  return new ClosureMsg<Closure>(std::forward<Closure>(c));
}

class MediaMsgQueue {
 public:
  typedef std::function<srs_error_t()> Task;

  srs_error_t Post(Task&& t) {
    return Post(ToQueueMsg<Task>(std::forward<Task>(t)));
  }
  virtual srs_error_t Post(MediaMsg* msg) = 0;

  srs_error_t Send(Task&& t) {
    return Send(ToQueueMsg<Task>(std::forward<Task>(t)));
  }
  virtual srs_error_t Send(MediaMsg* msg) = 0;

  // get the number of pending events.
  virtual int GetPendingNum() = 0;

 protected:
  virtual ~MediaMsgQueue() = default;
};

/**
 * @class MediaTimerHandler
 *
 * @brief Provides an abstract interface for handling timer msg.
 *
 * Subclasses handle a timer's expiration.
 *
 */
class MediaTimerHandler {
 public:
  /**
   * Called when timer expires.  <curTime> represents the current
   * time that the <MediaTimerHandler> was selected for timeout
   * dispatching and <args> is the asynchronous completion token that
   * was passed in when <Schedule> was invoked.
   * the return value is ignored.
   */
  virtual void OnTimeout(const MediaTimeValue& curTime, void* args) = 0;

 protected:
  virtual ~MediaTimerHandler() = default;
};

class MediaTimerQueue {
 public:
  /**
   * this function must be invoked in the own thread.
   * <interval> must be greater than 0.
   * If success:
   *    if <th> exists in queue, return ERROR_EXISTED;
   *    else return srs_success;
   */
  virtual srs_error_t Schedule(MediaTimerHandler* th,
              void* args,
              const MediaTimeValue& interval,
              uint32_t count) = 0;

  /**
   * this function must be invoked in the own thread.
   * If success:
   *    if <th> exists in queue, return srs_success;
   *    else return ERROR_NOT_FOUND;
   */
  virtual srs_error_t Cancel(MediaTimerHandler* th) = 0;

  void ResetThead() {
    tid_ = ::pthread_self();
  }
 protected:
  virtual ~MediaTimerQueue() = default;

  pthread_t tid_;
};

class MediaMsgQueueImp : public MediaMsgQueue {
 public:
	MediaMsgQueueImp();
	~MediaMsgQueueImp() override;

	// interface MediaMsgQueue
	srs_error_t Post(MediaMsg *msg) override;
	srs_error_t Send(MediaMsg *msg) override;
	int GetPendingNum() override;

  virtual bool IsEmpty();
	void Stop();
	virtual void DestoryPendingMsgs();

  void ResetThead() {
    tid_ = ::pthread_self();
  }

  enum { MAX_GET_ONCE = 5};
	typedef std::deque<MediaMsg*> MsgType;
	// Don't make the following two functions static because we want trace size.
	void Process(const MsgType &msgs);
	void Process(MediaMsg *msg);

protected:
	virtual void PopMsgs(MsgType &msgs, 
		uint32_t max_count = MAX_GET_ONCE, 
		uint32_t *remains = nullptr);

	virtual void PopMsg(MediaMsg *&msg, uint32_t *remains = NULL);
private:
  MsgType msgs_;
	bool stopped_ = false;
  pthread_t tid_ = -1;

	friend class SyncMsg;
};

class MediaMsgQueueWithMutex : public MediaMsgQueueImp  {
 public:
	MediaMsgQueueWithMutex() = default;
	~MediaMsgQueueWithMutex() override;

	srs_error_t Post(MediaMsg *msg) override;

	void PopMsgs(MediaMsgQueueImp::MsgType &msgs, 
               uint32_t aMaxCount = MAX_GET_ONCE, 
               uint32_t *remain = nullptr) override;

	void PopMsg(MediaMsg *&msg, uint32_t *remain_size = nullptr) override;

	srs_error_t PostWithOldSize(MediaMsg *msg, uint32_t *old_size = nullptr);
    
  int GetPendingNum() override;

  bool IsEmpty() override;

  void DestoryPendingMsgs() override;
private:
	using MutexType = std::mutex;
	MutexType mutex_;
};

class MediaTQBase : public MediaTimerQueue {
 public:
  MediaTQBase() = default;
  ~MediaTQBase() override = default;

  struct Node {
    Node(MediaTimerHandler* handler = nullptr, void* token = nullptr)
        : handler_(handler), token_(token), count_(0) {}
    MediaTimerHandler* handler_;
    void* token_;
    MediaTimeValue tv_expired_;
    MediaTimeValue tv_interval_;
    uint32_t count_;
  };

  srs_error_t Schedule(MediaTimerHandler* handler,
                      void* aToken,
                      const MediaTimeValue& aInterval,
                      uint32_t aCount) override;

  srs_error_t Cancel(MediaTimerHandler* handler) override;

  int Check(MediaTimeValue* aRemainTime = nullptr);

 protected:
  /// if the queue is empty, return MediaTimeValue::s_tvMax.
  MediaTimeValue GetEarliest();

  /// the following motheds are all called after locked
  virtual int PushNode_l(const Node& aPushNode) = 0;
  virtual int EraseNode_l(MediaTimerHandler* aEh) = 0;
  virtual int RePushNode_l(const Node& aPushNode) = 0;
  virtual int PopFirstNode_l(Node& aPopNode) = 0;
  virtual int GetEarliestTime_l(MediaTimeValue& aEarliest) const = 0;

  std::mutex mutex_;
  MediaTimeValue tick_value_;
};

class OrderedListTQ : public MediaTQBase {
public:
  OrderedListTQ();
  ~OrderedListTQ() override;

protected:
  virtual int PushNode_l(const Node &aPushNode);
  virtual int EraseNode_l(MediaTimerHandler *aEh);
  virtual int RePushNode_l(const Node &aPushNode);
  virtual int PopFirstNode_l(Node &aPopNode);
  virtual int GetEarliestTime_l(MediaTimeValue &aEarliest) const;

private:
  int EnsureSorted();

  using NodesType = std::list<Node>;
  NodesType nodes_;
};

template <class T>
class CalendarTQSlotT {
 public:
  CalendarTQSlotT *next_;
  T value_;
};

class CalendarTQ : public MediaTimerQueue, public MediaMsg {
 public:
  CalendarTQ(uint32_t slot_interval, uint32_t max_time, 
      MediaMsgQueue *queue);

  ~CalendarTQ() override;

  // MediaTimerQueue implement
  srs_error_t Schedule(MediaTimerHandler *handler, 
            void* token, 
            const MediaTimeValue &interval,
            uint32_t count) override;

  srs_error_t Cancel(MediaTimerHandler *aEh) override;

  srs_error_t OnFire() override;
  void OnDelete() override;

  // timer tick every <slot_interval>.
  void TimerTick();

 private:
  typedef MediaTimerHandler* HandlerType;
  struct ValueType {
    ValueType(HandlerType handler, void* token, 
          const MediaTimeValue &interval, uint32_t count) 
      : handler_(handler), token_(token), 
        time_interval_(interval), count_(count) { }

    bool operator == (const ValueType &aRight) const {
      return handler_ == aRight.handler_;
    }

    HandlerType handler_;
    void* token_;
    MediaTimeValue time_interval_;
    uint32_t count_;
  };
  
  typedef CalendarTQSlotT<ValueType> SlotType;
  typedef std::allocator<SlotType> AllocType;
  typedef std::map<HandlerType, uint32_t> HanlersType;

private:
  SlotType* NewSlot_i(const ValueType &aValue);
  void DeleteSlot_i(SlotType *aSlot);

  SlotType* RemoveUniqueHandler_i(const HandlerType &aHanler);
  SlotType* RemoveUniqueSlot_i(
    SlotType *&aFirst, 
    const HandlerType &aHanler);

  void InsertUnique_i(const MediaTimeValue &interval, SlotType *insert);

private:
  uint32_t interval_;
  SlotType **slots_;
  uint32_t max_slot_count_;
  uint32_t current_slot_;
  AllocType alloc_;
  MediaMsgQueue *msg_queue_;
  SlotType *event_slot_ = nullptr;
  HanlersType handlers_;
};

} //namespace ma

#endif  // __MEDIA_EVENT_QUEUE_H__
