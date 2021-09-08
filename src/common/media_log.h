#ifndef __MEDIA_KLENWOIQN_LOG_H__
#define __MEDIA_KLENWOIQN_LOG_H__

#ifdef __GS__
#include "logwrapper.h"
#else
#include "wa/wa_log.h"
#endif

namespace ma {

#ifdef __GS__

#define MDECLARE_LOGGER() 
#define MDEFINE_LOGGER(a, b) 

#define MLOG_TRACE(msg)   OS_INFO_TRACE(msg)
#define MLOG_DEBUG(msg)   OS_INFO_TRACE(msg)
#define MLOG_INFO(msg)    OS_INFO_TRACE(msg)
#define MLOG_WARN(msg)    OS_WARNING_TRACE(msg)
#define MLOG_ERROR(msg)   OS_ERROR_TRACE(msg)
#define MLOG_FATAL(msg)   do{OS_ERROR_TRACE(msg); assert(false);}while(0);

#define LOG_FMT(fmt) \
  constexpr int LOG_MAX_SIZE = 4096; \
  char log_data[LOG_MAX_SIZE]; \
  int size = 0; \
  va_list ap; \
  va_start(ap, fmt); \
  size += vsnprintf(log_data + size, LOG_MAX_SIZE - size, fmt, ap); \
  va_end(ap);

#define MaTrace(level, fun_name, line_no, fmt) \
  do {\
    LOG_FMT(fmt) \
    OS_LOG(level, OS_LOG_MODULE_TRACE, fun_name << ":" << line_no << ">" << log_data); \
  } while(0);

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

#define MLOG_TRACE(msg)   OLOG_TRACE(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_DEBUG(msg)   OLOG_DEBUG(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_INFO(msg)    OLOG_INFO(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_WARN(msg)    OLOG_WARN(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_ERROR(msg)   OLOG_ERROR(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_FATAL(msg)   do{OLOG_FATAL(__FUNCTION__ << ":" << __LINE__ << ">" << msg); assert(false);}while(0);

#define MLOG_TRACE_THIS(msg)   OLOG_TRACE_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_DEBUG_THIS(msg)   OLOG_DEBUG_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_INFO_THIS(msg)    OLOG_INFO_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_WARN_THIS(msg)    OLOG_WARN_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_ERROR_THIS(msg)   OLOG_ERROR_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_FATAL_THIS(msg)   do{OLOG_FATAL_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg); assert(false);}while(0);

#define MLOG_CTRACE(fmt, ...) \
    ELOG_TRACE("%s:%d>" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define MLOG_CDEBUG(fmt, ...) \
    ELOG_DEBUG("%s:%d>" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define MLOG_CINFO(fmt, ...) \
    ELOG_INFO("%s:%d>" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define MLOG_CWARN(fmt, ...) \
    ELOG_WARN("%s:%d>" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define MLOG_CERROR(fmt, ...) \
    ELOG_ERROR("%s:%d>" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define MLOG_CFATAL(fmt, ...) \
  do { \
    ELOG_FATAL("%s:%d>" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__) \
    assert(false); \
  } while(0);

#define MA_ASSERT LOG_ASSERT
#define MA_ASSERT_RETURN(expr, rv) \
  do { \
    if (!(expr)) { \
      ELOG_ERROR("%s:%d Assert failed: %s", __FILE__, __LINE__, #expr); \
      return rv; \
    } \
 } while (0)

#endif //__GS__

}

#endif //!__MEDIA_KLENWOIQN_LOG_H__

