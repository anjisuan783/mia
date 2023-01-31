#ifndef __MEDIA_CHECKER_H__
#define __MEDIA_CHECKER_H__

#include <pthread.h>
#include <mutex>
#include "utils/media_thread.h"

namespace ma {

#define MEDIA_DCHECK_IS_ON 1

class SequenceCheckerDoNothing {
 public:
  bool IsCurrent() const { return true; }
  void Detach() {}
};

class SequenceCheckerScope {
 public:
  template <typename ThreadLikeObject>
  explicit SequenceCheckerScope(const ThreadLikeObject* thread_like_object) {}
  SequenceCheckerScope(const SequenceCheckerScope&) = delete;
  SequenceCheckerScope& operator=(const SequenceCheckerScope&) = delete;
  ~SequenceCheckerScope() {}

  template <typename ThreadLikeObject>
  static bool IsCurrent(const ThreadLikeObject* thread_like_object) {
    return thread_like_object->IsCurrent();
  }
};

class SequenceCheckerImpl {
 public:
  SequenceCheckerImpl();
  ~SequenceCheckerImpl();

  bool IsCurrent();
  void Detach();

 private:
  std::mutex lock_;
  mutable bool attached_;
  mutable MediaThread* valid_thread_;
};

#if MEDIA_DCHECK_IS_ON
class SequenceChecker : public SequenceCheckerImpl {};
#else
class SequenceChecker : public SequenceCheckerDoNothing {};
#endif  // MEDIA_DCHECK_IS_ON

#define MEDIA_DCHECK(condition)                                           \
  do {                                                                    \
    if (MEDIA_DCHECK_IS_ON && !(condition)) {                             \
      MA_ASSERT(#condition);                                              \
    }                                                                     \
  } while (0)

#define MEDIA_DCHECK_RUN_ON(x)                                            \
  SequenceCheckerScope seq_check_scope(x); \
  MEDIA_DCHECK((x)->IsCurrent())

} // namespace ma

#endif //!__MEDIA_CHECKER_H__
