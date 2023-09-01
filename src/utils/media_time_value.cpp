#include "media_time_value.h"

#include <climits>
#include <sys/time.h>

#include "common/media_define.h"

namespace ma {

const MediaTimeValue MediaTimeValue::time_zero_;
const MediaTimeValue MediaTimeValue::time_max_(LONG_MAX, Media_ONE_SECOND_IN_USECS - 1);

void MediaTimeValue::Normalize() {
  if (usecond_ >= Media_ONE_SECOND_IN_USECS) {
    do {
      second_++;
      usecond_ -= Media_ONE_SECOND_IN_USECS;
    } while (usecond_ >= Media_ONE_SECOND_IN_USECS);
  } else if (usecond_ <= -Media_ONE_SECOND_IN_USECS) {
    do {
      second_--;
      usecond_ += Media_ONE_SECOND_IN_USECS;
    } while (usecond_ <= -Media_ONE_SECOND_IN_USECS);
  }

  if (second_ >= 1 && usecond_ < 0) {
    second_--;
    usecond_ += Media_ONE_SECOND_IN_USECS;
  } else if (second_ < 0 && usecond_ > 0) {
    second_++;
    usecond_ -= Media_ONE_SECOND_IN_USECS;
  }
}

MediaTimeValue MediaTimeValue::GetDayTime() {
  timeval tvCur;
  ::gettimeofday(&tvCur, NULL);

  static timeval s_lastTick = {0, 0};
  if (tvCur.tv_sec > s_lastTick.tv_sec + 3600 && s_lastTick.tv_sec != 0) {
    ::gettimeofday(&tvCur, NULL);
  }
  s_lastTick = tvCur;

  return MediaTimeValue(tvCur);
}

//   functions
MediaTimeValue::MediaTimeValue() : second_(0), usecond_(0) {}

MediaTimeValue::MediaTimeValue(time_t sec) : second_(sec), usecond_(0) {}

MediaTimeValue::MediaTimeValue(time_t sec, int64_t usec) {
  Set(sec, usec);
}

MediaTimeValue::MediaTimeValue(const timeval& tv) {
  Set(tv);
}

MediaTimeValue::MediaTimeValue(double sec) {
  Set(sec);
}

void MediaTimeValue::Set(time_t sec, int64_t usec) {
  second_ = sec;
  usecond_ = usec;
  Normalize();
}

void MediaTimeValue::Set(const timeval& aTv) {
  second_ = aTv.tv_sec;
  usecond_ = aTv.tv_usec;
  Normalize();
}

void MediaTimeValue::Set(double sec) {
  int64_t l = (int64_t)sec;
  second_ = l;
  usecond_ = (int64_t)((sec - (double)l) * Media_ONE_SECOND_IN_USECS);
  Normalize();
}

void MediaTimeValue::SetByMsec(int64_t milliseconds) {
  second_ = milliseconds / 1000;
  usecond_ = (int64_t)(milliseconds - (second_ * 1000)) * 1000;
}

time_t MediaTimeValue::GetSec() const {
  return second_;
}

int64_t MediaTimeValue::GetUsec() const {
  return usecond_;
}

int64_t MediaTimeValue::GetTimeInMsec() const {
  return second_ * 1000 + usecond_ / 1000;
}

int64_t MediaTimeValue::GetTimeInUsec() const {
  return second_ * 1000000 + usecond_;
}

int operator>(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  if (aLeft.GetSec() > aRight.GetSec()) {
    return 1;
  }
  if (aLeft.GetSec() == aRight.GetSec() &&
           aLeft.GetUsec() > aRight.GetUsec()) {
    return 1;
  }

  return 0;
}

int operator>=(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  if (aLeft.GetSec() > aRight.GetSec()) {
    return 1;
  }
  
  if (aLeft.GetSec() == aRight.GetSec() &&
           aLeft.GetUsec() >= aRight.GetUsec()) {
    return 1;
  }
  return 0;
}

int operator<(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  return aRight > aLeft;
}

int operator<=(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  return aRight >= aLeft;
}

int operator==(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  return aLeft.GetSec() == aRight.GetSec() &&
         aLeft.GetUsec() == aRight.GetUsec();
}

int operator!=(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  return !(aLeft == aRight);
}

void MediaTimeValue::operator+=(const MediaTimeValue& aRight) {
  second_ = GetSec() + aRight.GetSec();
  usecond_ = GetUsec() + aRight.GetUsec();
  Normalize();
}

void MediaTimeValue::operator-=(const MediaTimeValue& aRight) {
  second_ = GetSec() - aRight.GetSec();
  usecond_ = GetUsec() - aRight.GetUsec();
  Normalize();
}

MediaTimeValue operator+(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  return MediaTimeValue(aLeft.GetSec() + aRight.GetSec(),
                      aLeft.GetUsec() + aRight.GetUsec());
}

MediaTimeValue operator-(const MediaTimeValue& aLeft, const MediaTimeValue& aRight) {
  return MediaTimeValue(aLeft.GetSec() - aRight.GetSec(),
                      aLeft.GetUsec() - aRight.GetUsec());
}

}
