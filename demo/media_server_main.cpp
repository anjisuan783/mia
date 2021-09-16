//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include <stdio.h>
#include <unistd.h>

#include <memory>

#include <log4cxx/logger.h>
#include "log4cxx/propertyconfigurator.h"
#include <log4cxx/helpers/exception.h>
#include "h/media_return_code.h"
#include "h/media_server_api.h"

int main(int argc, char* argv[]) {

  log4cxx::PropertyConfigurator::configureAndWatch("log4cxx.properties");  
  log4cxx::LoggerPtr rootLogger = log4cxx::Logger::getRootLogger();  
  LOG4CXX_INFO(rootLogger, "init log4cxx with log4cxx.properties");

  ma::MediaServerApi::config _config;
  _config.listen_addr_.push_back("rtmp://0.0.0.0:1936");
  _config.listen_addr_.push_back("http://0.0.0.0:8080");
  _config.rtc_addr_.push_back("192.168.1.156");
  _config.rtc_stun_addr_ = "udp://192.168.1.156:9000";
  
  ma::MediaServerApi* server = ma::MediaServerFactory().Create();
  int ret = server->Init(_config);
  if (ma::kma_ok != ret) {
    char buffer[1024]; \
    snprintf(buffer, 1024, "initialize failed, code:%d \n", ret);
    LOG4CXX_INFO(rootLogger, buffer);

    return ret;
  }

  do {::sleep(1);} while(true);

  return 0;
}

