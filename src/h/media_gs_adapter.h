#ifndef __MEDIA_OBSERVER_H__
#define __MEDIA_OBSERVER_H__

#include <string>
#include <memory>

#include "datapackage.h"

namespace ma{

class IMediaPublisher {
 public:
  enum Type{
    t_video_avg,
    t_video_k,
    t_video_p
  };
 
  virtual ~IMediaPublisher() = default;

  virtual void OnPublish(const std::string& tcUrl, const std::string& stream) = 0;
  virtual void OnUnpublish() = 0;

  virtual void OnVideo(CDataPackage&, uint32_t timestamp, Type) = 0;
  virtual void OnAudio(CDataPackage&, uint32_t timestamp) = 0;
};

class MediaPublisherFactory {
 public:
  std::shared_ptr<IMediaPublisher> Create();
};

//TODO(nisse) not implemnet
class IMediaSubscriber {
 public:
  enum Type{
    t_video_avg,
    t_video_k,
    t_video_p
  };
 
  virtual ~IMediaSubscriber() = default;

  virtual void OnPublish() = 0;
  virtual void OnUnpublish() = 0;
};

}

#endif  //!MediaObserver

