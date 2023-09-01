#ifndef __MEDIA_TIMER_HELPER_H__
#define __MEDIA_TIMER_HELPER_H__

#include "utils/media_msg_queue.h"
#include "common/media_kernel_error.h"

namespace ma {

class MediaThread;
class MediaTimerHelp;
class MediaTimeValue;

class MediaTimerHelpSink {
public:
	virtual void OnTimer(MediaTimerHelp* id) = 0;

protected:
	virtual ~MediaTimerHelpSink() = default;
};

class MediaTimerHelp final : public MediaTimerHandler {
public:
	MediaTimerHelp();
	virtual ~MediaTimerHelp();

	srs_error_t Schedule(const MediaTimerHelpSink* aSink,
		const MediaTimeValue &interval, uint32_t count = 0);

	// Cancel the timer.
	srs_error_t Cancel();
	bool IsScheduled();
protected:
	void OnTimeout(const MediaTimeValue& curTime, void* args) override;

private:
	bool scheduled_ = false;
	MediaThread* pthread_ = nullptr;
	uint32_t count_ = 0;
};

} // namespace ma

#endif //!__MEDIA_TIMER_HELPER_H__
