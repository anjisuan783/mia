#include "rtmp/media_io_buffer.h"

#include <iostream>
#include "utils/media_msg_chain.h"
#include "connection/h/media_io.h"

namespace ma {

//BufferSender
RtmpBufferIO::RtmpBufferIO(MediaIOPtr io, RtmpBufferIOSink* sink)
  : sink_(sink), io_(std::move(io)) {
  io_->Open(this);
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

srs_error_t RtmpBufferIO::Write(MessageChain* msg) {
  srs_error_t err = srs_success;
  if (!msg) 
    return err;

  MessageChain* pDup = msg->DuplicateChained();

  // need OnWrite
  if (write_buffer_) {
    write_buffer_->Append(pDup);
    return srs_error_wrap(err, "would block");
  }

  write_buffer_ = pDup;

  return TrySend();
}

srs_error_t RtmpBufferIO::OnRead(MessageChain* msg) {
  nrecv_ += msg->GetChainedLength();
  return sink_->OnRead(msg);
}

srs_error_t RtmpBufferIO::OnWrite() {
  srs_error_t err = srs_success;
  if ((err = TrySend()) == srs_success) {
    err = sink_->OnWrite();
  }

  return err;
}

void RtmpBufferIO::OnDisconnect(srs_error_t reason) {
  io_->Close();
  sink_->OnDisc(reason);
}

srs_error_t RtmpBufferIO::TrySend() {
  if (!write_buffer_) return srs_success; 

  int sent = 0;
  srs_error_t ret = io_->Write(write_buffer_, &sent);
  if (ret != srs_success) {
    write_buffer_->AdvanceChainedReadPtr(sent);
  } else {
    write_buffer_->DestroyChained();
    write_buffer_ = nullptr;
  }

  return ret;
}

}  //namespace ma
