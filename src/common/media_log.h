#ifndef __MEDIA_KLENWOIQN_LOG_H__
#define __MEDIA_KLENWOIQN_LOG_H__

#include "wa/wa_log.h"

namespace ma {

#define MDECLARE_LOGGER DECLARE_LOGGER
#define MDEFINE_LOGGER(namespace, logName)  DEFINE_LOGGER(namespace, logName)

#define MLOG_TRACE(msg)   OLOG_TRACE(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_DEBUG(msg)   OLOG_DEBUG(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_INFO(msg)    OLOG_INFO(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_WARN(msg)    OLOG_WARN(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_ERROR(msg)   OLOG_ERROR(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_FATAL(msg)   do{OLOG_FATAL(__FUNCTION__ << ":" << __LINE__ << ">" << msg); abort();}while(0);

#define MLOG_TRACE_THIS(msg)   OLOG_TRACE_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_DEBUG_THIS(msg)   OLOG_DEBUG_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_INFO_THIS(msg)    OLOG_INFO_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_WARN_THIS(msg)    OLOG_WARN_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_ERROR_THIS(msg)   OLOG_ERROR_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg)
#define MLOG_FATAL_THIS(msg)   do{OLOG_FATAL_THIS(__FUNCTION__ << ":" << __LINE__ << ">" << msg); abort();}while(0);

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
 
} //namespace ma

#endif //!__MEDIA_KLENWOIQN_LOG_H__

