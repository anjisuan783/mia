//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_MESSAGE_CHAIN_H__
#define __MEDIA_MESSAGE_CHAIN_H__


#include <sys/uio.h>
#include <inttypes.h>
#include <memory>
#include <string>

#include "common/media_log.h"

namespace ma {

#define MA_BIT_ENABLED(dword, bit) (((dword) & (bit)) != 0)
#define MA_BIT_DISABLED(dword, bit) (((dword) & (bit)) == 0)
#define MA_BIT_CMP_MASK(dword, bit, mask) (((dword) & (bit)) == mask)
#define MA_SET_BITS(dword, bits) (dword |= (bits))
#define MA_CLR_BITS(dword, bits) (dword &= ~(bits))

class DataBlock;

/*
 * The concept of <MessageChain> is mainly copyed from <ACE_Message_Block>
 * http://www.cs.wustl.edu/~schmidt/ACE.html
 *
 * An <MessageChain> is modeled after the message data
 * structures used in System V STREAMS.  Its purpose is to
 * enable efficient manipulation of arbitrarily-large messages
 * without incurring much memory copying overhead.  Here are the
 * main characteristics of an <MessageChain>:
 * 1. Contains a pointer to a reference-counted
 *    <DataBlock>, which in turn points to the actual data
 *    buffer.  This allows very flexible and efficient sharing of
 *    data by multiple <MessageChain>s.
 * 2. One or more <MessageChain> can be linked to form a
 *    'fragment chain.'
 *
 *	The internal structure of <MessageChain>:
 *     |<----------------------(Chained MessageChain)---------------------->|
 *     |<---(TopLevel MessageChain)--->|           | (Next MessageChain) |
 *     xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx (m_pNext) xxxxxxxxxxxxxxxxxxxxxxxxxx
 *     ^           ^          ^           ^---------->^
 *     |           |          |           |
 *     m_pBeginPtr m_pReadPtr m_pWritePtr m_pEndPtr 
 *                 |<-Length->|<--Space-->|
 *     |<------Capacity------------------>|
 *
 *  If <m_Flag> is set with <DONT_DELETE>, <m_pBeginPtr> and <m_pEndPtr> are  
 *  pointing to the actual data while <m_pDataBlock> is NULL.
 */

class MessageChain final {
  MDECLARE_LOGGER();
  
 public:
  enum {
    error_ok,
    error_invalid,
    error_part_data,
    error_invalid_arg,
    error_out_of_memory
  };
 
  enum {
    // Don't delete the data on exit since we don't own it.
    DONT_DELETE = 1 << 0,

    // Malloc and copy data internally
    MALLOC_AND_COPY = 1 << 1,

    // Can't read/write.
    READ_LOCKED = 1 << 8,
    WRITE_LOCKED = 1 << 9,

    // the following flags are only for internal purpose.
    INTERNAL_MASK = 0xFF00,
    DUPLICATED = 1 << 17
  };
  using MFlag = uint32_t;

  MessageChain() = default;
  MessageChain(const MessageChain &);
  /**
  * Create an initialized message containing <aSize> bytes. 
  * AdvanceTopLevelWritePtr for <aAdvanceWritePtrSize> bytes.
  * If <aData> == 0 then we create and own the <aData>;
  * If <aData> != 0 
  *   If <aFlag> == DONT_DELETE, do nothing when destruction;
  *   Else delete it when destruction;
  */
  explicit MessageChain(uint32_t size, 
                        const char * data = NULL, 
                        MFlag flag = 0, 
                        uint32_t increaseWritePtr = 0);
                        
  explicit MessageChain(std::shared_ptr<DataBlock> db);

  ~MessageChain();

  MessageChain(MessageChain &&) = delete;
  
  void operator = (const MessageChain&) = delete;
  void operator = (MessageChain &&) = delete;

  // Read <count> bytes, advance it if <increase> is true,
  // if <dst> != NULL, copy data into it.
  int Read(void* dst, uint32_t count, 
      uint32_t *nread = NULL, bool increase = true);

  //peek some data from messageblock.
  int Peek(void* dst, uint32_t count, uint32_t pos = 0, 
      uint32_t* nread = NULL);

  // Write and advance <count> bytes from <aSrc> to the first <MessageChain>
  int Write(const void* src, uint32_t count, uint32_t *nwritten = NULL);

  // Get the length of the <MessageChain>s, including chained <MessageChain>s.
  uint32_t GetChainedLength() const ;

  // Get the space of the <MessageChain>s, including chained <MessageChain>s.
  uint32_t GetChainedSpace() const ;

  // Append <mb> to the end chain.
  void Append(MessageChain *mb);

  // Get the next <MessageChain>
  inline MessageChain* GetNext() {
    return next_;
  }
  
  inline void NulNext() {
    next_ = nullptr;
  }

  // Advance <count> bytes for reading in chained <MessageChain>s.
  int AdvanceChainedReadPtr(uint32_t count, uint32_t *nread = NULL);

  // Advance <count> bytes for writing in chained <MessageChain>s.
  // <MessageChain> must never be read before, and could write continually.
  int AdvanceChainedWritePtr(uint32_t count, uint32_t *nwritten = NULL);

  /// Return a "shallow" copy that not memcpy actual data buffer.
  /// Use DestroyChained() to delete the return <MessageChain>.
  MessageChain* DuplicateChained();

