
#ifndef __MEDIA_TIME_VALUE_H__
#define __MEDIA_TIME_VALUE_H__

#include <time.h>
#include <inttypes.h>

namespace ma {

class MediaTimeValue {
public:
	MediaTimeValue();
	MediaTimeValue(time_t sec);
	MediaTimeValue(time_t sec, int64_t usec);
	MediaTimeValue(const timeval &tv);
	MediaTimeValue(double sec);
	
	void Set(time_t second, int64_t usec);
	void Set(const timeval &tv);
	void Set(double sec);

	time_t GetSec() const ;
	int64_t GetUsec() const ;

	void SetByMsec(int64_t mseconds);
	int64_t GetTimeInMsec() const;
	int64_t GetTimeInUsec() const;

	void operator += (const MediaTimeValue &aRight);
	void operator -= (const MediaTimeValue &aRight);

	friend MediaTimeValue operator + (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend MediaTimeValue operator - (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend int operator < (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend int operator > (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend int operator <= (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend int operator >= (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend int operator == (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);
	friend int operator != (const MediaTimeValue &aLeft, const MediaTimeValue &aRight);

	static MediaTimeValue GetDayTime();
	static const MediaTimeValue time_zero_;
	static const MediaTimeValue time_max_;
	
private:
	void Normalize();
	
	time_t second_;
	int64_t usecond_;
};

} //namespace ma

#endif // !RTTIMEVALUE_H
