//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __GS_HTTP_STACK_H__
#define __GS_HTTP_STACK_H__

#include "common/media_kernel_error.h"

#include <map>
#include <string>
#include <memory>

#include "http/h/http_message.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"

namespace ma {

// parse uri from schema/server:port/path?query

class SrsHttpUri final {
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
  HttpMessage(std::string_view body);
  ~HttpMessage() = default;

  void set_basic(uint8_t type, uint8_t method, 
      uint16_t status, int64_t content_length);
  void set_header(const SrsHttpHeader&, bool keep_alive);
  srs_error_t set_url(const std::string& url, bool allow_jsonp);
  void set_https(bool v);

  std::shared_ptr<IMediaConnection> connection() override;
  void connection(std::shared_ptr<IMediaConnection>) override;

public:
  // The schema, http or https.
  const std::string& schema() override;
  std::string method() override;
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

  void set_body_eof() {
    body_eof_ = true;
  }
  
  bool is_body_eof() override {
    return body_eof_;
  }

  void on_body(std::string_view data);

  std::shared_ptr<MediaRequest> to_request(const std::string&);
  
 private:
  uint8_t method_i();
 private:  
  // The request type defined as
  //      enum http_parser_type { HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH };
  uint8_t type_{0};
  // The HTTP method defined by HTTP_METHOD_MAP
  uint8_t method_{0};
  
  uint16_t status_;
  int64_t content_length_{-1};
  int64_t body_recv_length_{0};

  std::string body_;

  bool body_eof_{false};

  // The http headers
  SrsHttpHeader header_;
  // Whether the request indicates should keep alive for the http connection.
  bool keep_alive_{true};
  // Whether the body is chunked.
  bool chunked_{false};

  std::string schema_{"http"};
  // The parsed url.
  std::string url_;
  // The extension of file, for example, .flv
  std::string ext_;
  // The uri parser
  std::unique_ptr<SrsHttpUri> uri_;
  // The query map
  std::map<std::string, std::string> _query;

  // Whether request is jsonp.
  bool jsonp{false};
  // The method in QueryString will override the HTTP method.
  std::string jsonp_method_;

  std::shared_ptr<IMediaConnection> owner_;
};

/* Compile with -DHTTP_PARSER_STRICT=0 to make less checks, but run
 * faster
 */
#ifndef HTTP_PARSER_STRICT
# define HTTP_PARSER_STRICT 1
#endif

/* Maximium header size allowed. If the macro is not defined
 * before including this header then the default is used. To
 * change the maximum header size, define the macro in the build
 * environment (e.g. -DHTTP_MAX_HEADER_SIZE=<value>). To remove
 * the effective limit on the size of the header, define the macro
 * to a very large number (e.g. -DHTTP_MAX_HEADER_SIZE=0x7fffffff)
 */
#ifndef HTTP_MAX_HEADER_SIZE
# define HTTP_MAX_HEADER_SIZE (80*1024)
#endif

struct http_parser {
  /** PRIVATE **/
  unsigned int type : 2;         /* enum http_parser_type */
  unsigned int flags : 8;        /* F_* values from 'flags' enum; semi-public */
  unsigned int state : 7;        /* enum state from http_parser.c */
  unsigned int header_state : 7; /* enum header_state from http_parser.c */
  unsigned int index : 7;        /* index into current matcher */
  unsigned int lenient_http_headers : 1;

  uint32_t nread;          /* # bytes read in various scenarios */
  uint64_t content_length; /* # bytes in body (0 if no Content-Length header) */

  /** READ-ONLY **/
  unsigned short http_major;
  unsigned short http_minor;
  unsigned int status_code : 16; /* responses only */
  unsigned int method : 8;       /* requests only */
  unsigned int http_errno : 7;

  /* 1 = Upgrade header was present and the parser has exited because of that.
   * 0 = No upgrade header present.
   * Should be checked when http_parser_execute() returns in addition to
   * error checking.
   */
  unsigned int upgrade : 1;

