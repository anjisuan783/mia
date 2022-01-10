#include <iostream>
#include <memory>

#include <log4cxx/logger.h>
#include "log4cxx/propertyconfigurator.h"
#include <log4cxx/helpers/exception.h>

#include "../h/rtc_stack_api.h"

using namespace wa;

int main(int argc, char* argv[])
{
  log4cxx::PropertyConfigurator::configureAndWatch("log4cxx.properties");  
  log4cxx::LoggerPtr rootLogger = log4cxx::Logger::getRootLogger();  
  LOG4CXX_INFO(rootLogger, "init log4cxx with log4cxx.properties");

  std::unique_ptr<rtc_api> p(create_agent());
  
  std::vector<std::string> ips{"127.0.0.1", "192.168.1.156"};
  int ret = p->initiate(1, ips);
  
  if(ret != 0){
    char buf[1024];
    snprintf(buf, 1024, "init rtc_api failed, code:%d", ret);
    LOG4CXX_INFO(rootLogger, buf);
  }
  
  return 0;
}

