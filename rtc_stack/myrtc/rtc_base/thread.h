/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_THREAD_H_
#define RTC_BASE_THREAD_H_

#include <stdint.h>

#include <list>
#include <memory>
#include <string>
#include <type_traits>

#if defined(WEBRTC_POSIX)
#include <pthread.h>
#endif
#include "rtc_base/constructor_magic.h"
#include "rtc_base/location.h"
#include "rtc_base/message_handler.h"
#include "rtc_base/message_queue.h"
#include "rtc_base/platform_thread_types.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/rtc_export.h"
#include "rtc_base/thread_annotations.h"

#if defined(WEBRTC_WIN)
#include "rtc_base/win32.h"
#endif

namespace rtc {

class Thread;

namespace rtc_thread_internal {

class MessageLikeTask : public MessageData {
 public:
  virtual void Run() = 0;
};

template <class FunctorT>
class MessageWithFunctor final : public MessageLikeTask {
 public:
  explicit MessageWithFunctor(FunctorT&& functor)
      : functor_(std::forward<FunctorT>(functor)) {}

  void Run() override { functor_(); }

 private:
  ~MessageWithFunctor() override {}

  typename std::remove_reference<FunctorT>::type functor_;

  RTC_DISALLOW_COPY_AND_ASSIGN(MessageWithFunctor);
};

class MessageHandlerWithTask final : public MessageHandler {
 public:
  MessageHandlerWithTask() = default;

  void OnMessage(Message* msg) override {
    static_cast<MessageLikeTask*>(msg->pdata)->Run();
    delete msg->pdata;
  }

 private:
  ~MessageHandlerWithTask() override {}

  RTC_DISALLOW_COPY_AND_ASSIGN(MessageHandlerWithTask);
};

}  // namespace rtc_thread_internal

class RTC_EXPORT ThreadManager {
 public:
  static const int kForever = -1;

  // Singleton, constructor and destructor are private.
  static ThreadManager* Instance();

  Thread* CurrentThread();
  void SetCurrentThread(Thread* thread);

  // Returns a thread object with its thread_ ivar set
  // to whatever the OS uses to represent the thread.
  // If there already *is* a Thread object corresponding to this thread,
  // this method will return that.  Otherwise it creates a new Thread
  // object whose wrapped() method will return true, and whose
  // handle will, on Win32, be opened with only synchronization privileges -
  // if you need more privilegs, rather than changing this method, please
  // write additional code to adjust the privileges, or call a different
  // factory method of your own devising, because this one gets used in
  // unexpected contexts (like inside browser plugins) and it would be a
  // shame to break it.  It is also conceivable on Win32 that we won't even
  // be able to get synchronization privileges, in which case the result
  // will have a null handle.
  Thread* WrapCurrentThread();
  void UnwrapCurrentThread();

  bool IsMainThread();

 private:
  ThreadManager();
  ~ThreadManager();

#if defined(WEBRTC_POSIX)
  pthread_key_t key_;
#endif

#if defined(WEBRTC_WIN)
  const DWORD key_;
#endif

  // The thread to potentially autowrap.
  const PlatformThreadRef main_thread_ref_;

  RTC_DISALLOW_COPY_AND_ASSIGN(ThreadManager);
};

struct _SendMessage {
  _SendMessage() {}
  Thread* thread;
  Message msg;
  bool* ready;
};

// WARNING! SUBCLASSES MUST CALL Stop() IN THEIR DESTRUCTORS!  See ~Thread().

class RTC_EXPORT RTC_LOCKABLE Thread : public MessageQueue {
 public:
  explicit Thread(SocketServer* ss);
  explicit Thread(std::unique_ptr<SocketServer> ss);
  // Constructors meant for subclasses; they should call DoInit themselves and
  // pass false for |do_init|, so that DoInit is called only on the fully
  // instantiated class, which avoids a vptr data race.
  Thread(SocketServer* ss, bool do_init);
  Thread(std::unique_ptr<SocketServer> ss, bool do_init);

  // NOTE: ALL SUBCLASSES OF Thread MUST CALL Stop() IN THEIR DESTRUCTORS (or
  // guarantee Stop() is explicitly called before the subclass is destroyed).
  // This is required to avoid a data race between the destructor modifying the
  // vtable, and the Thread::PreRun calling the virtual method Run().
  ~Thread() override;

  static std::unique_ptr<Thread> CreateWithSocketServer();
  static std::unique_ptr<Thread> Create();
  static Thread* Current();

  // Used to catch performance regressions. Use this to disallow blocking calls
  // (Invoke) for a given scope.  If a synchronous call is made while this is in
  // effect, an assert will be triggered.
  // Note that this is a single threaded class.
  class ScopedDisallowBlockingCalls {
   public:
    ScopedDisallowBlockingCalls();
    ScopedDisallowBlockingCalls(const ScopedDisallowBlockingCalls&) = delete;
    ScopedDisallowBlockingCalls& operator=(const ScopedDisallowBlockingCalls&) =
        delete;
    ~ScopedDisallowBlockingCalls();

