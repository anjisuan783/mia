//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __HTTP_MESSAGE_INTERFACE_H__
#define __HTTP_MESSAGE_INTERFACE_H__

namespace ma {

class IMediaConnection;

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
//
// There are some modes to determine the length of body:
//      1. content-length and chunked.
//      2. infinite chunked.
//      3. no body.
// For example:
//      ISrsHttpMessage* r = ...;
//      while (!r->eof()) r->read(); // Read in mode 1 or 3.
// @rmark for mode 2, the infinite chunked, all left data is body.
class ISrsHttpMessage
{
public:
    ISrsHttpMessage() = default;
    virtual ~ISrsHttpMessage() = default;

    virtual std::shared_ptr<IMediaConnection> connection() = 0;
    virtual void connection(std::shared_ptr<IMediaConnection> conn) = 0;
public:
    virtual const std::string& method() = 0;
    virtual uint16_t status_code() = 0;
    // Method helpers.
    virtual bool is_http_get() = 0;
    virtual bool is_http_put() = 0;
    virtual bool is_http_post() = 0;
    virtual bool is_http_delete() = 0;
    virtual bool is_http_options() = 0;
public:
    virtual const std::string& schema() = 0;

    // Whether should keep the connection alive.
    virtual bool is_keep_alive() = 0;
    // The uri contains the host and path.
    virtual std::string uri() = 0;
    // The url maybe the path.
    virtual std::string url() = 0;
    virtual std::string host() = 0;
    virtual std::string path() = 0;
    virtual std::string query() = 0;
    virtual std::string ext() = 0;
    // Get the RESTful id, in string,
    // for example, pattern is /api/v1/streams, path is /api/v1/streams/100,
    // then the rest id is 100.
    // @param pattern the handler pattern which will serve the request.
    // @return the REST id; "" if not matched.
    virtual std::string parse_rest_id(std::string pattern) = 0;

    // The content length, -1 for chunked or not set.
    virtual int64_t content_length() = 0;
public:
    // Get the param in query string,
    // for instance, query is "start=100&end=200",
    // then query_get("start") is "100", and query_get("end") is "200"
    virtual std::string query_get(const std::string& key) = 0;
    // Get the headers.
    virtual SrsHttpHeader& header() = 0;

    virtual const std::string& get_body() = 0;
public:
    // Whether the current request is JSONP,
    // which has a "callback=xxx" in QueryString.
    virtual bool is_jsonp() = 0;
};

}
#endif //!__HTTP_MESSAGE_INTERFACE_H__

