#include "utils/media_checker.h"

namespace ma {

SequenceCheckerImpl::SequenceCheckerImpl()
    : attached_(true),
      valid_thread_(MediaThreadManager::Instance()->CurrentThread()) {}

SequenceCheckerImpl::~SequenceCheckerImpl() = default;

bool SequenceCheckerImpl::IsCurrent() {
  MediaThread* current_thread = MediaThreadManager::Instance()->CurrentThread();
  std::lock_guard<std::mutex> guard(lock_);
  if (!attached_) {
    attached_ = true;
    valid_thread_ = current_thread;
    return true;
  }
  return MediaThreadManager::IsEqualCurrentThread(valid_thread_);
}

void SequenceCheckerImpl::Detach() {
  std::lock_guard<std::mutex> guard(lock_);
  attached_ = false;
}

} //namespace ma
