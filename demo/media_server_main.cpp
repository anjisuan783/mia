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

int usage() {
  printf("usage: ./mia -l[log4cxx.properties]\n");
  return -1;
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    return usage();
  }
  std::string log4cxx_config_path;
  int c;
  while ((c = getopt(argc, argv, "l:")) != -1) {
		switch (c) {
  		case 'l':
        log4cxx_config_path = optarg;
        break;
      default:
        return usage();
		}
	}

  if (log4cxx_config_path.empty()) {
    return usage();
  }

  log4cxx::PropertyConfigurator::configureAndWatch(log4cxx_config_path);  
  log4cxx::LoggerPtr rootLogger = log4cxx::Logger::getRootLogger();
  constexpr int MAX_BUFFER_SIZE = 256;
  char log_buffer[MAX_BUFFER_SIZE];
  snprintf(log_buffer, MAX_BUFFER_SIZE, "init log4cxx with %s", 
           log4cxx_config_path.c_str());
  LOG4CXX_INFO(rootLogger, log_buffer);

  ma::MediaServerApi::Config _config{
    .workers_= (uint32_t)1,
    .ioworkers_ = (uint32_t)1,
    true,
    false,                          //flv_record_
    30000,
    ma::JitterAlgorithmZERO,
    {                               //listen_addr_
      {"https://0.0.0.0:443"},
      {"http://0.0.0.0:80"}
    },
    (uint32_t)1,
    {                               //candidates_
      {"192.168.50.164"}             
    },
    {"udp://192.168.1.156:9000"},    //stun_addr_
    {"./mia.key"},
    {"./mia.crt"},
    5,                              //rtmp request keyframe secode
    {""},
    {"/home/build/mia"}
  };

  snprintf(log_buffer, MAX_BUFFER_SIZE, "mia start \n");
  LOG4CXX_INFO(rootLogger, log_buffer);

  ma::MediaServerApi* server = ma::MediaServerFactory().Create();
  int ret = server->Init(_config);
  if (ma::kma_ok != ret) {
    snprintf(log_buffer, MAX_BUFFER_SIZE, "initialize failed, code:%d \n", ret);
    LOG4CXX_INFO(rootLogger, log_buffer);
    return ret;
  }

  do {::sleep(1);} while(true);

  snprintf(log_buffer, MAX_BUFFER_SIZE, "mia stop \n");
  LOG4CXX_INFO(rootLogger, log_buffer);

  return 0;
}

