//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_RTMP_CONNECTION_H__
#define __MEDIA_RTMP_CONNECTION_H__

#include "connection/h/conn_interface.h"

namespace ma {

class MediaDummyConnection : public IMediaConnection {
 public:
  void Start() override { }
  void Disconnect() override { }
};

}

#endif //!__MEDIA_RTMP_CONNECTION_H__
