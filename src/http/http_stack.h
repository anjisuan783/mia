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
#include <memory>

#include "http/h/http_message.h"

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

class HttpMessage final : public ISrsHttpMessage {
 public:
  HttpMessage(const std::string& body);
  ~HttpMessage() = default;

  void set_basic(uint8_t type, const std::string& method, uint16_t status, int64_t content_length);
  void set_header(const SrsHttpHeader&, bool keep_alive);
  srs_error_t set_url(const std::string& url, bool allow_jsonp);
  void set_https(bool v);

  std::shared_ptr<IMediaConnection> connection() override;
  void connection(std::shared_ptr<IMediaConnection>) override;

public:
  // The schema, http or https.
  const std::string& schema() override;
  const std::string& method() override;
  uint16_t status_code() override;

  //method helper
  bool is_http_get() override;
  bool is_http_put() override;
  bool is_http_post() override;
  bool is_http_delete() override;
  bool is_http_options() override;
  // Whether body is chunked encoding, for reader only.
  bool is_chunked();
  // Whether should keep the connection alive.
  bool is_keep_alive() override;
  // The uri contains the host and path.
  std::string uri() override;
  // The url maybe the path.
  std::string url() override;
  std::string host() override;
  int port();
  std::string path() override;
  std::string query() override;
  std::string ext() override;
  // Get the RESTful matched id.
  std::string parse_rest_id(std::string pattern) override;

  bool is_jsonp() override;

  SrsHttpHeader& header() override;

  std::string query_get(const std::string& key) override;

  const std::string& get_body() override;

  int64_t content_length() override;
  
private:
  // The request type defined as
  //      enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };
  uint8_t type_{0};
  // The HTTP method defined by HTTP_METHOD_MAP
  std::string _method;
  uint16_t _status;
  int64_t _content_length{-1};

  std::string _body;

  // The http headers
  SrsHttpHeader _header;
  // Whether the request indicates should keep alive for the http connection.
  bool _keep_alive{true};
  // Whether the body is chunked.
  bool chunked{false};

  std::string schema_{"http"};
  // The parsed url.
  std::string _url;
  // The extension of file, for example, .flv
  std::string _ext;
  // The uri parser
  std::unique_ptr<SrsHttpUri> _uri;
  // The query map
  std::map<std::string, std::string> _query;

  // Whether request is jsonp.
  bool jsonp{false};
  // The method in QueryString will override the HTTP method.
  std::string jsonp_method;

  std::shared_ptr<IMediaConnection> owner_;
};


std::string_view generate_http_status_text(int status);

}

#endif //!__GS_HTTP_STACK_H__

