/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <algorithm>
#include <cstdarg>
#include <vector>

#include "absl/base/attributes.h"
#include "rtc_base/checks.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/logging.h"
#include "rtc_base/platform_thread_types.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/string_utils.h"
#include "rtc_base/string_builder.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"

namespace rtc {
namespace {
// By default, release builds don't log, debug builds at info level
#if !defined(NDEBUG)
static LoggingSeverity g_min_sev = LS_INFO;
static LoggingSeverity g_dbg_sev = LS_INFO;
#else
static LoggingSeverity g_min_sev = LS_NONE;
static LoggingSeverity g_dbg_sev = LS_NONE;
#endif

// Return the filename portion of the string (that following the last slash).
const char* FilenameFromPath(const char* file) {
  const char* end1 = ::strrchr(file, '/');
  const char* end2 = ::strrchr(file, '\\');
  if (!end1 && !end2)
    return file;
  else
    return (end1 > end2) ? end1 + 1 : end2 + 1;
}

// Global lock for log subsystem, only needed to serialize access to streams_.
ABSL_CONST_INIT GlobalLock g_log_crit;
}  // namespace

// Inefficient default implementation, override is recommended.
void LogSink::OnLogMessage(const std::string& msg,
                           LoggingSeverity severity,
                           const char* tag) {
  OnLogMessage(tag + (": " + msg), severity);
}

void LogSink::OnLogMessage(const std::string& msg,
                           LoggingSeverity /* severity */) {
  OnLogMessage(msg);
}

/////////////////////////////////////////////////////////////////////////////
// LogMessage
/////////////////////////////////////////////////////////////////////////////

bool LogMessage::log_to_stderr_ = true;

// The list of logging streams currently configured.
// Note: we explicitly do not clean this up, because of the uncertain ordering
// of destructors at program exit.  Let the person who sets the stream trigger
// cleanup by setting to null, or let it leak (safe at program exit).
LogMessage::StreamList LogMessage::streams_ RTC_GUARDED_BY(g_log_crit);

// Boolean options default to false (0)
bool LogMessage::thread_, LogMessage::timestamp_;

LogMessage::LogMessage(const char* file, int line, LoggingSeverity sev)
    : LogMessage(file, line, sev, ERRCTX_NONE, 0) {}

LogMessage::LogMessage(const char* file,
                       int line,
                       LoggingSeverity sev,
                       LogErrorContext err_ctx,
                       int err)
    : severity_(sev) {
  if (timestamp_) {
    // Use SystemTimeMillis so that even if tests use fake clocks, the timestamp
    // in log messages represents the real system time.
    int64_t time = TimeDiff(SystemTimeMillis(), LogStartTime());
    // Also ensure WallClockStartTime is initialized, so that it matches
    // LogStartTime.
    WallClockStartTime();
    print_stream_ << "[" << rtc::LeftPad('0', 3, rtc::ToString(time / 1000))
                  << ":" << rtc::LeftPad('0', 3, rtc::ToString(time % 1000))
                  << "] ";
  }

  if (thread_) {
    PlatformThreadId id = CurrentThreadId();
    print_stream_ << "[" << id << "] ";
  }

  if (file != nullptr) {
    print_stream_ << "(" << FilenameFromPath(file) << ":" << line << "): ";
  }

  if (err_ctx != ERRCTX_NONE) {
    char tmp_buf[1024];
    SimpleStringBuilder tmp(tmp_buf);
    tmp.AppendFormat("[0x%08X]", err);
    switch (err_ctx) {
      case ERRCTX_ERRNO:
        tmp << " " << strerror(err);
        break;
      default:
        break;
    }
    extra_ = tmp.str();
  }
}

// DEPRECATED. Currently only used by downstream projects that use
// implementation details of logging.h. Work is ongoing to remove those
// dependencies.
LogMessage::LogMessage(const char* file,
                       int line,
                       LoggingSeverity sev,
                       const std::string& tag)
    : LogMessage(file, line, sev) {
  print_stream_ << tag << ": ";
}

LogMessage::~LogMessage() {
  FinishPrintStream();

  const std::string str = print_stream_.Release();

  if (severity_ >= g_dbg_sev) {
    OutputToDebug(str, severity_);
  }

  GlobalLockScope cs(&g_log_crit);
  for (auto& kv : streams_) {
    if (severity_ >= kv.second) {
      kv.first->OnLogMessage(str, severity_);
    }
  }
}

void LogMessage::AddTag(const char* tag) {
}

rtc::StringBuilder& LogMessage::stream() {
  return print_stream_;
}

int LogMessage::GetMinLogSeverity() {
  return g_min_sev;
}

LoggingSeverity LogMessage::GetLogToDebug() {
  return g_dbg_sev;
}
int64_t LogMessage::LogStartTime() {
  static const int64_t g_start = SystemTimeMillis();
  return g_start;
}

uint32_t LogMessage::WallClockStartTime() {
  static const uint32_t g_start_wallclock = time(nullptr);
  return g_start_wallclock;
}

void LogMessage::LogThreads(bool on) {
  thread_ = on;
}

void LogMessage::LogTimestamps(bool on) {
  timestamp_ = on;
}

void LogMessage::LogToDebug(LoggingSeverity min_sev) {
  g_dbg_sev = min_sev;
  GlobalLockScope cs(&g_log_crit);
  UpdateMinLogSeverity();
}

void LogMessage::SetLogToStderr(bool log_to_stderr) {
  log_to_stderr_ = log_to_stderr;
}

int LogMessage::GetLogToStream(LogSink* stream) {
  GlobalLockScope cs(&g_log_crit);
  LoggingSeverity sev = LS_NONE;
  for (auto& kv : streams_) {
    if (!stream || stream == kv.first) {
      sev = std::min(sev, kv.second);
    }
  }
  return sev;
}

void LogMessage::AddLogToStream(LogSink* stream, LoggingSeverity min_sev) {
  GlobalLockScope cs(&g_log_crit);
  streams_.push_back(std::make_pair(stream, min_sev));
  UpdateMinLogSeverity();
}

void LogMessage::RemoveLogToStream(LogSink* stream) {
  GlobalLockScope cs(&g_log_crit);
  for (StreamList::iterator it = streams_.begin(); it != streams_.end(); ++it) {
    if (stream == it->first) {
      streams_.erase(it);
      break;
    }
  }
  UpdateMinLogSeverity();
}

void LogMessage::ConfigureLogging(const char* params) {
  LoggingSeverity current_level = LS_VERBOSE;
  LoggingSeverity debug_level = GetLogToDebug();

  std::vector<std::string> tokens;
  tokenize(params, ' ', &tokens);

  for (const std::string& token : tokens) {
    if (token.empty())
      continue;

    // Logging features
    if (token == "tstamp") {
      LogTimestamps();
    } else if (token == "thread") {
      LogThreads();

      // Logging levels
    } else if (token == "verbose") {
      current_level = LS_VERBOSE;
    } else if (token == "info") {
      current_level = LS_INFO;
    } else if (token == "warning") {
      current_level = LS_WARNING;
    } else if (token == "error") {
      current_level = LS_ERROR;
    } else if (token == "none") {
      current_level = LS_NONE;

      // Logging targets
    } else if (token == "debug") {
      debug_level = current_level;
    }
  }

  LogToDebug(debug_level);
}

void LogMessage::UpdateMinLogSeverity()
    RTC_EXCLUSIVE_LOCKS_REQUIRED(g_log_crit) {
  LoggingSeverity min_sev = g_dbg_sev;
  for (const auto& kv : streams_) {
    const LoggingSeverity sev = kv.second;
    min_sev = std::min(min_sev, sev);
  }
  g_min_sev = min_sev;
}

void LogMessage::OutputToDebug(const std::string& str,
                               LoggingSeverity severity) {
  bool log_to_stderr = log_to_stderr_;

  if (log_to_stderr) {
    fprintf(stderr, "%s", str.c_str());
    fflush(stderr);
  }
}

// static
bool LogMessage::IsNoop(LoggingSeverity severity) {
  if (severity >= g_dbg_sev || severity >= g_min_sev)
    return false;

  // TODO(tommi): We're grabbing this lock for every LogMessage instance that
  // is going to be logged. This introduces unnecessary synchronization for
  // a feature that's mostly used for testing.
  GlobalLockScope cs(&g_log_crit);
  if (streams_.size() > 0)
    return false;

  return true;
}

void LogMessage::FinishPrintStream() {
  if (!extra_.empty())
    print_stream_ << " : " << extra_;
  print_stream_ << "\n";
}

namespace webrtc_logging_impl {

void Log(const LogArgType* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  LogMetadataErr meta;
  const char* tag = nullptr;
  switch (*fmt) {
    case LogArgType::kLogMetadata: {
      meta = {va_arg(args, LogMetadata), ERRCTX_NONE, 0};
      break;
    }
    case LogArgType::kLogMetadataErr: {
      meta = va_arg(args, LogMetadataErr);
      break;
    }
    default: {
      RTC_NOTREACHED();
      va_end(args);
      return;
    }
  }

  if (LogMessage::IsNoop(meta.meta.Severity())) {
    va_end(args);
    return;
  }

  LogMessage log_message(meta.meta.File(), meta.meta.Line(),
                         meta.meta.Severity(), meta.err_ctx, meta.err);
  if (tag) {
    log_message.AddTag(tag);
  }

  for (++fmt; *fmt != LogArgType::kEnd; ++fmt) {
    switch (*fmt) {
      case LogArgType::kInt:
        log_message.stream() << va_arg(args, int);
        break;
      case LogArgType::kLong:
        log_message.stream() << va_arg(args, long);
        break;
      case LogArgType::kLongLong:
        log_message.stream() << va_arg(args, long long);
        break;
      case LogArgType::kUInt:
        log_message.stream() << va_arg(args, unsigned);
        break;
      case LogArgType::kULong:
        log_message.stream() << va_arg(args, unsigned long);
        break;
      case LogArgType::kULongLong:
        log_message.stream() << va_arg(args, unsigned long long);
        break;
      case LogArgType::kDouble:
        log_message.stream() << va_arg(args, double);
        break;
      case LogArgType::kLongDouble:
        log_message.stream() << va_arg(args, long double);
        break;
      case LogArgType::kCharP: {
        const char* s = va_arg(args, const char*);
        log_message.stream() << (s ? s : "(null)");
        break;
      }
      case LogArgType::kStdString:
        log_message.stream() << *va_arg(args, const std::string*);
        break;
      case LogArgType::kStringView:
        log_message.stream() << *va_arg(args, const std::string_view*);
        break;
      case LogArgType::kVoidP:
        log_message.stream() << rtc::ToHex(
            reinterpret_cast<uintptr_t>(va_arg(args, const void*)));
        break;
      default:
        RTC_NOTREACHED();
        va_end(args);
        return;
    }
  }

  va_end(args);
}

}  // namespace webrtc_logging_impl
}  // namespace rtc
