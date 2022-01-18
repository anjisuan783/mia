//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_CONNECTION_INTERFACE_H__
#define __MEDIA_CONNECTION_INTERFACE_H__

class CDataPackage;
class ITransport;

namespace ma {

class IMediaConnection : public 
    std::enable_shared_from_this<IMediaConnection> {
 public:
  virtual ~IMediaConnection() { }

  virtual void Start() = 0;
  virtual void Disconnect() = 0;

  virtual std::string Ip() = 0;
};

}

#endif //!__MEDIA_CONNECTION_INTERFACE_H__

