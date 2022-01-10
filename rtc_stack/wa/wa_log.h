/**
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*     http:// www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifndef __WA_LOG_H__
#define __WA_LOG_H__

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <map>
#include <string>
#include <utility>
#include <type_traits>
#include <ostream>
#include <cstring>

#include <log4cxx/logger.h>
#include <log4cxx/helpers/exception.h>

#ifdef WIN32
  #define __PRETTY_FUNCTION__ __FUNCTION__
  #define __METHOD_NAME__    __FUNCTION__
#else
#if defined(ANDROID)  || defined(DARWIN)
  #define __METHOD_NAME__    __PRETTY_FUNCTION__
#else
  inline std::string methodName(const std::string& prettyFunction)
  {
    size_t end = prettyFunction.find('(');
    if(end == prettyFunction.npos)
      return prettyFunction;
    
    size_t begin = prettyFunction.rfind(' ',end);
    if(begin == prettyFunction.npos)
      return prettyFunction.substr(0,end);

    begin++;
    return prettyFunction.substr(begin,end - begin);
  }

  #define __METHOD_NAME__ methodName(__PRETTY_FUNCTION__)
#endif

#endif


class LogContext {
 public:
  LogContext() : context_log_{""} {
  }

  virtual ~LogContext() = default;

  void setLogContext(const std::map<std::string, std::string>& context) {
    context_ = context;
    context_log_ = "";
    for (const auto &item : context) {
      context_log_ += item.first + ": " + item.second + ", ";
    }
  }

  void copyLogContextFrom(const LogContext& log_context) { setLogContext(log_context.context_); }

  const std::string& printLogContext() const { return context_log_; }

 private:
  std::string context_log_;
  std::map<std::string, std::string> context_;
};

#define DECLARE_LOGGER() \
  static log4cxx::LoggerPtr logger;

#define DEFINE_LOGGER(namespace, logName) \
  log4cxx::LoggerPtr namespace::logger = log4cxx::Logger::getLogger(logName);

#define ELOG_MAX_BUFFER_SIZE 10240

#define SPRINTF_ELOG_MSG(buffer, fmt, args...) \
char buffer[ELOG_MAX_BUFFER_SIZE]; \
snprintf(buffer, ELOG_MAX_BUFFER_SIZE, fmt, ##args);

#define ELOG_TRACE2(logger, fmt, args...) \
SPRINTF_ELOG_MSG(__tmp, fmt, ##args); \
LOG4CXX_TRACE(logger, __tmp);

#define ELOG_DEBUG2(logger, fmt, args...) \
SPRINTF_ELOG_MSG(__tmp, fmt, ##args); \
LOG4CXX_DEBUG(logger, __tmp);

#define ELOG_INFO2(logger, fmt, args...) \
SPRINTF_ELOG_MSG(__tmp, fmt, ##args); \
LOG4CXX_INFO(logger, __tmp);

#define ELOG_WARN2(logger, fmt, args...) \
SPRINTF_ELOG_MSG(__tmp, fmt, ##args); \
LOG4CXX_WARN(logger, __tmp);

#define ELOG_ERROR2(logger, fmt, args...) \
SPRINTF_ELOG_MSG(__tmp, fmt, ##args); \
LOG4CXX_ERROR(logger, __tmp);

#define ELOG_FATAL2(logger, fmt, args...) \
SPRINTF_ELOG_MSG(__tmp, fmt, ##args); \
LOG4CXX_FATAL(logger, __tmp);

namespace detail {
// Helper for forwarding correctly the object to be logged
template <typename T>
struct LogElementForwarder {
  T operator()(T t) {
    return t;
  }
};

template <>
struct LogElementForwarder<std::string> {
  template <typename S>
  const char* operator()(const S& t) {
    return t.c_str();
  }
};
}  // namespace detail

#define DEFINE_ELOG_T(name, invoke) \
template <typename Logger, typename... Args> \
inline void name(const Logger&, const char*, Args...) __attribute__((always_inline)); \
\
template <typename Logger, typename... Args> \
void name(const Logger& logger, const char* fmt, Args... args) { \
  invoke(logger, fmt, detail::LogElementForwarder<typename std::decay<Args>::type>{}(args)...); \
} \
\
template <typename Logger> \
inline void name(const Logger&, const char*) __attribute__((always_inline)); \
\
template <typename Logger> \
void name(const Logger& logger, const char* fmt) { \
  invoke(logger, "%s", fmt); \
}

DEFINE_ELOG_T(ELOG_TRACET, ELOG_TRACE2)
DEFINE_ELOG_T(ELOG_DEBUGT, ELOG_DEBUG2)
DEFINE_ELOG_T(ELOG_INFOT, ELOG_INFO2)
DEFINE_ELOG_T(ELOG_WARNT, ELOG_WARN2)
DEFINE_ELOG_T(ELOG_ERRORT, ELOG_ERROR2)
DEFINE_ELOG_T(ELOG_FATALT, ELOG_FATAL2)

// older versions of log4cxx don't support tracing
#ifdef LOG4CXX_TRACE
#define ELOG_TRACE(fmt, args...) \
  if (logger->isTraceEnabled()) { \
    ELOG_TRACET(logger, fmt, ##args); \
  }
#else
#define ELOG_TRACE(fmt, args...) \
  if (logger->isDebugEnabled()) { \
    ELOG_DEBUGT(logger, fmt, ##args); \
  }
#endif

#define ELOG_DEBUG(fmt, args...) \
  if (logger->isDebugEnabled()) { \
    ELOG_DEBUGT(logger, fmt, ##args); \
  }

#define ELOG_INFO(fmt, args...) \
  if (logger->isInfoEnabled()) { \
    ELOG_INFOT(logger, fmt, ##args); \
  }

#define ELOG_WARN(fmt, args...) \
  if (logger->isWarnEnabled()) { \
    ELOG_WARNT(logger, fmt, ##args); \
  }

#define ELOG_ERROR(fmt, args...) \
  if (logger->isErrorEnabled()) { \
    ELOG_ERRORT(logger, fmt, ##args); \
  }

#define ELOG_FATAL(fmt, args...) \
  if (logger->isFatalEnabled()) { \
    ELOG_FATALT(logger, fmt, ##args); \
  }

class CRecorder final
{
  typedef enum {
    hex     = 0,
    decimal = 1
  }Ordix;
  
public :
  CRecorder(char* inBuf, unsigned long inBufSize)
    : m_buf(inBuf),
      m_size(inBufSize) {
    reset();
  }
  ~CRecorder() = default;
  CRecorder& operator << (char ch) { return *this << (int)ch; }
  CRecorder& operator << (unsigned char ch) { return *this << (int)ch; }
  CRecorder& operator << (short s) { return *this << (int)s; }
  CRecorder& operator << (unsigned short s) { return *this << (int)s; }
  CRecorder& operator << (const char* inString) { Advance(inString); return *this; }
  CRecorder& operator << (const std::string& str) { return (*this << str.c_str()); }
#ifdef WIN32
  CRecorder& operator <<(const WCHAR* wstr) {
    if(!wstr)
      return * this;
    int nLen = WideCharToMultiByte(CP_UTF8,0, wstr, wcslen(wstr), NULL, 0,0,0);
    if(nLen > 0)
    {
      std::string str;
      str.resize(nLen);
      WideCharToMultiByte(CP_UTF8,0, wstr, wcslen(wstr), (LPSTR)str.c_str(), nLen,0,0);
      return (*this<<str);
    }
    return *this;
  }
#endif
  CRecorder& operator << (const void* lpv) {
    std::ostringstream oss;
    oss << lpv;
    *this << "0x" << hex << oss.str();
    return *this;
  }

  template <class T> 
  CRecorder& operator << (T i) {
    std::ostringstream oss;
    oss << i;
    if (GetHexFlag()) {
      oss << std::hex;
    }
    Advance(oss.str().c_str());
    SetHexFlag(0);
    return *this;
  }
  
  CRecorder& operator << (Ordix ordix) {
    switch(ordix){
    case hex :
      SetHexFlag(1);
      break;

    case decimal :
      SetHexFlag(0);
      break;
    default :
      ;
    }
    return *this;
  }
  
  operator char*() const { return m_buf; }

private :
  void reset() {
    m_pos = 0;
    m_hex = 0;
    std::memset(m_buf, 0, m_size);
  }
  
  void Advance(const char* inString) {
    if (inString) {
      unsigned long nLength = (unsigned long)strlen(inString);
      if(nLength > m_size - m_pos - 64)
          nLength = m_size - m_pos - 64;
  
      if (nLength > 0) {
        memcpy(m_buf + m_pos*sizeof(char), inString, nLength*sizeof(char));
        m_pos += nLength;
      }
    }
  }
  
  void SetHexFlag(unsigned char inValue) { m_hex = inValue; }
  
  unsigned char GetHexFlag() { return m_hex; }

private :
  char*  m_buf;
  unsigned long  m_size;
  unsigned long  m_pos;
  unsigned char m_hex;
};

#define OLOG(logger, str) \
  do { \
      char formatBuf[ELOG_MAX_BUFFER_SIZE]; \
      CRecorder formator(formatBuf, ELOG_MAX_BUFFER_SIZE); \
      logger(formator << str); \
  } while (0)

/*
#define OLOG1_WARN(str) \
  do { \
      char formatBuf[WA_LOG_MAX_LOG_LENGTH]; \
      CRecorder formator(formatBuf, WA_LOG_MAX_LOG_LENGTH); \
      ELOG_WARN(formator << str); \
  } while (0)
*/

