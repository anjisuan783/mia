#ifndef __MEDIA_SERVER_API_H__
#define __MEDIA_SERVER_API_H__

class IHttpServer;
class CDataPackage;

namespace ma {

// The time jitter algorithm:
// 1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
// 2. zero, only ensure sttream start at zero, ignore timestamp jitter.
// 3. off, disable the time jitter algorithm, like atc.
enum JitterAlgorithm {
  JitterAlgorithmFULL = 0x01,
  JitterAlgorithmZERO,
  JitterAlgorithmOFF
};

class MediaServerApi {
 public:
  struct config {
    bool enable_gop_{true};
    bool enable_atc_{false};
    bool flv_record_{false};
    int consumer_queue_size_{1000};
    JitterAlgorithm jotter_algo_{JitterAlgorithmZERO};
  };
 
  virtual ~MediaServerApi() { }
 
  /*
   *  num1 : thread num of media thread pool
   *  num2 : thread num of connection thread pool
   */
  virtual void Init(unsigned int num1, unsigned int num2) = 0;

  virtual bool OnHttpConnect(IHttpServer*, CDataPackage*) = 0;
};

class MediaServerFactory {
 public:
  MediaServerApi* Create();
};

}

#endif //!__MEDIA_SERVER_API_H__

