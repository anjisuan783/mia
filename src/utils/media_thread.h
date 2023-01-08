#ifndef __MEDIA_THREAD_H__
#define __MEDIA_THREAD_H__

#include <thread>
#include <mutex>
#include <condition_variable>
#include <pthread.h>

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
	void Create(const std::string& name);
	void Destory();

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
protected:
	MediaThread() = default;
	virtual ~MediaThread() = default;

	virtual void OnThreadInit();
	virtual void OnThreadRun() = 0;
private:
	static void ThreadProc(void *param);
	bool IsRunning();

protected:
	pthread_t handler_ = 0;
	bool stopped_ = false;
	unsigned long tid_;
	MediaThreadEvent event_;
private:
	std::unique_ptr<std::thread> worker_;
	std::string name_;

	friend class MediaThreadManager;
};

class MediaThreadManager final {
 public:
  static const int kForever = -1;

  static MediaThreadManager* Instance();

	MediaThread* CreateNetThread(const std::string& name);
	MediaThread* CreateTaskThread(const std::string& name);

  MediaThread* CurrentThread();
  void SetCurrentThread(MediaThread* thread);

  bool IsMainThread();
	static bool IsEqualCurrentThread(MediaThread*);
 private:
  MediaThreadManager();
  ~MediaThreadManager();

  pthread_key_t key_;

  const pthread_t main_thread_ref_;

	MediaThreadManager(const MediaThreadManager&) = delete;
	MediaThreadManager& operator=(const MediaThreadManager&) = delete
};

} // namespace ma

#endif //!__MEDIA_THREAD_H__
