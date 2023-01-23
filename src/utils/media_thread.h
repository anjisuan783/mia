#ifndef __MEDIA_THREAD_H__
#define __MEDIA_THREAD_H__

#include <sys/uio.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <pthread.h>
#include <unordered_map>

#include "common/media_kernel_error.h"

namespace ma {

#define MEDIA_INFINITE 0xFFFFFFFF 

class MediaReactor;
class MediaMsgQueue;
class MediaTimerQueue;
class MediaThreadManager;

class MediaThreadEvent {
 public:
  void Wait(int* ms = nullptr);
  void Signal();
 private:
  bool ready_ = false;
  std::mutex mutex_;
  std::condition_variable cv_;
};

class MediaThread {
 public:
	virtual void Create(const std::string& name);
	void Destroy();

	void Run();
	virtual srs_error_t Stop() = 0;
	void Join();	

	unsigned long GetTid();
	pthread_t GetThreadHandle();
	bool IsStopped() const;

	virtual MediaReactor* Reactor() = 0;
	virtual MediaMsgQueue* MsgQueue() = 0;
	virtual MediaTimerQueue* TimerQueue() = 0;
	
	const std::string& GetName() { return name_; }
	virtual void OnThreadRun() = 0;
protected:
	MediaThread() = default;
	virtual ~MediaThread() = default;

	virtual void OnThreadInit();	
private:
	static void ThreadProc(void *param);
	bool IsRunning();

protected:
	pthread_t handler_ = 0;
	bool stopped_ = false;
	unsigned long tid_;
	MediaThreadEvent event_;
	std::string name_;
private:
	std::unique_ptr<std::thread> worker_;

	friend class MediaThreadManager;
};

class MediaThreadManager final {
 public:
  static const int kForever = -1;

  static MediaThreadManager* Instance();

	static MediaThread* CreateNetThread(const std::string& name);
	static MediaThread* CreateTaskThread(const std::string& name);
	static MediaThread* FetchOrCreateMainThread();
	MediaThread* GetDefaultNetThread();

  MediaThread* CurrentThread();
  void SetCurrentThread(MediaThread* thread);

  bool IsMainThread();
	static bool IsEqualCurrentThread(MediaThread*);
 private:
  MediaThreadManager();
  ~MediaThreadManager();

  pthread_key_t key_;

  const pthread_t main_thread_ref_;
	static MediaThread* default_net_thread_;
	static MediaThread* main_thread_;
	
	MediaThreadManager(const MediaThreadManager&) = delete;
	MediaThreadManager& operator=(const MediaThreadManager&) = delete;
};

struct ThreadInfo;

class NetThreadManager final{
 public:
	int Register(MediaThread*);
	int UnRegister(MediaThread*);
  int GetIOBuffer(pthread_t handler, iovec*& iov, char*& iobuffer);
 protected:
  typedef std::unordered_map<pthread_t, std::shared_ptr<ThreadInfo>> ThreadInfoListType;
  ThreadInfoListType thread_infos_;
  std::mutex mutex_;
};

} // namespace ma

#endif //!__MEDIA_THREAD_H__