  /** PUBLIC **/
  void *data; /* A pointer to get hook to the "connection" or "socket" object */
};


typedef int (*http_data_cb) (http_parser*, const char *at, size_t length);
typedef int (*http_cb) (http_parser*);

struct http_parser_settings {
  http_cb      on_message_begin;
  http_data_cb on_url;
  http_data_cb on_status;
  http_data_cb on_header_field;
  http_data_cb on_header_value;
  http_cb      on_headers_complete;
  http_data_cb on_body;
  http_cb      on_message_complete;
  /* When on_chunk_header is called, the current chunk length is stored
   * in parser->content_length.
   */
  http_cb      on_chunk_header;
  http_cb      on_chunk_complete;
};

void http_parser_init(http_parser *parser, enum http_parser_type t);

int http_should_keep_alive (const http_parser *parser);

size_t http_parser_execute(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           const size_t len);

// bodyAllowedForStatus reports whether a given response status code
// permits a body.  See RFC2616, section 4.4.
bool srs_go_http_body_allowd(int status);

// DetectContentType implements the algorithm described
// at http://mimesniff.spec.whatwg.org/ to determine the
// Content-Type of the given data.  It considers at most the
// first 512 bytes of data.  DetectContentType always returns
// a valid MIME type: if it cannot determine a more specific one, it
// returns "application/octet-stream".
inline std::string srs_go_http_detect() {
  // TODO: Implement the request content-type detecting.
  return "application/octet-stream"; // fallback
}

std::string_view generate_http_status_text(int status);

/* Map for errno-related constants
*
* The provided argument should be a macro that takes 2 arguments.
*/
#define HTTP_ERRNO_MAP(XX)                                           \
 /* No error */                                                     \
 XX(OK, "success")                                                  \
                                                                    \
 /* Callback-related errors */                                      \
 XX(CB_message_begin, "the on_message_begin callback failed")       \
 XX(CB_url, "the on_url callback failed")                           \
 XX(CB_header_field, "the on_header_field callback failed")         \
 XX(CB_header_value, "the on_header_value callback failed")         \
 XX(CB_headers_complete, "the on_headers_complete callback failed") \
 XX(CB_body, "the on_body callback failed")                         \
 XX(CB_message_complete, "the on_message_complete callback failed") \
 XX(CB_status, "the on_status callback failed")                     \
 XX(CB_chunk_header, "the on_chunk_header callback failed")         \
 XX(CB_chunk_complete, "the on_chunk_complete callback failed")     \
                                                                    \
 /* Parsing-related errors */                                       \
 XX(INVALID_EOF_STATE, "stream ended at an unexpected time")        \
 XX(HEADER_OVERFLOW,                                                \
    "too many header bytes seen; overflow detected")                \
 XX(CLOSED_CONNECTION,                                              \
    "data received after completed connection: close message")      \
 XX(INVALID_VERSION, "invalid HTTP version")                        \
 XX(INVALID_STATUS, "invalid HTTP status code")                     \
 XX(INVALID_METHOD, "invalid HTTP method")                          \
 XX(INVALID_URL, "invalid URL")                                     \
 XX(INVALID_HOST, "invalid host")                                   \
 XX(INVALID_PORT, "invalid port")                                   \
 XX(INVALID_PATH, "invalid path")                                   \
 XX(INVALID_QUERY_STRING, "invalid query string")                   \
 XX(INVALID_FRAGMENT, "invalid fragment")                           \
 XX(LF_EXPECTED, "LF character expected")                           \
 XX(INVALID_HEADER_TOKEN, "invalid character in header")            \
 XX(INVALID_CONTENT_LENGTH,                                         \
    "invalid character in content-length header")                   \
 XX(UNEXPECTED_CONTENT_LENGTH,                                      \
    "unexpected content-length header")                             \
 XX(INVALID_CHUNK_SIZE,                                             \
    "invalid character in chunk size header")                       \
 XX(INVALID_CONSTANT, "invalid constant string")                    \
 XX(INVALID_INTERNAL_STATE, "encountered unexpected internal state")\
 XX(STRICT, "strict mode assertion failed")                         \
 XX(PAUSED, "parser is paused")                                     \
 XX(UNKNOWN, "an unknown error occurred")

/* Define HPE_* values for each errno value above */
#define HTTP_ERRNO_GEN(n, s) HPE_##n,
enum http_errno {
 HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
};
#undef HTTP_ERRNO_GEN

const char* http_errno_name(enum http_errno err);
const char* http_errno_description(enum http_errno err);

/* Get an http_errno value from an http_parser */
#define HTTP_PARSER_ERRNO(p) ((enum http_errno) (p)->http_errno)

/* Request Methods */
#define HTTP_METHOD_MAP(XX)         \
  XX(0,  DELETE,      DELETE)       \
  XX(1,  GET,         GET)          \
  XX(2,  HEAD,        HEAD)         \
  XX(3,  POST,        POST)         \
  XX(4,  PUT,         PUT)          \
  /* pathological */                \
  XX(5,  CONNECT,     CONNECT)      \
  XX(6,  OPTIONS,     OPTIONS)      \
  XX(7,  TRACE,       TRACE)        \
  /* WebDAV */                      \
  XX(8,  COPY,        COPY)         \
  XX(9,  LOCK,        LOCK)         \
  XX(10, MKCOL,       MKCOL)        \
  XX(11, MOVE,        MOVE)         \
  XX(12, PROPFIND,    PROPFIND)     \
  XX(13, PROPPATCH,   PROPPATCH)    \
  XX(14, SEARCH,      SEARCH)       \
  XX(15, UNLOCK,      UNLOCK)       \
  XX(16, BIND,        BIND)         \
  XX(17, REBIND,      REBIND)       \
  XX(18, UNBIND,      UNBIND)       \
  XX(19, ACL,         ACL)          \
  /* subversion */                  \
  XX(20, REPORT,      REPORT)       \
  XX(21, MKACTIVITY,  MKACTIVITY)   \
  XX(22, CHECKOUT,    CHECKOUT)     \
  XX(23, MERGE,       MERGE)        \
  /* upnp */                        \
  XX(24, MSEARCH,     M-SEARCH)     \
  XX(25, NOTIFY,      NOTIFY)       \
  XX(26, SUBSCRIBE,   SUBSCRIBE)    \
  XX(27, UNSUBSCRIBE, UNSUBSCRIBE)  \
  /* RFC-5789 */                    \
  XX(28, PATCH,       PATCH)        \
  XX(29, PURGE,       PURGE)        \
  /* CalDAV */                      \
  XX(30, MKCALENDAR,  MKCALENDAR)   \
  /* RFC-2068, section 19.6.1.2 */  \
  XX(31, LINK,        LINK)         \
  XX(32, UNLINK,      UNLINK)       \
  /* icecast */                     \
  XX(33, SOURCE,      SOURCE)       \

enum http_method {
#define XX(num, name, string) HTTP_##name = num,
  HTTP_METHOD_MAP(XX)
#undef XX
};

}

#endif //!__GS_HTTP_STACK_H__

