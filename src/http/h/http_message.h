//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __HTTP_MESSAGE_INTERFACE_H__
#define __HTTP_MESSAGE_INTERFACE_H__

#include <sstream>
#include <string>
#include <map>
#include <memory>

#include "rtc_base/sigslot.h"
#include "rtmp/media_req.h"

namespace ma {

// A Header represents the key-value pairs in an HTTP header.
class SrsHttpHeader {
private:
  // The order in which header fields with differing field names are
  // received is not significant. However, it is "good practice" to send
  // general-header fields first, followed by request-header or response-
  // header fields, and ending with the entity-header fields.
  // @doc https://tools.ietf.org/html/rfc2616#section-4.2
  std::map<std::string, std::string> headers;
public:
  SrsHttpHeader() = default;
  ~SrsHttpHeader() = default;
public:
  // Add adds the key, value pair to the header.
  // It appends to any existing values associated with key.
  void set(const std::string& key, const std::string& value);
  // Get gets the first value associated with the given key.
  // If there are no values associated with the key, Get returns "".
  // To access multiple values of a key, access the map directly
  // with CanonicalHeaderKey.
  std::string get(const std::string& key) const;
  // Delete the http header indicated by key.
  // Return the removed header field.
  void del(const std::string&);
  // Get the count of headers.
  int count();
public:
  // Get the content length. -1 if not set.
  int64_t content_length();
  // set the content length by header "Content-Length"
  void set_content_length(int64_t size);
public:
  // Get the content type. empty string if not set.
  std::string content_type();
  // set the content type by header "Content-Type"
  void set_content_type(const std::string& ct);
public:
  // write all headers to string stream.
  void write(std::stringstream& ss);
  const std::map<std::string, std::string>& header();

  void clear() { headers.clear(); }
};

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
class ISrsHttpMessage {
 public:
  ISrsHttpMessage() = default;
  virtual ~ISrsHttpMessage() = default;

  virtual std::shared_ptr<IMediaConnection> connection() = 0;
  virtual void connection(std::shared_ptr<IMediaConnection> conn) = 0;
  virtual std::string method() = 0;
  virtual uint16_t status_code() = 0;
  // Method helpers.
  virtual bool is_http_get() = 0;
  virtual bool is_http_put() = 0;
  virtual bool is_http_post() = 0;
  virtual bool is_http_delete() = 0;
  virtual bool is_http_options() = 0;
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

  virtual std::shared_ptr<MediaRequest> to_request(const std::string&) = 0;
  // Get the param in query string,
  // for instance, query is "start=100&end=200",
  // then query_get("start") is "100", and query_get("end") is "200"
  virtual std::string query_get(const std::string& key) = 0;
  // Get the headers.
  virtual SrsHttpHeader& header() = 0;

  virtual const std::string& get_body() = 0;

  virtual bool is_body_eof() = 0;

  // Whether the current request is JSONP,
  // which has a "callback=xxx" in QueryString.
  virtual bool is_jsonp() = 0;

  // Used only for body notify.
  sigslot::signal1<const std::string&> SignalOnBody_;
};

}
#endif //!__HTTP_MESSAGE_INTERFACE_H__