  // Cutting the chained <MessageChain>s at the start point <start>.
  // Return new <MessageChain> that advanced <start> read bytes from the old.
  // <start> must be less than ChainedLength.
  // Use DestroyChained() to delete the return <MessageChain>.
  MessageChain* Disjoint(uint32_t start);

/*
 * Decrease the shared DataBlock's reference count by 1.  If the
 * DataBlock's reference count goes to 0, it is deleted.
 * In all cases, this MessageChain is deleted - it must have come
 * from the heap, or there will be trouble.
 *
 * DestroyChained() is designed to DestroyChained the continuation chain; the
 * destructor is not.  If we make the destructor DestroyChained the
 * continuation chain by calling DestroyChained() or delete on the message
 * blocks in the continuation chain, the following code will not
 * work since the message block in the continuation chain is not off
 * the heap:
 *
 *  MessageChain mb1(1024);
 *  MessageChain mb2(1024);
 *
 *  mb1.SetNext(&mb2);
 *
 * And hence, call DestroyChained() on a dynamically allocated message
 * block. This will DestroyChained all the message blocks in the
 * continuation chain.  If you call delete or let the message block
 * fall off the stack, cleanup of the message blocks in the
 * continuation chain becomes the responsibility of the user.
 *
 * None <MessageChain> but returned by DuplicateChained() or 
 * Disjoint() could be destroyed by this DestroyChained().
 */
  int DestroyChained();

  // Fill iovec to make socket read/write effectively.
  uint32_t FillIov(iovec* inIov, 
                uint32_t inMax, 
                uint32_t& outFillLength, 
                const MessageChain*& outRemainder) const;

  // For enhanced checking, mainly for debug purpose.
  void LockReading();
  void LockWriting();

  // Destory chained <MessageChain>s whose length is 0.
  // return first Top MessageChain whose length is not 0.
  MessageChain* ReclaimGarbage();

  // Copy all chained data.
  std::string FlattenChained(void);

 public:
  // Get <read_> of the first <MessageChain>
  const char* GetFirstMsgReadPtr() const ;
  // Advance <step> bytes from <read_> of the first <MessageChain>
  int AdvanceFirstMsgReadPtr(uint32_t step);

  // Get <write_> of the top-level <MessageChain>
  char* GetFirstMsgWritePtr() const;
  // Advance <step> bytes from <write_> of the first <MessageChain>
  int AdvanceFirstMsgWritePtr(uint32_t step);

  // Message length is (<write_> - <read_>).
  // Get the length in the first <MessageChain>.
  uint32_t GetFirstMsgLength() const;

  // Get the number of bytes available after the <write_> 
  // in the first <MessageChain>.
  uint32_t GetFirstMsgSpace() const;

  // Rewind <read_> of chained <MessageChain>s to their beginnings,
  // It's not safe because it don't record first read ptr 
  // if it not equals <begin_>.
  void RewindChained(bool bReadWind);
  void SaveChainedReadPtr();

  // for test only
  inline MFlag GetFlag() {
    return flag_;
  }

  inline std::shared_ptr<DataBlock> GetData() {
    return data_block_;
  }
  
  // Return a "shallow" copy of the first <MessageChain>,
  // if flag set DONT_DELETE, malloc and memcpy actual data,
  // else just add DataBlock reference.
  MessageChain* DuplicateFirstMsg() const;
  bool operator ==(const MessageChain& right);
  bool operator !=(const MessageChain& right) {return !(*this==right);}
  
  static std::string GetBlockStatics();

 private:
  explicit MessageChain(std::shared_ptr<DataBlock> aDb, MFlag aFlag);
  void Reset(std::shared_ptr<DataBlock> aDb);
  
 private:
  MessageChain *next_{nullptr};
  std::shared_ptr<DataBlock> data_block_;
  const char* read_{nullptr};
  char* write_{nullptr};
 
  const char* begin_{nullptr};
  const char* end_{nullptr};

  const char* save_read_{nullptr};
  MFlag flag_{0};
};

/*
 * The concept of <DataBlock> is mainly copyed by <ACE_Data_Block>
 * http://www.cs.wustl.edu/~schmidt/ACE.html
 *
 * @brief Stores the data payload that is accessed via one or more
 * <CRtMessageBlock>s.
 *
 * This data structure is reference counted to maximize sharing.
 * memory pool is used to allocate the memory.
 * Only allocate once including <DataBlock> and size of buffer.
 *
 * The internal structure of <DataBlock>:
 *              ------------
 *              | size_    |
 *           -----data_    |
 *           |  |----------|
 *           -->| (buffer) |
 *              |          |
 *              ------------
 */
class DataBlock final {
  friend struct DataBlockDeleter;
 public:
  DataBlock(int32_t aSize, const char* aData);
  
  ~DataBlock() = default;
  DataBlock(const DataBlock&) = delete;
  void operator = (const DataBlock&) = delete;
  DataBlock(DataBlock&&) = delete;
  void operator = (DataBlock&&) = delete;

  static std::shared_ptr<DataBlock> Create(
      int32_t aSize, const char* inData);
  
  inline char* GetBasePtr() const {
    return data_;
  }

  inline int32_t GetLength() const {
    return size_;
  }

  inline int32_t GetCapacity() const {
    return size_;
  }

 private:
  int32_t size_;
  char* data_;
};

class MsgChainAutoDeleter {
 public:
  MsgChainAutoDeleter(MessageChain* mc) : mc_(mc) { }
  ~MsgChainAutoDeleter() {
    if (mc_) {
      mc_->DestroyChained();
    }
  }
 private:
  MessageChain* mc_;
};

}

#endif // !__MEDIA_MESSAGE_CHAIN_H__
