//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MIA_CONFIG_H__
#define __MIA_CONFIG_H__

#include <log4cxx/logger.h>
#include "log4cxx/propertyconfigurator.h"
#include <log4cxx/helpers/exception.h>

#include "h/media_server_api.h"

#define MIA_LOG(fmt, ...) \
  if (true) { \
    constexpr int MAX_BUFFER_SIZE = 256; \
    char log_buffer[MAX_BUFFER_SIZE]; \
    snprintf(log_buffer, MAX_BUFFER_SIZE, fmt, ##__VA_ARGS__); \
    log4cxx::LoggerPtr rootLogger = log4cxx::Logger::getRootLogger(); \
    LOG4CXX_INFO(rootLogger, log_buffer); \
  }

int config(ma::MediaServerApi::Config& _config, 
    std::string log4cxx_config_path, std::string mia_config_path);

#endif //!__MIA_CONFIG_H__
