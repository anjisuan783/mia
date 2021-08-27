#ifndef __MEDIA_KLENWOIQN_LOG_H__
#define __MEDIA_KLENWOIQN_LOG_H__

#ifdef __GS__
#include "logwrapper.h"
#else
#include "wa/wa_log.h"
#endif

namespace ma {

#ifdef __GS__

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

#define MDECLARE_LOGGER() 
#define MDEFINE_LOGGER(a, b) 

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

#define MA_ASSERT OS_ASSERTE
#define MA_ASSERT_RETURN OS_ASSERTE_RETURN

#else

#define MDECLARE_LOGGER DECLARE_LOGGER
#define MDEFINE_LOGGER(namespace, logName)  DEFINE_LOGGER(namespace, logName)

#define MLOG_TRACE(msg)   OLOG_TRACE(msg)
#define MLOG_DEBUG(msg)   OLOG_DEBUG(msg)
#define MLOG_INFO(msg)    OLOG_INFO(msg)
#define MLOG_WARN(msg)    OLOG_WARN(msg)
#define MLOG_ERROR(msg)   OLOG_ERROR(msg)
#define MLOG_FATAL(msg)   do{OLOG_FATAL(msg); assert(false);}while(0);

#define MLOG_TRACE_THIS(msg)   OLOG_TRACE_THIS(msg)
#define MLOG_DEBUG_THIS(msg)   OLOG_DEBUG_THIS(msg)
#define MLOG_INFO_THIS(msg)    OLOG_INFO_THIS(msg)
#define MLOG_WARN_THIS(msg)    OLOG_WARN_THIS(msg)
#define MLOG_ERROR_THIS(msg)   OLOG_ERROR_THIS(msg)
#define MLOG_FATAL_THIS(msg)   do{OLOG_FATAL_THIS(msg); assert(false);}while(0);


#define MLOG_CTRACE   ELOG_TRACE
#define MLOG_CDEBUG   ELOG_DEBUG
#define MLOG_CINFO    ELOG_INFO
#define MLOG_CWARN    ELOG_WARN
#define MLOG_CERROR   ELOG_ERROR
#define MLOG_CFATAL   ELOG_FATAL

#define MA_ASSERT LOG_ASSERT
#define MA_ASSERT_RETURN(expr, rv) \
  do { \
    if (!(expr)) { \
      ELOG_ERROR("%s:%s Assert failed: %s", __FILE__, __LINE__, #expr); \
      return rv; \
    } \
 } while (0)

#endif //__GS__

}

#endif //!__MEDIA_KLENWOIQN_LOG_H__

