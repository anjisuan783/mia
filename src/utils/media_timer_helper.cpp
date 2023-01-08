#include "media_timer_helper.h"

#include "common/media_log.h"
#include "media_thread.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

//////////////////////////////////////////////////////////////////////
// class MediaTimerHelp
MediaTimerHelp::MediaTimerHelp() = default;

MediaTimerHelp::~MediaTimerHelp() {
  srs_error_t err = Cancel();
  if (err != srs_success) {
    MLOG_WARN_THIS("Cancel failed, desc:%s", srs_error_desc(err));
    delete err;
  }
}

srs_error_t MediaTimerHelp::Schedule(const MediaTimerHelpSink* sink,
                                     const MediaTimeValue& inInterval,
                                     uint32_t count) {
  if (!sink)
    return srs_error_new(ERROR_INVALID_ARGS, "sink is nullptr");

  srs_error_t error = srs_success;

  if (pthread_ == nullptr) {
    MA_ASSERT(!scheduled_);
    pthread_ = MediaThreadManager::Instance()->CurrentThread();
    MediaTimerQueue* timer_queue = nullptr;
    if (pthread_)
      timer_queue = pthread_->TimerQueue();

    if (!timer_queue) {
      return srs_error_new(ERROR_NOT_FOUND, "timer queue is null!")
    }
  }

  if (scheduled_) {
    if ((error = Cancel()) != srs_success) {
      return srs_error_wrap(error, "cancel timer failed!");
    }
  }

  if (!MediaThreadManager::IsEqualCurrentThread(pthread_)) {
    return srs_error_new(ERROR_FAILURE, "thread not match! open threadid=",
        pthread_->GetThreadHandle());
  }

  scheduled_ = true;
  count_ = count;
  return pthread_->TimerQueue()->Schedule(this, (void*)sink, inInterval, count);
}

bool MediaTimerHelp::IsScheduled() {
  return scheduled_;
}

srs_error_t MediaTimerHelp::Cancel() {
  srs_error_t err = srs_success;
  if (!scheduled_)
    return err;

  if (pthread_->IsStopped())
    return srs_error_new(ERROR_INVALID_STATE, "thread stopped.");

  scheduled_ = false;
  count_ = 0;

  return pthread_->TimerQueue()->Cancel(this);
}

void MediaTimerHelp::OnTimeout(const MediaTimeValue&, void* args) {
  MA_ASSERT(scheduled_);

  if (count_ > 0) {
    --count_;
    if (count_ == 0)
      scheduled_ = false;
  }
  MediaTimerHelpSink* pSink = static_cast<MediaTimerHelpSink*>(args);
  MA_ASSERT(pSink);
#ifdef _ENABLE_EXCEPTION_
  try {
#endif
    if (pSink)
      pSink->OnTimer(this);
#ifdef _ENABLE_EXCEPTION_
  } catch (...) {
    MLOG_ERROR_THIS("catch exception! sink=" << pSink);
  }
#endif
}

}  // namespace ma
