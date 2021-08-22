#ifndef __MEDIA_KLENWOIQN_LOG_H__
#define __MEDIA_KLENWOIQN_LOG_H__

#include "logwrapper.h"

namespace ma {

inline void MaTrace(int level, const char* fun_name, int line_no, const char* fmt, ...) {
  constexpr int LOG_MAX_SIZE = 4096;
  char log_data[LOG_MAX_SIZE];
  int size = 0;
  
  va_list ap;
  va_start(ap, fmt);
  // we reserved 1 bytes for the new line.
  size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap);
  va_end(ap);

  OS_LOG(level, OS_LOG_MODULE_TRACE, "(" << fun_name << ":" << line_no << "):" << log_data);
}

#define MLOG_TRACE(msg)   OS_INFO_TRACE(msg)
#define MLOG_DEBUG(msg)   OS_INFO_TRACE(msg)
#define MLOG_INFO(msg)    OS_INFO_TRACE(msg)
#define MLOG_WARN(msg)    OS_WARNING_TRACE(msg)
#define MLOG_ERROR(msg)   OS_ERROR_TRACE(msg)
#define MLOG_FATAL(msg)   do{OS_ERROR_TRACE(msg); assert(false);}while(0);


#define MLOG_CTRACE(msg, ...) \
    MaTrace(OS_LOG_LEVEL_INFO, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__)
#define MLOG_CDEBUG(msg, ...) \
    MaTrace(OS_LOG_LEVEL_INFO, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__)
#define MLOG_CINFO(msg, ...) \
    MaTrace(OS_LOG_LEVEL_INFO, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__)
#define MLOG_CWARN(msg, ...) \
    MaTrace(OS_LOG_LEVEL_WARNING, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__)
#define MLOG_CERROR(msg, ...) \
    MaTrace(OS_LOG_LEVEL_ERROR, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__)
#define MLOG_CFATAL(msg, ...) \
    do{ \
      MaTrace(OS_LOG_LEVEL_ERROR, __FUNCTION__, __LINE__, msg, ##__VA_ARGS__); \
      assert(false); \
    } \
    while(0);
}

#define MA_ASSERT OS_ASSERTE
#define MA_ASSERT_RETURN OS_ASSERTE_RETURN

#endif //!__MEDIA_KLENWOIQN_LOG_H__
