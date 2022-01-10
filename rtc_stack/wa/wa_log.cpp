#include "wa_log.h"
#include "event.h"

namespace wa {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("libevent");

void lib_evnet_log(int severity, const char *msg) {
  const char *severity_str;
	switch (severity) {
	case _EVENT_LOG_DEBUG:
		severity_str = "debug";
		break;
	case _EVENT_LOG_MSG:
		severity_str = "msg";
		break;
	case _EVENT_LOG_WARN:
		severity_str = "warn";
		break;
	case _EVENT_LOG_ERR:
		severity_str = "err";
		break;
	default:
		severity_str = "???";
		break;
	}
  ELOG_TRACE("%s:%s", severity_str, msg);
}

}