#define OLOG_TRACE(msg) \
    if (logger->isTraceEnabled()) { \
      OLOG(ELOG_TRACE, msg); \
    }

#define OLOG_DEBUG(msg) \
    if (logger->isDebugEnabled()) { \
      OLOG(ELOG_DEBUG, msg); \
    }

#define OLOG_INFO(msg) \
    if (logger->isInfoEnabled()) { \
      OLOG(ELOG_INFO, msg); \
    }

#define OLOG_WARN(msg) \
    if (logger->isWarnEnabled()) { \
      OLOG(ELOG_WARN, msg); \
    }
/*
#define OLOG_WARN(msg) \
    if (logger->isWarnEnabled()) { \
      OLOG1_WARN(msg); \
    }
*/

#define OLOG_ERROR(msg) \
    if (logger->isErrorEnabled()) { \
      OLOG(ELOG_ERROR, msg); \
    }  

#define OLOG_FATAL(msg) \
    if (logger->isFatalEnabled()) { \
      OLOG(ELOG_FATAL, msg); \
    }  


#define OLOG_TRACE_THIS(msg) \
    if (logger->isTraceEnabled()) { \
      OLOG(ELOG_TRACE, "["<<__FUNCTION__<<":"<<__LINE__<<"]("<<this<<")" << msg); \
    }