   private:
    Thread* const thread_;
    const bool previous_state_;
  };

  bool IsCurrent() const;

  // Sleeps the calling thread for the specified number of milliseconds, during
  // which time no processing is performed. Returns false if sleeping was
  // interrupted by a signal (POSIX only).
  static bool SleepMs(int millis);

  // Sets the thread's name, for debugging. Must be called before Start().
  // If |obj| is non-null, its value is appended to |name|.
  const std::string& name() const { return name_; }
  bool SetName(const std::string& name, const void* obj);

  // Starts the execution of the thread.
  bool Start();

  // Tells the thread to stop and waits until it is joined.
  virtual void Stop();

  // By default, Thread::Run() calls ProcessMessages(kForever).  To do other
  // work, override Run().  To receive and dispatch messages, call
  // ProcessMessages occasionally.
  virtual void Run();

  virtual void Send(const Location& posted_from,
                    MessageHandler* phandler,
                    uint32_t id = 0,
                    MessageData* pdata = nullptr);

  // Convenience method to invoke a functor on another thread.  Caller must
  // provide the |ReturnT| template argument, which cannot (easily) be deduced.
  // Uses Send() internally, which blocks the current thread until execution
  // is complete.
  // Ex: bool result = thread.Invoke<bool>(RTC_FROM_HERE,
  // &MyFunctionReturningBool);
  // NOTE: This function can only be called when synchronous calls are allowed.
  // See ScopedDisallowBlockingCalls for details.
  // NOTE: Blocking invokes are DISCOURAGED, consider if what you're doing can
  // be achieved with PostTask() and callbacks instead.
  template <class ReturnT, class FunctorT>
  ReturnT Invoke(const Location& posted_from, FunctorT&& functor) {
    FunctorMessageHandler<ReturnT, FunctorT> handler(
        std::forward<FunctorT>(functor));
    InvokeInternal(posted_from, &handler);
    return handler.MoveResult();
  }

  // Posts a task to invoke the functor on |this| thread asynchronously, i.e.
  // without blocking the thread that invoked PostTask(). Ownership of |functor|
  // is passed and (usually, see below) destroyed on |this| thread after it is
  // invoked.
  // Requirements of FunctorT:
  // - FunctorT is movable.
  // - FunctorT implements "T operator()()" or "T operator()() const" for some T
  //   (if T is not void, the return value is discarded on |this| thread).
  // - FunctorT has a public destructor that can be invoked from |this| thread
  //   after operation() has been invoked.
  // - The functor must not cause the thread to quit before PostTask() is done.
  //
  // Destruction of the functor/task mimics what TaskQueue::PostTask does: If
  // the task is run, it will be destroyed on |this| thread. However, if there
  // are pending tasks by the time the Thread is destroyed, or a task is posted
  // to a thread that is quitting, the task is destroyed immediately, on the
  // calling thread. Destroying the Thread only blocks for any currently running
  // task to complete. Note that TQ abstraction is even vaguer on how
  // destruction happens in these cases, allowing destruction to happen
  // asynchronously at a later time and on some arbitrary thread. So to ease
  // migration, don't depend on Thread::PostTask destroying un-run tasks
  // immediately.
  //
  // Example - Calling a class method:
  // class Foo {
  //  public:
  //   void DoTheThing();
  // };
  // Foo foo;
  // thread->PostTask(RTC_FROM_HERE, Bind(&Foo::DoTheThing, &foo));
  //
  // Example - Calling a lambda function:
  // thread->PostTask(RTC_FROM_HERE,
  //                  [&x, &y] { x.TrackComputations(y.Compute()); });
  template <class FunctorT>
  void PostTask(const Location& posted_from, FunctorT&& functor) {
    // Allocate at first call, never deallocate.
    static auto* const handler =
        new rtc_thread_internal::MessageHandlerWithTask;
    Post(posted_from, handler, 0,
         new rtc_thread_internal::MessageWithFunctor<FunctorT>(
             std::forward<FunctorT>(functor)));
  }

  template <class FunctorT>
  void PostDelayTask(const Location& posted_from, FunctorT&& functor, int cms) {
    // Allocate at first call, never deallocate.
    static auto* const handler =
        new rtc_thread_internal::MessageHandlerWithTask;
    PostDelayed(posted_from, cms, handler, 0,
         new rtc_thread_internal::MessageWithFunctor<FunctorT>(
             std::forward<FunctorT>(functor)));
  }

  // From MessageQueue
  bool IsProcessingMessagesForTesting() override;
  void Clear(MessageHandler* phandler,
             uint32_t id = MQID_ANY,
             MessageList* removed = nullptr) override;
  void ReceiveSends() override;

