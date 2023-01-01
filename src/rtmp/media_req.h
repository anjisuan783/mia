//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_REQUEST_H__
#define __MEDIA_REQUEST_H__

#include <string>
#include <memory>

#include "common/media_define.h"

namespace ma {

class RtmpAmf0Object;

// The original request from client.
class MediaRequest final {
public:
    // The client ip.
    std::string ip;
public:
    // The tcUrl: rtmp://request_vhost:port/app/stream
    // support pass vhost in query string, such as:
    //    rtmp://ip:port/app?vhost=request_vhost/stream
    //    rtmp://ip:port/app...vhost...request_vhost/stream
    std::string tcUrl;
    std::string pageUrl;
    std::string swfUrl;
    double objectEncoding;
// The data discovery from request.
public:
    // Discovery from tcUrl and play/publish.
    std::string schema;
    // The vhost in tcUrl.
    std::string vhost;
    // The host in tcUrl.
    std::string host;
    // The port in tcUrl.
    int port;
    // The app in tcUrl, without param.
    std::string app;
    // The param in tcUrl(app).
    std::string param;
    // The stream in play/publish
    std::string stream;
    // For play live stream,
    // used to specified the stop when exceed the duration.
    // @see https://github.com/ossrs/srs/issues/45
    // in srs_utime_t.
    srs_utime_t duration;
    // The token in the connect request,
    // used for edge traverse to origin authentication,
    // @see https://github.com/ossrs/srs/issues/104
    RtmpAmf0Object* args = nullptr;
public:
    MediaRequest();
    ~MediaRequest();
public:
    // Deep copy the request, for source to use it to support reload,
    // For when initialize the source, the request is valid,
    // When reload it, the request maybe invalid, so need to copy it.
    std::shared_ptr<MediaRequest> copy();
    // update the auth info of request,
    // To keep the current request ptr is ok,
    // For many components use the ptr of request.
    void update_auth(MediaRequest* req);
    // Get the stream identify, vhost/app/stream.
    std::string get_stream_url();
    // To strip url, user must strip when update the url.
    void strip();
public:
    // Transform it as HTTP request.
    MediaRequest* as_http();
};

}

#endif //!__MEDIA_REQUEST_H__
