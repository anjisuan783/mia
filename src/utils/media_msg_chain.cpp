#include "utils/media_msg_chain.h"

#include <atomic>

namespace ma {

DataBlock::DataBlock(int32_t aSize, const char* aData)
  : size_{aSize}, data_{const_cast<char*>(aData)} {
}

struct DataBlockDeleter {
  void operator()(DataBlock* data_block) const {
    uint32_t len = sizeof(DataBlock) + data_block->size_;
    data_block->~DataBlock();

    std::allocator<char> allocChar;
    allocChar.deallocate(static_cast<char*>(static_cast<void*>(data_block)), len);
  }
};


std::shared_ptr<DataBlock> DataBlock::Create(
    int32_t aSize, const char* inData) {

  // alloc sizeof(DataBlock) and <aSize> at one time.
  std::allocator<char> allocChar;
  char *pBuf = allocChar.allocate(sizeof(DataBlock) + aSize, nullptr);

  char* pData = pBuf + sizeof(DataBlock);
  if (inData) {
    ::memcpy(pData, inData, aSize);
  }
  
  return std::move(std::shared_ptr<DataBlock>(
      new (pBuf) DataBlock(aSize, pData), DataBlockDeleter()));
}

#ifndef MEDIA_NDEBUG
	#define SELFCHECK_MessageChain(pmb) \
	do { \
		MA_ASSERT(pmb->begin_ <= pmb->read_); \
		MA_ASSERT(pmb->read_ <= pmb->write_); \
		MA_ASSERT(pmb->write_ <= pmb->end_); \
	} while (0)
#else
	#define SELFCHECK_MessageChain(pmb)
#endif // MEDIA_NDEBUG


MDEFINE_LOGGER(MessageChain, "ma.utils"); 

std::atomic<int> s_block_createcount = 0;
std::atomic<int> s_block_destoycount = 0;

std::string MessageChain::GetBlockStatics() {
	char szBuffer[128] = {0};
	snprintf(szBuffer, sizeof(szBuffer), 
		" [msgblock c-%d d-%d]",
		s_block_createcount.load(), s_block_destoycount.load());
	return std::string(szBuffer);
}

MessageChain::MessageChain(uint32_t aSize, 
                           const char* aData, 
                           MFlag aFlag, 
                           uint32_t aAdvanceWritePtrSize)	{
  ++ s_block_createcount;
  if (aData && MA_BIT_DISABLED(aFlag, MessageChain::MALLOC_AND_COPY)) {
    MA_SET_BITS(aFlag, MessageChain::DONT_DELETE);
    begin_ = aData;
    read_ = begin_;
    write_ = const_cast<char*>(begin_);
    end_ = aData + aSize;
  } else {
#ifndef MEDIA_NDEBUG
    if (aData) {
      MA_ASSERT(MA_BIT_DISABLED(aFlag, MessageChain::DONT_DELETE));
    }
#endif // MEDIA_NDEBUG
		MA_CLR_BITS(aFlag, MessageChain::DONT_DELETE);
		if (aSize > 0) {
		  Reset(std::move(DataBlock::Create(aSize, nullptr)));
	  }
	}
	
	if (aAdvanceWritePtrSize > 0)
		AdvanceFirstMsgWritePtr(aAdvanceWritePtrSize);
	
	flag_ = aFlag;
	MA_CLR_BITS(flag_, MessageChain::MALLOC_AND_COPY);
	MA_CLR_BITS(flag_, MessageChain::INTERNAL_MASK);
}

MessageChain::MessageChain(std::shared_ptr<DataBlock> aDb) {
  Reset(std::move(aDb));
  write_ = const_cast<char*>(end_);
  MA_SET_BITS(flag_, DUPLICATED);
}

MessageChain::MessageChain(std::shared_ptr<DataBlock> aDb, MFlag aFlag) {
  ++ s_block_createcount;
  MA_ASSERT(MA_BIT_DISABLED(aFlag, MessageChain::DONT_DELETE));
  MA_CLR_BITS(aFlag, MessageChain::DONT_DELETE);

  Reset(std::move(aDb));
  flag_ = aFlag;
  MA_CLR_BITS(flag_, MessageChain::MALLOC_AND_COPY);
  MA_CLR_BITS(flag_, MessageChain::INTERNAL_MASK);
}

MessageChain::MessageChain(const MessageChain& r)
  : next_{r.next_},
    data_block_{r.data_block_}, 
    read_{r.read_}, 
    write_{r.write_},
    begin_{r.begin_},
    end_{r.end_},
    save_read_{r.save_read_},
    flag_{r.flag_} {
}

MessageChain::~MessageChain() {
  ++ s_block_destoycount;
}

int MessageChain::Peek(void* aDst, uint32_t aCount, uint32_t aPos, uint32_t* aBytesRead) {
  MA_ASSERT(MA_BIT_DISABLED(flag_, READ_LOCKED));
  uint32_t dwLen = GetFirstMsgLength();
  uint32_t dwHaveRead = 0;

  MA_ASSERT(write_ >= read_);
  if(aPos >= dwLen) {
    aPos -= dwLen;
    if(next_) {
      return next_->Peek(aDst, aCount, aPos, aBytesRead);
    }
    return error_part_data;
  }

  if(dwLen >= aCount + aPos) {
    if (aDst) {
      ::memcpy((char*)aDst+dwHaveRead, read_ + aPos, aCount);
    }
    dwHaveRead += aCount;
    if (aBytesRead) {
      *aBytesRead = dwHaveRead;
    }
    return error_ok;
  }
  
  if (aDst) {
    ::memcpy((char*)aDst+dwHaveRead, read_ + aPos, dwLen - aPos);
  }
  
  dwHaveRead += (dwLen - aPos);
  if (next_) {
    uint32_t dwNextRead;
    int rv = next_->Read(aDst ? (char*)aDst+dwHaveRead : aDst, 
                         aCount- dwHaveRead, 
                         &dwNextRead, 
                         false);
    dwHaveRead += dwNextRead;
    if (aBytesRead) {
      *aBytesRead = dwHaveRead;
    }
    return rv;
  } 
  
  if (aBytesRead) {
    *aBytesRead = dwHaveRead;

  }
  return error_part_data;
}

int MessageChain::Read(void* aDst, uint32_t aCount, uint32_t *aBytesRead, bool aAdvance) {
  MessageChain *pMbMove = this;
  uint32_t dwHaveRead = 0;
  bool bPartial = true;

  while(pMbMove)
  {
    MA_ASSERT(MA_BIT_DISABLED(pMbMove->flag_, READ_LOCKED));
    MA_ASSERT(pMbMove->write_ >= pMbMove->read_);
    uint32_t dwLen = pMbMove->GetFirstMsgLength();
    uint32_t dwNeedRead = aCount - dwHaveRead;
    dwNeedRead = (dwLen >= dwNeedRead) ? dwNeedRead : dwLen;
    if (aDst) {
      ::memcpy((char*)aDst+dwHaveRead, pMbMove->read_, dwNeedRead);
    }
    dwHaveRead += dwNeedRead;
    if (aAdvance) {
      pMbMove->read_ += dwNeedRead;
      MA_ASSERT(pMbMove->read_ <= pMbMove->write_);
    }
    
    if(dwHaveRead >= aCount) {
      bPartial = false;
      break;
    }
    pMbMove = pMbMove->next_;
  }

  if (aBytesRead) {
    *aBytesRead = dwHaveRead;
  }

  return bPartial ? error_part_data : error_ok;
}

int MessageChain::Write(void* aSrc, uint32_t aCount, uint32_t *aBytesWritten) {
  MA_ASSERT(MA_BIT_DISABLED(flag_, WRITE_LOCKED));
  uint32_t dwSpace = GetFirstMsgSpace();
  uint32_t dwHaveWritten = 0;

  if (dwSpace >= aCount) {
    dwHaveWritten = aCount;
    if (aSrc) {
      ::memcpy(write_, aSrc, aCount);
    }
    write_ += aCount;
    if (aBytesWritten) {
      *aBytesWritten = dwHaveWritten;
    }
    return error_ok;
  }
	else {
    dwHaveWritten = dwSpace;
    if (aSrc)
      ::memcpy(write_, aSrc, dwSpace);
    write_ += dwSpace;
    MA_ASSERT(write_ == end_);
    if (aBytesWritten)
      *aBytesWritten = dwHaveWritten;
    return error_part_data;
  }
}

uint32_t MessageChain::GetChainedLength() const {
  uint32_t dwRet = 0;
  const MessageChain *i = this;
#ifdef _ENABLE_EXCEPTION_
  try {
#endif
    for (; nullptr != i; i = i->next_)
      dwRet += i->GetFirstMsgLength();
#ifdef _ENABLE_EXCEPTION_
  } catch (...) {
    MLOG_ERROR("catch exception! i="<<i<<" len="<<dwRet
      <<" i->write_="<<(void*)i->write_<<" i->read_="<<(void*)i->read_);
    return 0;
  }
#endif
  return dwRet;
}

uint32_t MessageChain::GetChainedSpace() const {
  uint32_t dwRet = 0;
  for (const MessageChain *i = this; nullptr != i; i = i->next_) {
    dwRet += i->GetFirstMsgSpace();
  }
  return dwRet;
}

void MessageChain::Append(MessageChain *aMb) {
  if (!aMb) {
    return;
  }
  SELFCHECK_MessageChain(aMb);
  MessageChain *pMbMove = this;
  while (pMbMove) {
    MA_ASSERT(aMb != pMbMove);
    
    //find last
    if (pMbMove->next_) {
      pMbMove = pMbMove->next_;
      continue;
    }
    
    //last
    pMbMove->next_ = aMb;
    break;
  }
}

int MessageChain::AdvanceChainedReadPtr(uint32_t aCount, uint32_t *aBytesRead) {
  if(!aCount)
    return error_ok;
  int rv = Read(nullptr, aCount, aBytesRead);
  return rv;
}

int MessageChain::AdvanceChainedWritePtr(
    uint32_t aCount, uint32_t *aBytesWritten) {
  if(!aCount) {
    return error_ok;
  }
  
  MA_ASSERT(MA_BIT_DISABLED(flag_, WRITE_LOCKED));
  uint32_t dwNeedWrite = aCount;
  for (MessageChain *pCurrent = this; 
       nullptr != pCurrent && dwNeedWrite > 0; 
       pCurrent = pCurrent->next_) {
    MA_ASSERT(pCurrent->begin_ == pCurrent->read_);
    if (pCurrent->begin_ != pCurrent->read_) {
        MLOG_ERROR("can't advance."
                   " begin_=" << (void*)pCurrent->begin_ <<
                   " read_=" << (void*)pCurrent->read_);
      if (aBytesWritten) {
        *aBytesWritten = aCount - dwNeedWrite;
      }
      return error_part_data;
    }

    uint32_t dwLen = pCurrent->GetFirstMsgSpace();
    if (dwNeedWrite <= dwLen) {
      pCurrent->AdvanceFirstMsgWritePtr(dwNeedWrite);
      if (aBytesWritten) {
        *aBytesWritten = aCount;

      }
      return error_ok;
    } else {
      dwNeedWrite -= dwLen;
      pCurrent->AdvanceFirstMsgWritePtr(dwLen);
    }
  }

  MA_ASSERT(aCount > dwNeedWrite);
  if (aBytesWritten) {
    *aBytesWritten = aCount - dwNeedWrite;
  }
  return error_part_data;
}

MessageChain* MessageChain::DuplicateChained() {
	MessageChain *pRet = nullptr, *pNewMove = nullptr;
	MessageChain *pOldMove = this;

	while (pOldMove) {
		MessageChain *pDuplicate = pOldMove->DuplicateFirstMsg();
		if (!pDuplicate) {
			MLOG_WARN("return nullptr from DuplicateFirstMsg!");
			if (pRet)
				pRet->DestroyChained();
			return nullptr;
		}

		if (!pRet) {
			pRet = pDuplicate;
			MA_ASSERT(!pNewMove);
			pNewMove = pDuplicate;
		}
		else {
			MA_ASSERT(pNewMove);
			pNewMove->next_ = pDuplicate;
			pNewMove = pDuplicate;
		}

		pOldMove = pOldMove->next_;
	}
	return pRet;
}

MessageChain* MessageChain::DuplicateFirstMsg() const {
  MessageChain *pRet = nullptr;
  if (MA_BIT_ENABLED(flag_, MessageChain::DONT_DELETE)) {
    // <begin_> and <end_> are pointing to the actual data,  
    // and data_block_ is nullptr.
    MA_ASSERT(!data_block_);

    uint32_t dwLen = end_ - begin_;
    MFlag flagNew = flag_;
    MA_CLR_BITS(flagNew, MessageChain::DONT_DELETE);
    MA_SET_BITS(flagNew, MessageChain::MALLOC_AND_COPY);
    pRet = new MessageChain(dwLen, begin_, flagNew);

    if (dwLen) {
#ifdef _ENABLE_EXCEPTION_
      try {
#endif
        ::memcpy(pRet->GetFirstMsgWritePtr(), begin_, dwLen);
#ifdef _ENABLE_EXCEPTION_
      } catch (...) {
        MLOG_ERROR("catch exception! len="<<dwLen<<" begin_="<<(void*)begin_);
        delete pRet;
        return nullptr;
      }
#endif
    }
  } else {
    pRet = new MessageChain(data_block_, flag_);
  }

  /// <DataBlock> maybe realloc if DONT_DELETE,
  /// so can't do "pRet->read_ = read_;"
  pRet->read_ += (read_ - begin_);
  pRet->write_ += (write_ - begin_);
  MA_SET_BITS(pRet->flag_, DUPLICATED);
  SELFCHECK_MessageChain(pRet);

  return pRet;
}

MessageChain* MessageChain::Disjoint(uint32_t aStart) {
  if (aStart > GetChainedLength()) {
    MLOG_WARN("start="<<aStart<<" len="<<GetChainedLength());
    return nullptr;
  }

  // find the start point of disjointing.
  MessageChain *pFind = nullptr;
  for (MessageChain *pCurrent = this; pCurrent; pCurrent = pCurrent->next_) {
    uint32_t dwLen = pCurrent->GetFirstMsgLength();
    if (aStart == 0 && dwLen == 0) {
    } else if (aStart == dwLen) {
      pFind = pCurrent->next_;
      pCurrent->next_ = nullptr;
      break;
    } else if (aStart < dwLen) {
      pFind = pCurrent->DuplicateFirstMsg();
      if (pFind) {
        pFind->next_ = pCurrent->next_;
        pFind->read_ += aStart;
        SELFCHECK_MessageChain(pFind);

        pCurrent->write_ -= dwLen - aStart;
        pCurrent->next_ = nullptr;
        SELFCHECK_MessageChain(pCurrent);
      } else {
        MLOG_WARN("return nullptr from DuplicateFirstMsg!");
      }
      break;
    }
    else {
      aStart -= dwLen;
    }
  }

  // duplicate from <pFind>, if is DUPLICATED, need not duplicate again.
  MessageChain *pPrevious = nullptr;
  MessageChain *pMove = pFind;
  while (pMove) {
    if (MA_BIT_DISABLED(pMove->flag_, DUPLICATED)) {
      MLOG_WARN("there are not DUPLICATED blocks behind the disjointed block.");
      MessageChain *pNew = pMove->DuplicateFirstMsg();
      if (!pNew) {
      // the best way is rollback to destroy what duplicated in this function.
      // but duplicate failed will rare happen.
        return nullptr;
      }
      
      if (pFind == pMove) {
        // the <pFind> is not DUPLICATED, replace it
        pFind = pNew;
      } else if (pPrevious) {
        MA_ASSERT(pPrevious->next_ == pMove);
        pPrevious->next_ = pNew;
      }
      pNew->next_ = pMove->next_;
      pMove->next_ = nullptr;
      pMove = pNew->next_;
      pPrevious = pNew;
    } else {
      pPrevious = pMove;
      pMove = pMove->next_;
    }
  }
  return pFind;
}

int MessageChain::DestroyChained() {
  MessageChain *pMbMove = this;
  while (pMbMove) {
    MA_ASSERT(MA_BIT_ENABLED(pMbMove->flag_, DUPLICATED));
    if (MA_BIT_DISABLED(pMbMove->flag_, DUPLICATED)) {
      pMbMove = pMbMove->next_;
      continue;
    }

    MessageChain *pTmp = pMbMove->next_;
    delete pMbMove;
    pMbMove = pTmp;
  }

  return error_ok;
}

uint32_t MessageChain::FillIov(iovec* inIov, 
                               uint32_t inMax, 
                               uint32_t& outFillLength, 
                               const MessageChain*& outRemainder) const {
  outFillLength = 0;
  uint32_t fillNum =0;
  const MessageChain* i = this;
  for (; nullptr != i && fillNum < inMax; i = i->next_) {
    uint32_t dwLen = i->GetFirstMsgLength();
    if (dwLen > 0) {
      inIov[fillNum].iov_base = const_cast<char*>(i->GetFirstMsgReadPtr());
      inIov[fillNum].iov_len = dwLen;
      ++fillNum;
      outFillLength += dwLen;
    }
  }
  outRemainder = i;
  return fillNum;
}

void MessageChain::RewindChained(bool bRead) {
  // TODO: need record first read ptr
  for (MessageChain *i = this; nullptr != i; i = i->next_) {
    SELFCHECK_MessageChain(i);
    if(bRead) {
      MA_ASSERT(i->save_read_);
      i->read_ = i->save_read_;
    } else {
      i->write_ = const_cast<char*>(begin_);
    }
  }
}

void MessageChain::SaveChainedReadPtr() {
  for (MessageChain *i = this; nullptr != i; i = i->next_) {
    SELFCHECK_MessageChain(i);
    i->save_read_ = i->read_;
  }
}

MessageChain* MessageChain::ReclaimGarbage() {
  // find the start point of disjointing.
  MessageChain *pCurrent = this;
  while (pCurrent) {
    uint32_t dwLen = pCurrent->GetFirstMsgLength();
    if (dwLen == 0) {
      MessageChain *pTmp = pCurrent->next_;
      if (MA_BIT_ENABLED(pCurrent->flag_, DUPLICATED)) {
        delete pCurrent;
      }
      pCurrent = pTmp;
    } else {
      return pCurrent;
    }
  }
  return nullptr;
}

std::string MessageChain::FlattenChained() {
  std::string strRet;
  strRet.reserve(GetChainedLength() + 1);

  for (MessageChain *i = this; nullptr != i; i = i->next_) {
    strRet.append(i->GetFirstMsgReadPtr(), i->GetFirstMsgLength());
  }
  return std::move(strRet);
}

void MessageChain::Reset(std::shared_ptr<DataBlock> aDb) {
  begin_ = (aDb ? aDb->GetBasePtr() : nullptr);
  read_ = begin_;
  write_ = const_cast<char*>(begin_);
  end_ = begin_ + (aDb ? aDb->GetLength() : (uint32_t)0);
  data_block_ = std::move(aDb);
}

uint32_t MessageChain::GetFirstMsgLength() const {
  MA_ASSERT(write_ >= read_);
  return (uint32_t)(write_ - read_);
}

uint32_t MessageChain::GetFirstMsgSpace() const {
  MA_ASSERT(end_ >= write_);
  return (uint32_t)(end_ - write_);
}


const char* MessageChain::GetFirstMsgReadPtr() const {
  MA_ASSERT(MA_BIT_DISABLED(flag_, READ_LOCKED));
  return read_;
}

char* MessageChain::GetFirstMsgWritePtr() const {
  MA_ASSERT(MA_BIT_DISABLED(flag_, WRITE_LOCKED));
  return write_;
}

int MessageChain::AdvanceFirstMsgWritePtr(uint32_t aStep) {
  MA_ASSERT(MA_BIT_DISABLED(flag_, WRITE_LOCKED));
  MA_ASSERT_RETURN(write_ + aStep <= end_, error_invalid);
  write_ += aStep;
  return error_ok;
}

int MessageChain::AdvanceFirstMsgReadPtr(uint32_t aStep) {
  MA_ASSERT(MA_BIT_DISABLED(flag_, READ_LOCKED));
  MA_ASSERT_RETURN(write_ >= read_ + aStep, error_invalid);
  read_ += aStep;
  return error_ok;
}

void MessageChain::LockReading() {
  MA_SET_BITS(flag_, READ_LOCKED);
}

void MessageChain::LockWriting() {
  MA_SET_BITS(flag_, WRITE_LOCKED);
}

//TODO need optimizing
bool MessageChain::operator ==(const MessageChain& right) {
  if(this->GetChainedLength() != right.GetChainedLength()) {
    return false;
  }

  //length equal
  if (this->GetChainedLength() == 0) {
    return true;
  }

  if(this->FlattenChained() == const_cast<MessageChain&>(right).FlattenChained()) {
    return true;
  }
  return false;
}

} //namespace ma

