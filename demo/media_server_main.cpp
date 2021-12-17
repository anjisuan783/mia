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

  ma::MediaServerApi::Config _config{
    .workers_= (uint32_t)1,
    .ioworkers_ = (uint32_t)1,
    true,
    false,
    false,                          //flv_record_
    30000,
    ma::JitterAlgorithmZERO,
    {                               //listen_addr_
      {"https://0.0.0.0:443"},
      {"http://0.0.0.0:80"}
    },
    (uint32_t)1,
    {                               //candidates_
      {"192.168.1.156"}             
    },
    {"udp://192.168.1.156:9000"},    //stun_addr_
    {"./mia.key"},
    {"./mia.crt"},
    .request_keyframe_interval = 5  //rtmp request keyframe secode
  };

  char buffer[1024];
  snprintf(buffer, 1024, "mia start \n");
  LOG4CXX_INFO(rootLogger, buffer);

  ma::MediaServerApi* server = ma::MediaServerFactory().Create();
  int ret = server->Init(_config);
  if (ma::kma_ok != ret) {
    snprintf(buffer, 1024, "initialize failed, code:%d \n", ret);
    LOG4CXX_INFO(rootLogger, buffer);
    return ret;
  }

  do {::sleep(1);} while(true);

  snprintf(buffer, 1024, "mia stop \n");
  LOG4CXX_INFO(rootLogger, buffer);

  return 0;
}

