//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//


#ifndef __MEDIA_EVENT_QUEUE_H__
#define __MEDIA_EVENT_QUEUE_H__

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

class MediaMsgQueue {
 public:
  // this function could be invoked in the different thread.
  // like PostMessage() in Win32 API.
  virtual srs_error_t Post(MediaMsg* aEvent) = 0;

  // this function could be invoked in the different thread.
  // like SendMessage() in Win32 API.
  virtual srs_error_t Send(MediaMsg* aEvent) = 0;

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
   *    if <th> exists in queue, return ERROR_SYSTEM_EXISTED;
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
   *    else return ERROR_SYSTEM_NOT_FOUND;
   */
  virtual srs_error_t Cancel(MediaTimerHandler* th) = 0;

 protected:
  virtual ~MediaTimerQueue() = default;
};

class MediaMsgQueueImp : public MediaMsgQueue {
 public:
	MediaMsgQueueImp();
	~MediaMsgQueueImp() override;

	// interface MediaMsgQueue
	srs_error_t Post(MediaMsg *msg) override;
	srs_error_t Send(MediaMsg *msg) override;
	int GetPendingNum() override;

  bool IsEmpty();
	void Stop();
	void DestoryPendingMsgs();

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

  void ResetThd();

private:
  MsgType msgs_;
	bool stopped_;

  pthread_t tid_;

	friend class SyncMsg;
};

class MediaMsgQueueWithMutex : public MediaMsgQueueImp  {
 public:
	MediaMsgQueueWithMutex();
	~MediaMsgQueueWithMutex() override;

	srs_error_t Post(MediaMsg *msg) override;

	void PopMsgs(MediaMsgQueueImp::MsgType &msgs, 
		uint32_t aMaxCount = MAX_GET_ONCE, 
		uint32_t *remain = NULL) override;

	void PopMsg(MediaMsg *&msg, uint32_t *aRemainSize = NULL) override;

	srs_error_t PostWithOldSize(MediaMsg *msg, uint32_t *old_size = NULL);
    
  int GetPendingNum() override;
private:
	typedef std::mutex MutexType;
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
      MediaMsgQueue *aEq);

  ~CalendarTQ() override;

  // interface IRtTimerQueue
  srs_error_t Schedule(MediaTimerHandler *aEh, 
            void* aToken, 
            const MediaTimeValue &aInterval,
            uint32_t aCount) override;

  srs_error_t Cancel(MediaTimerHandler *aEh) override;

  srs_error_t OnFire() override;
  void OnDelete() override;

  // timer tick every <slot_interval>.
  void TimerTick();
 private:
  using HandlerType = MediaTimerHandler*;
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
  MediaMsgQueue *event_queue_;
  SlotType *event_slot_ = nullptr;
  HanlersType handlers_;
};

} //namespace ma

#endif  // __MEDIA_EVENT_QUEUE_H__