  // ProcessMessages will process I/O and dispatch messages until:
  //  1) cms milliseconds have elapsed (returns true)
  //  2) Stop() is called (returns false)
  bool ProcessMessages(int cms);

  // Returns true if this is a thread that we created using the standard
  // constructor, false if it was created by a call to
  // ThreadManager::WrapCurrentThread().  The main thread of an application
  // is generally not owned, since the OS representation of the thread
  // obviously exists before we can get to it.
  // You cannot call Start on non-owned threads.
  bool IsOwned();

  // Expose private method IsRunning() for tests.
  //
  // DANGER: this is a terrible public API.  Most callers that might want to
  // call this likely do not have enough control/knowledge of the Thread in
  // question to guarantee that the returned value remains true for the duration
  // of whatever code is conditionally executing because of the return value!
  bool RunningForTest() { return IsRunning(); }

  // These functions are public to avoid injecting test hooks. Don't call them
  // outside of tests.
  // This method should be called when thread is created using non standard
  // method, like derived implementation of rtc::Thread and it can not be
  // started by calling Start(). This will set started flag to true and
  // owned to false. This must be called from the current thread.
  bool WrapCurrent();
  void UnwrapCurrent();

  // Sets the per-thread allow-blocking-calls flag to false; this is
  // irrevocable. Must be called on this thread.
  void DisallowBlockingCalls() { SetAllowBlockingCalls(false); }

 protected:
  // Same as WrapCurrent except that it never fails as it does not try to
  // acquire the synchronization access of the thread. The caller should never
  // call Stop() or Join() on this thread.
  void SafeWrapCurrent();

  // Blocks the calling thread until this thread has terminated.
  void Join();

  static void AssertBlockingIsAllowedOnCurrentThread();

  friend class ScopedDisallowBlockingCalls;

 private:
  // Sets the per-thread allow-blocking-calls flag and returns the previous
  // value. Must be called on this thread.
  bool SetAllowBlockingCalls(bool allow);

#if defined(WEBRTC_WIN)
  static DWORD WINAPI PreRun(LPVOID context);
#else
  static void* PreRun(void* pv);
#endif

  // ThreadManager calls this instead WrapCurrent() because
  // ThreadManager::Instance() cannot be used while ThreadManager is
  // being created.
  // The method tries to get synchronization rights of the thread on Windows if
  // |need_synchronize_access| is true.
  bool WrapCurrentWithThreadManager(ThreadManager* thread_manager,
                                    bool need_synchronize_access);

  // Return true if the thread is currently running.
  bool IsRunning();

  // Processes received "Send" requests. If |source| is not null, only requests
  // from |source| are processed, otherwise, all requests are processed.
  void ReceiveSendsFromThread(const Thread* source);

  // If |source| is not null, pops the first "Send" message from |source| in
  // |sendlist_|, otherwise, pops the first "Send" message of |sendlist_|.
  // The caller must lock |crit_| before calling.
  // Returns true if there is such a message.
  bool PopSendMessageFromThread(const Thread* source, _SendMessage* msg);

  void InvokeInternal(const Location& posted_from, MessageHandler* handler);

  std::list<_SendMessage> sendlist_;
  std::string name_;

  // TODO(tommi): Add thread checks for proper use of control methods.
  // Ideally we should be able to just use PlatformThread.

#if defined(WEBRTC_POSIX)
  pthread_t thread_ = 0;
#endif

#if defined(WEBRTC_WIN)
  HANDLE thread_ = nullptr;
  DWORD thread_id_ = 0;
#endif

  // Indicates whether or not ownership of the worker thread lies with
  // this instance or not. (i.e. owned_ == !wrapped).
  // Must only be modified when the worker thread is not running.
  bool owned_ = true;

  // Only touched from the worker thread itself.
  bool blocking_calls_allowed_ = true;

  friend class ThreadManager;

  RTC_DISALLOW_COPY_AND_ASSIGN(Thread);
};

// AutoThread automatically installs itself at construction
// uninstalls at destruction, if a Thread object is
// _not already_ associated with the current OS thread.

class AutoThread : public Thread {
 public:
  AutoThread();
  ~AutoThread() override;

 private:
  RTC_DISALLOW_COPY_AND_ASSIGN(AutoThread);
};

// AutoSocketServerThread automatically installs itself at
// construction and uninstalls at destruction. If a Thread object is
// already associated with the current OS thread, it is temporarily
// disassociated and restored by the destructor.

class AutoSocketServerThread : public Thread {
 public:
  explicit AutoSocketServerThread(SocketServer* ss);
  ~AutoSocketServerThread() override;

 private:
  rtc::Thread* old_thread_;

  RTC_DISALLOW_COPY_AND_ASSIGN(AutoSocketServerThread);
};

}  // namespace rtc

#endif  // RTC_BASE_THREAD_H_