#define OLOG_DEBUG_THIS(msg) \
    if (logger->isDebugEnabled()) { \
      OLOG(ELOG_DEBUG, "("<<this<<")" << msg); \
    }

#define OLOG_INFO_THIS(msg) \
    if (logger->isInfoEnabled()) { \
      OLOG(ELOG_INFO, "("<<this<<")" << msg); \
    }
    
#define OLOG_WARN_THIS(msg) \
    if (logger->isWarnEnabled()) { \
      OLOG(ELOG_WARN, "("<<this<<")" << msg); \
    }

#define OLOG_ERROR_THIS(msg) \
    if (logger->isErrorEnabled()) { \
      OLOG(ELOG_ERROR, "("<<this<<")" << msg); \
    }  

#define OLOG_FATAL_THIS(msg) \
    if (logger->isFatalEnabled()) { \
      OLOG(ELOG_FATAL, "("<<this<<")" << msg); \
    }  

#define WLOG_TRACE   ELOG_TRACE
#define WLOG_DEBUG   ELOG_DEBUG
#define WLOG_INFO    ELOG_INFO
#define WLOG_WARNING ELOG_WARN
#define WLOG_ERROR   ELOG_ERROR
#define WLOG_FATAL   ELOG_FATAL

#define LOG_ASSERT(expr) \
    do { \
      if (!(expr)) { \
        ELOG_ERROR("%s:%d Assert failed: %s", __FILE__, __LINE__, #expr); \
        assert(false); \
      } \
    } while(0)

#endif  // WA_SRC_LOGGER_H_

