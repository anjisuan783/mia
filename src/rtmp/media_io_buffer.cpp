#include "rtmp/media_io_buffer.h"

#include <iostream>
#include "utils/media_msg_chain.h"
#include "connection/h/media_io.h"

namespace ma {

//BufferSender
RtmpBufferIO::RtmpBufferIO(MediaIOPtr io, RtmpBufferIOSink* sink)
  : sink_(sink), io_(std::move(io)) {
  io_->Open(this);
  max_buffer_length_ = 8 * 1024;
}

RtmpBufferIO::~RtmpBufferIO() {
  sink_ = nullptr;
  io_->Close();
  
  if (write_buffer_)
    write_buffer_->DestroyChained();
}

void RtmpBufferIO::SetSink(RtmpBufferIOSink* sink) {
  sink_ = sink;
}

void RtmpBufferIO::SetMaxBuffer(int64_t s) {
  max_buffer_length_ = s;
}

srs_error_t RtmpBufferIO::Write(MessageChain* msg, bool force) {
  srs_error_t err = srs_success;
  if (!msg) 
    return err;

  uint32_t msg_length = msg->GetChainedLength();

  if (nbufferd_ >= max_buffer_length_) {
    if (force) {
      write_buffer_->Append(msg->DuplicateChained());
      nbufferd_ += msg_length;
      return err;
    }
    return srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "buffer too much %d", nbufferd_);
  }

  // need OnWrite
  if (write_buffer_) {
    write_buffer_->Append(msg->DuplicateChained());
    nbufferd_ += msg_length;
    return err;
  }

  int sent = 0;
  err = io_->Write(msg, &sent);
  if (err != srs_success) {
    write_buffer_ = msg->Disjoint(sent);
    nbufferd_ += msg_length - sent;
  }

  return err;
}

srs_error_t RtmpBufferIO::OnRead(MessageChain* msg) {
  nrecv_ += msg->GetChainedLength();
  return sink_->OnRead(msg);
}

srs_error_t RtmpBufferIO::OnWrite() {
  srs_error_t err = srs_success;
  if ((err = TrySend()) == srs_success) {
  }

  return err;
}

void RtmpBufferIO::OnClose(srs_error_t reason) {
  io_->Close();
  SignalOnclose_(reason);
}

srs_error_t RtmpBufferIO::TrySend() {
  if (!write_buffer_) return srs_success; 

  int sent = 0;
  srs_error_t err = io_->Write(write_buffer_, &sent);
  if (err != srs_success) {
    write_buffer_->AdvanceChainedReadPtr(sent);
  } else {
    sent = write_buffer_->GetChainedLength();
    write_buffer_->DestroyChained();
    write_buffer_ = nullptr;
  }

  nbufferd_ -= sent;

  if (!write_buffer_) {
    assert(nbufferd_ == 0);
  }

  return err;
}

}  //namespace ma
