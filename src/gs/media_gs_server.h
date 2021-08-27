#ifdef __GS__

#ifndef __MEDIA_GS_SERVER_H__
#define __MEDIA_GS_SERVER_H__

#include "h/media_gs_adapter.h"

namespace ma {

class GsServer final : public IGsServer{
 public:
  GsServer() = default;
  bool OnHttpConnect(IHttpServer*, CDataPackage*) override;
};

}

#endif //!__MEDIA_GS_SERVER_H__

#endif //__GS__

