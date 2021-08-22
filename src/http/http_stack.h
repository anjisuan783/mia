//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __GS_HTTP_STACK_H__
#define __GS_HTTP_STACK_H__

#include "common/srs_kernel_error.h"

#include <map>
#include <string>

namespace ma {

// For http parser macros
#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

// parse uri from schema/server:port/path?query

class SrsHttpUri final
{
private:
  std::string url;
  std::string schema;
  std::string host;
  int port{0};
  std::string path;
  std::string query;
  std::string username_;
  std::string password_;
  std::map<std::string, std::string> query_values_;
public:
  SrsHttpUri() = default;
  ~SrsHttpUri() = default;
public:
  // Initialize the http uri.
  virtual srs_error_t initialize(std::string _url);
  // After parsed the message, set the schema to https.
  virtual void set_schema(std::string v);
public:
  virtual std::string get_url();
  virtual std::string get_schema();
  virtual std::string get_host();
  virtual int get_port();
  virtual std::string get_path();
  virtual std::string get_query();
  virtual std::string get_query_by_key(std::string key);
  virtual std::string username();
  virtual std::string password();
private:
  // Get the parsed url field.
  // @return return empty string if not set.
  virtual std::string get_uri_field(std::string uri, void* hp_u, int field);
  srs_error_t parse_query();
public:
  static std::string query_escape(std::string s);
  static std::string path_escape(std::string s);
  static srs_error_t query_unescape(std::string s, std::string& value);
  static srs_error_t path_unescape(std::string s, std::string& value);
};

// A Header represents the key-value pairs in an HTTP header.
class SrsHttpHeader
{
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
  void set(std::string key, std::string value);
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
  void set_content_type(std::string ct);
public:
  // write all headers to string stream.
  void write(std::stringstream& ss);
  const std::map<std::string, std::string>& header();

  void clear() { headers.clear(); }
};

std::string_view generate_http_status_text(int status);

}

#endif //!__GS_HTTP_STACK_H__

