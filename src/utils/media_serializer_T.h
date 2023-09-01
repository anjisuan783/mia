#ifndef __MEDIA_SERIALIZER_T__
#define __MEDIA_SERIALIZER_T__

#include "common/media_define.h"
#include "utils/media_msg_chain.h"
#include "utils/media_kernel_buffer.h"

namespace ma {

template <class Convertor> 
class MediaSerializerT : public SrsBuffer {
 public:
	MediaSerializerT(MessageChain& msg)
		: msg_(msg), read_result_(0) , write_result_(0) { }

	int size() override {
		return msg_.GetChainedSpace();
	}
	int left() override {
		return msg_.GetChainedLength();
	}
	bool empty() override {
		return msg_.GetChainedLength() == 0;
	}
	bool require(int required_size) override {
		return (int)msg_.GetChainedLength() >= required_size;
	}
	void skip(int size) override {
		assert(size >= 0);
		msg_.AdvanceChainedReadPtr(size);
	}
	void save_reader() override {
		msg_.SaveChainedReadPtr();
	}
	void restore_reader() override {
		msg_.RewindChained(true);
	}
	int8_t read_1bytes() override {
		int8_t ret;
		*this >> ret;
		return ret;
	}
	int16_t read_2bytes() override {
		int16_t ret;
		*this >> ret;
		return ret;
	}
  int32_t read_3bytes() override {
		int32_t ret;
		Read(&ret, 3);
		Convertor::Swap24(ret);
		return ret;
	}
  int32_t read_4bytes() override {
		int32_t ret;
		*this >> ret;
		return ret;
	}
  int64_t read_8bytes() override {
		int64_t ret;
		*this >> ret;
		return ret;
	}
  std::string read_string(int len) override {
		std::string ret(len, ' ');
		Read(const_cast<char*>(ret.data()), len);
		return std::move(ret);
	}

  void read_bytes(char* data, int size) override {
		Read(data, size);
	}
  void write_1bytes(int8_t value) override {
		*this << value;
	}
  void write_2bytes(int16_t value) override {
		*this << value;
	}
	void write_3bytes(int32_t value) override {
		Convertor::Swap24(value);
		Write(&value, 3);
	}
  void write_4bytes(int32_t value) override {
		*this << value;
	}
  void write_8bytes(int64_t value) override {
		*this << value;
	}
  void write_string(const std::string& value) override {
		Write(value.c_str(), value.length());
	}
  void write_bytes(char* data, int size) override {
		Write(data, size);
	}

	MediaSerializerT& operator<<(uint8_t n) {
		return Write(&n, sizeof(uint8_t));
	}
	MediaSerializerT& operator>>(uint8_t& n) {
		return Read(&n, sizeof(uint8_t));
	}

	MediaSerializerT& operator<<(int8_t n) {
		return Write(&n, sizeof(int8_t));
	}
	MediaSerializerT& operator>>(int8_t& n) {
		return Read(&n, sizeof(int8_t));
	}

	MediaSerializerT& operator<<(uint16_t n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(uint16_t));
	}
	MediaSerializerT& operator>>(uint16_t& n) {
		Read(&n, sizeof(uint16_t));
		Convertor::Swap(n);
		return *this;
	}

	MediaSerializerT& operator<<(int16_t n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(int16_t));
	}
	MediaSerializerT& operator>>(int16_t& n) {
		Read(&n, sizeof(int16_t));
		Convertor::Swap(n);
		return *this;
	}

	MediaSerializerT& operator<<(uint32_t n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(uint32_t));
	}
	MediaSerializerT& operator>>(uint32_t& n) {
		Read(&n, sizeof(uint32_t));
		Convertor::Swap(n);
		return *this;
	}

	MediaSerializerT& operator<<(int32_t n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(int32_t));
	}
	MediaSerializerT& operator>>(int32_t& n) {
		Read(&n, sizeof(int32_t));
		Convertor::Swap(n);
		return *this;
	}
	
	MediaSerializerT& operator<<(float n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(float));
	}
	MediaSerializerT& operator>>(float& n) {
		Read(&n, sizeof(float));
		Convertor::Swap(n);
		return *this;
	}

	MediaSerializerT& operator<<(uint64_t n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(uint64_t));
	}
	MediaSerializerT& operator>>(uint64_t& n) {
		Read(&n, sizeof(uint64_t));
		Convertor::Swap(n);
		return *this;
	}

	MediaSerializerT& operator<<(int64_t n) {
		Convertor::Swap(n);
		return Write(&n, sizeof(int64_t));
	}
	MediaSerializerT& operator>>(int64_t& n) {
		Read(&n, sizeof(int64_t));
		Convertor::Swap(n);
		return *this;
	}

	// Not support bool because its sizeof is not fixed.
	MediaSerializerT& operator<<(bool n) = delete;
	MediaSerializerT& operator>>(bool& n) = delete;

	// Not support long double.
	MediaSerializerT& operator<<(long double n) = delete;
	MediaSerializerT& operator>>(long double& n) = delete;

	MediaSerializerT& Read(void *buf, uint32_t n) {
		if (MessageChain::error_ok == read_result_) {
			uint32_t nRead = 0;
			read_result_ = msg_.Read(buf, n, &nRead, true);
		}
		if (MessageChain::error_ok != read_result_) {
			assert(false);
		}
		return *this;
	}

	MediaSerializerT& Write(const void *buf, uint32_t n) {
		if (MessageChain::error_ok == write_result_) {
			uint32_t nWritten = 0;
			write_result_ = msg_.Write(buf, n, &nWritten);
		}
		if (MessageChain::error_ok != write_result_) {
			assert(false);
		}
		return *this;
	}
	
	bool IsOk() {
		if (MessageChain::error_ok == write_result_ && 
				MessageChain::error_ok == read_result_) {                           
			return true;
		}
		return false;
	}
 private:
	MessageChain& msg_;
	int read_result_ = MessageChain::error_ok;
	int write_result_ = MessageChain::error_ok;
};

