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
  _config.listen_addr_.push_back("rtmp://0.0.0.0:1935");
  
  std::unique_ptr<ma::MediaServerApi> server(ma::MediaServerFactory().Create());
  int ret = server->Init(_config);
  if (ma::kma_ok != ret) {
    ::printf("initialize failed, code:%d \n", ret);

    return ret;
  }

  do {::sleep(1);} while(true);

  return 0;
}

