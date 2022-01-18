//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_SERVER_API_H__
#define __MEDIA_SERVER_API_H__

#include <string>
#include <vector>

namespace ma {

// The time jitter algorithm:
// 1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
// 2. zero, only ensure sttream start at zero, ignore timestamp jitter.
// 3. off, disable the time jitter algorithm, like atc.
enum JitterAlgorithm {
  JitterAlgorithmFULL = 1,
  JitterAlgorithmZERO = 2,
  JitterAlgorithmOFF = 3
};

class MediaServerApi {
 public:
  struct Config {
    uint32_t workers_{1};               // live workers
    uint32_t ioworkers_{1};             // TODO multi-theads not implement
    bool enable_gop_{true};
    bool flv_record_{false};
    int consumer_queue_size_{30000};    // ms
    JitterAlgorithm jitter_algo_{JitterAlgorithmZERO};
    bool mix_correct_{false};           // fix live timestamp by map
 
    //for rtc
    uint32_t rtc_workers_{1};
    std::vector<std::string> candidates_;  //local candidates [ip]
    uint16_t stun_port{9000}; //not use

    //for https
    std::string https_key{"./conf/mia.key"};  // pem fromat private key file path
    std::string https_crt{"./conf/mia.crt"};  // pem fromat certificate file path
    std::string https_hostname;

    //for rtmp2rtc
    int32_t request_keyframe_interval{5}; // second

    //for vhost
    std::string vhost{""};

    //for http server static file path
    std::string path{"."};

    std::vector<std::string> listen_addr_;   // [schema://ip:port]
  };
 
  virtual ~MediaServerApi() { }
 
  /*
   *  num1 : thread num of media thread pool
   *  num2 : thread num of connection thread pool  TODO not implement
   */
  virtual int Init(const Config&) = 0;

  virtual void Dump() = 0;
};

class MediaServerFactory {
 public:
  MediaServerApi* Create();
};

}

#endif //!__MEDIA_SERVER_API_H__

