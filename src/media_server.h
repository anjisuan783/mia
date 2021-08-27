#ifndef __MEDIA_SERVER_H__
#define __MEDIA_SERVER_H__

#include <set>
#include <memory>

#include "h/media_server_api.h"
#include "common/srs_kernel_error.h"

namespace ma {

class IMediaHttpHandler;
class MediaSource;
class MediaRequest;

class MediaServerImp final : public MediaServerApi {

  friend class MediaConnMgr;
 public:
  MediaServerImp() = default;
  virtual ~MediaServerImp() = default;

  int Init(const config&) override;

  srs_error_t on_publish(std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r);
  void on_unpublish(std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r);

 public:
  config config_;
  
 private:
  std::unique_ptr<IMediaHttpHandler> mux_;
  bool inited_{false};
};

extern MediaServerImp g_server_;

}

#endif //!__MEDIA_SERVER_H__

