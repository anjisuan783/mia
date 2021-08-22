#ifndef __MEDIA_SERVER_H__
#define __MEDIA_SERVER_H__

#include <set>
#include <memory>

#include "h/media_server_api.h"
#include "common/srs_kernel_error.h"

namespace ma {

class IGsHttpHandler;
class MediaSource;
class MediaRequest;

class MediaServerImp final : public MediaServerApi {

  friend class MediaConnMgr;
 public:
  MediaServerImp() = default;
  virtual ~MediaServerImp() = default;

  void Init(unsigned int num1, unsigned int num2) override;

  srs_error_t on_publish(std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r);
  void on_unpublish(std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r);

 private:
  bool OnHttpConnect(IHttpServer*, CDataPackage*) override;

 public:
  config config_;
  
 private:
  std::unique_ptr<IGsHttpHandler> mux_;
  bool inited_{false};
};

extern MediaServerImp g_server_;

}

#endif //!__MEDIA_SERVER_H__
