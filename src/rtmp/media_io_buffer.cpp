#include "rtmp/media_io_buffer.h"

#include "utils/media_msg_chain.h"
#include "connection/h/media_io.h"

namespace ma {

//BufferSender
RtmpBufferIO::RtmpBufferIO(MediaIOPtr io, RtmpBufferIOSink* sink)
  : sink_(sink), io_(std::move(io)) {
  io_->SignalOnRead_.connect(this, &RtmpBufferIO::OnRead);
  io_->SignalOnWrite_.connect(this, &RtmpBufferIO::OnWrite);
  io_->SignalOnDisct_.connect(this, &RtmpBufferIO::OnDisc);
}

RtmpBufferIO::~RtmpBufferIO() {
  sink_ = nullptr;
  io_->Close();
  io_->SignalOnWrite_.disconnect(this);
  io_->SignalOnRead_.disconnect(this);
  io_->SignalOnDisct_.disconnect(this);
  io_ = nullptr;

  if (write_buffer_)
    write_buffer_->DestroyChained();
}

void RtmpBufferIO::SetSink(RtmpBufferIOSink* sink) {
  sink_ = sink;
}

srs_error_t RtmpBufferIO::Write(MessageChain* msg) {
  srs_error_t err = ERROR_SUCCESS;
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

void RtmpBufferIO::OnRead(MessageChain* msg) {
  nrecv_ += msg->GetChainedLength();
  sink_->OnRead(msg);
}

void RtmpBufferIO::OnWrite() {
  if (TrySend() == ERROR_SUCCESS) {
    sink_->OnWrite();
  }
}

void RtmpBufferIO::OnDisc(int reason) {
  io_->Close();
  sink_->OnDisc(reason);
}

srs_error_t RtmpBufferIO::TrySend() {
  assert(write_buffer_);

  int sent = 0;
  srs_error_t ret = io_->Write(write_buffer_, &sent);
  if (ret != ERROR_SUCCESS) {
    write_buffer_->AdvanceChainedReadPtr(sent);
  } else {
    write_buffer_->DestroyChained();
  }

  return ret;
}

}  //namespace ma