class BEConvertor {
public:
	static void Swap(int64_t &aHostLong) {
#ifdef OS_LITTLE_ENDIAN
		Swap8(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(uint64_t &aHostLong) {
#ifdef OS_LITTLE_ENDIAN
		Swap8(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(int32_t &aHostLong) {
#ifdef OS_LITTLE_ENDIAN
		Swap4(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(uint32_t &aHostLong) {
#ifdef OS_LITTLE_ENDIAN
		Swap4(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(int16_t &aHostShort) {
#ifdef OS_LITTLE_ENDIAN
		Swap2(&aHostShort, &aHostShort);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(uint16_t &aHostShort) {
#ifdef OS_LITTLE_ENDIAN
		Swap2(&aHostShort, &aHostShort);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap24(int32_t &aHostLong) {
#ifdef OS_LITTLE_ENDIAN
		Swap3(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap24(uint32_t &aHostLong) {
#ifdef OS_LITTLE_ENDIAN
		Swap3(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void SwapFloat32(float& f) {
#ifdef OS_LITTLE_ENDIAN
		Swapf32(f,f);
#endif
	}

	// copied from ACE_CDR
	static void Swap2(const void *orig, void* target) {
		uint16_t usrc = * reinterpret_cast<const uint16_t*>(orig);
		uint16_t* udst = reinterpret_cast<uint16_t*>(target);
		*udst = (usrc << 8) | (usrc >> 8);
	}
	
	static void Swap3(const void* orig, void* target) {
		uint32_t x = *((uint32_t*)orig);
		x &= 0x00FFFFFF;
		x = ((x&0xff)<<16) | (x & 0xff00) | ((x & 0xff0000) >> 16);
		* reinterpret_cast<uint32_t*>(target) = x;
	}

	static void Swap4(const void* orig, void* target) {
		uint32_t x = * reinterpret_cast<const uint32_t*>(orig);
		x = (x << 24) | ((x & 0xff00) << 8) | ((x & 0xff0000) >> 8) | (x >> 24);
		* reinterpret_cast<uint32_t*>(target) = x;
	}
	
	static void Swap8(const void* orig, void* target) {
		uint32_t x = * reinterpret_cast<const uint32_t*>(orig);
		uint32_t y = 
				* reinterpret_cast<const uint32_t*>(static_cast<const char*>(orig) + 4);
		x = (x << 24) | ((x & 0xff00) << 8) | ((x & 0xff0000) >> 8) | (x >> 24);
		y = (y << 24) | ((y & 0xff00) << 8) | ((y & 0xff0000) >> 8) | (y >> 24);
		* reinterpret_cast<uint32_t*>(target) = y;
		* reinterpret_cast<uint32_t*>(static_cast<char*>(target) + 4) = x;
	}

	static void Swapf32(float& f1, float& f2) {
		typedef union{
			float f;
			char c[4];
		}FLOAT_CONV;
		FLOAT_CONV d1, d2;
		d1.f = f1;

		d2.c[0] = d1.c[3];
		d2.c[1] = d1.c[2];
		d2.c[2] = d1.c[1];
		d2.c[3] = d1.c[0];
		f2 = d2.f;
	}
	
protected:
	BEConvertor();
	~BEConvertor();
};

class LEConvertor {
public:
	static void Swap(int64_t &aHostLongLong) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap8(&aHostLongLong, &aHostLongLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(uint64_t &aHostLongLong) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap8(&aHostLongLong, &aHostLongLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(int32_t &aHostLong) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap4(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(uint32_t &aHostLong) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap4(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(int16_t &aHostShort) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap2(&aHostShort, &aHostShort);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap(uint16_t &aHostShort) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap2(&aHostShort, &aHostShort);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap24(int32_t &aHostLong) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap3(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void Swap24(uint32_t &aHostLong) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swap3(&aHostLong, &aHostLong);
#endif // OS_LITTLE_ENDIAN
	}
	static void SwapFloat32(float& f) {
#ifndef OS_LITTLE_ENDIAN
		BEConvertor::Swapf32(f,f);
#endif // OS_LITTLE_ENDIAN
	}

protected:
	LEConvertor() = default;
	~LEConvertor() = default;
};

using MediaStreamBE = MediaSerializerT<BEConvertor>;
using MediaStreamLE = MediaSerializerT<LEConvertor>;

} //namespace ma

#endif //!__MEDIA_SERIALIZER_T__
