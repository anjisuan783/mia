//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include <stdio.h>
#include <unistd.h>
#include <iostream>

#include <memory>
#include <assert.h>

#include <log4cxx/logger.h>
#include "log4cxx/propertyconfigurator.h"
#include <log4cxx/helpers/exception.h>
#include <libconfig.h>

#include "h/media_return_code.h"
#include "h/media_server_api.h"

int usage() {
  printf("usage: ./mia -l [log4cxx.properties] -c [mia.cfg]\n");
  return -1;
}

static constexpr int MAX_BUFFER_SIZE = 256;

#define MIA_LOG(fmt, ...) \
  if (true) { \
    char log_buffer[MAX_BUFFER_SIZE]; \
    snprintf(log_buffer, MAX_BUFFER_SIZE, fmt, ##__VA_ARGS__); \
    LOG4CXX_INFO(rootLogger, log_buffer); \
  }

int main(int argc, char* argv[]) {
  if (argc < 5) {
    return usage();
  }
  
  std::string log4cxx_config_path;
  std::string mia_config_path;
  int c;
  while ((c = getopt(argc, argv, "l:c:")) != -1) {
		switch (c) {
  		case 'l':
        log4cxx_config_path = optarg;
        break;
      case 'c':
        mia_config_path = optarg;
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
  
  MIA_LOG("init log4cxx with %s", log4cxx_config_path.c_str());

  //load configure
  config_t cfg;
  config_setting_t *setting;

  config_init(&cfg);

  /* Read the file. If there is an error, report it and exit. */
  if(! config_read_file(&cfg, mia_config_path.c_str())) {
    fprintf(stderr, "read mia config %s:%d - %s\n", config_error_file(&cfg),
            config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return(-1);
  }

  MIA_LOG("init mia with %s", mia_config_path.c_str());

  ma::MediaServerApi::Config _config;

  setting = config_lookup(&cfg, "host");
  if(setting) {
    static const char* sub_setting_name[] = 
        {"live", "rtc", "listener", "rtmp2rtc", "http"};
    
    int setting_count = config_setting_length(setting);

    for (int i = 0; i < setting_count; ++i) {
      config_setting_t *sub_item = 
          config_setting_get_member(setting, sub_setting_name[i]);
      
      if (sub_item) {
        int sub_item_count = config_setting_length(sub_item);
        int i1;
        const char *s1 = nullptr; 
    
        if (sub_setting_name[i] == "live") {
          if (config_setting_lookup_string(sub_item, "gop", &s1)) {
            _config.enable_gop_ = (std::string(s1) == "on");
          }

          if (config_setting_lookup_string(sub_item, "flv_record", &s1)) {
            _config.flv_record_ = (std::string(s1) == "on");
          }

          if (config_setting_lookup_int(sub_item, "workers", &i1)) {
            _config.workers_ = i1;
          }

          if (config_setting_lookup_int(sub_item, "ioworkers", &i1)) {
            _config.ioworkers_ = i1;
          }
          
          if (config_setting_lookup_int(sub_item, "queue_length", &i1)) {
            _config.consumer_queue_size_ = i1;
          }
          
          if (config_setting_lookup_int(sub_item, "algo", &i1)) {
            _config.jitter_algo_ = (ma::JitterAlgorithm)i1;
          }

          if (config_setting_lookup_string(sub_item, "mix_correct", &s1)) {
            _config.mix_correct_ = (std::string(s1) == "on");
          }
          
          MIA_LOG("gop:%s flv:%s worker:%d ioworker:%d len:%d al:%d correct:%s", 
                  _config.enable_gop_?"on":"off", 
                  _config.flv_record_?"on":"off",
                  _config.workers_, _config.ioworkers_, 
                  _config.consumer_queue_size_,
                  (int)_config.jitter_algo_,
                  _config.mix_correct_?"on":"off");
          continue;
        } 

        if (sub_setting_name[i] == "rtc") {
          if (config_setting_lookup_int(sub_item, "workers", &i1)) {
            _config.rtc_workers_ = i1;
          }
          if (config_setting_lookup_string(sub_item, "candidates", &s1)) {
            _config.candidates_.emplace_back(s1);
          }
          if (config_setting_lookup_int(sub_item, "stun_port", &i1)) {
            _config.stun_port = i1;
          }
          MIA_LOG("workers:%d candidates:%s stun_port:%d", 
                  _config.rtc_workers_, 
                  _config.candidates_[0].c_str(), 
                  _config.stun_port);
          continue;
        }

        if (sub_setting_name[i] == "listener") {
          if (config_setting_lookup_string(sub_item, "http", &s1)) {
            std::string addr{"http://"};
            addr.append(s1);
            _config.listen_addr_.emplace_back(std::move(addr));
          }

          if (config_setting_lookup_string(sub_item, "https", &s1)) {
            std::string addr{"https://"};
            addr.append(s1);
            _config.listen_addr_.emplace_back(std::move(addr));
          }
          if (config_setting_lookup_string(sub_item, "key", &s1)) {
            _config.https_key = s1;
          }
          if (config_setting_lookup_string(sub_item, "cert", &s1)) {
            _config.https_crt = s1;
          }
          if (config_setting_lookup_string(sub_item, "hostname", &s1)) {
            _config.https_hostname = s1;
          }
          std::string addrs;
          for(const auto& x :  _config.listen_addr_) {
            addrs.append(x);
            addrs.append(" ");
          }
          MIA_LOG("addrs:%s key:%s cert:%s host:%s", 
                  addrs.c_str(), 
                  _config.https_key.c_str(),
                  _config.https_crt.c_str(),
                  _config.https_hostname.c_str());
          continue;
        }

        if (sub_setting_name[i] == "rtmp2rtc") {
          if (config_setting_lookup_int(sub_item, "keyframe_interval", &i1)) {
            _config.request_keyframe_interval = i1;
          }

          MIA_LOG("key:%d", _config.request_keyframe_interval);
          continue;
        }

        if (sub_setting_name[i] == "http") {
          if (config_setting_lookup_string(sub_item, "path", &s1)) {
            _config.path = s1;
          }

          MIA_LOG("path:%s", _config.path.c_str());
        }
      }
    }
  }
  config_destroy(&cfg);

  MIA_LOG("mia start");

  ma::MediaServerApi* server = ma::MediaServerFactory().Create();
  int ret = server->Init(_config);
  if (ma::kma_ok != ret) {
    MIA_LOG("initialize failed, code:%d", ret);
    return ret;
  }

  do {::sleep(1);} while(true);

  MIA_LOG("mia stop");

  return 0;
}

