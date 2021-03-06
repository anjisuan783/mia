//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __RTC_OBJECTPOOL_H__
#define __RTC_OBJECTPOOL_H__

namespace webrtc {

template<typename ObjType>
class ObjectPoolT {
 public:
	ObjectPoolT(int pre_size) {
    pool_.reserve(pre_size);
    for(int i = 0; i < pre_size; ++i)
      pool_.push_back(new ObjType);
  }
	
	~ObjectPoolT() {
	  for (auto& i : pool_) {
      delete i;
    }
	}

	ObjType* New() {
	  bool pool_empty = pool_.empty();
	  ObjType* ret = pool_empty ? (new ObjType) : pool_.back();
    if (!pool_empty)
      pool_.pop_back();
    
		return ret;
	}
	
	void Delete(ObjType* p) {
		if (p == nullptr)
			return ;
    
		pool_.push_back(p);
	}

private:
	typedef std::vector<ObjType*> PoolType;

	PoolType pool_;
};

}

#endif // __RTC_OBJECTPOOL_H__

