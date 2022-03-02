#include "http/http_stack.h"

#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <math.h>
#include <stdlib.h>
#include <map>
#include <sstream>
#include <memory>
#include <cstring>

#include "common/media_define.h"
#include "common/media_log.h"
#include "http/http_consts.h"
#include "common/media_kernel_error.h"
#include "utils/protocol_utility.h"


namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.http");


/* Copyright Joyent, Inc. and other Node contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef BIT_AT
# define BIT_AT(a, i)                                                \
  (!!((unsigned int) (a)[(unsigned int) (i) >> 3] &                  \
   (1 << ((unsigned int) (i) & 7))))
#endif

using namespace std;

/* Tokens as defined by rfc 2616. Also lowercases them.
 *        token       = 1*<any CHAR except CTLs or separators>
 *     separators     = "(" | ")" | "<" | ">" | "@"
 *                    | "," | ";" | ":" | "\" | <">
 *                    | "/" | "[" | "]" | "?" | "="
 *                    | "{" | "}" | SP | HT
 */
static const char tokens[256] = {
/*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
        0,       0,       0,       0,       0,       0,       0,       0,
/*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
        0,       0,       0,       0,       0,       0,       0,       0,
/*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
        0,       0,       0,       0,       0,       0,       0,       0,
/*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
        0,       0,       0,       0,       0,       0,       0,       0,
/*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
       ' ',     '!',      0,      '#',     '$',     '%',     '&',    '\'',
/*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
        0,       0,      '*',     '+',      0,      '-',     '.',      0,
/*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
       '0',     '1',     '2',     '3',     '4',     '5',     '6',     '7',
/*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
       '8',     '9',      0,       0,       0,       0,       0,       0,
/*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
        0,      'a',     'b',     'c',     'd',     'e',     'f',     'g',
/*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
       'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
/*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
       'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
/*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
       'x',     'y',     'z',      0,       0,       0,      '^',     '_',
/*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
       '`',     'a',     'b',     'c',     'd',     'e',     'f',     'g',
/* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
       'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
/* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
       'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
/* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
       'x',     'y',     'z',      0,      '|',      0,      '~',       0 };

static const int8_t unhex[256] ={
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
 ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
 ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
 , 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1
 ,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1
 ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
 ,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1
 ,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

#if HTTP_PARSER_STRICT
# define T(v) 0
#else
# define T(v) v
#endif
       
 static const uint8_t normal_url_char[32] = {
 /*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
         0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
 /*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
         0    | T(2)   |   0    |   0    | T(16)  |   0    |   0    |   0,
 /*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
         0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
 /*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
         0    |   0    |   0    |   0    |   0    |   0    |   0    |   0,
 /*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
         0    |   2    |   4    |   0    |   16   |   32   |   64   |  128,
 /*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |   0,
 /*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |  128,
 /* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
         1    |   2    |   4    |   8    |   16   |   32   |   64   |   0, };
       
#undef T


/* Macros for character classes; depends on strict-mode  */
#define CR                  '\r'
#define LF                  '\n'
#define LOWER(c)            (unsigned char)(c | 0x20)
#define IS_ALPHA(c)         (LOWER(c) >= 'a' && LOWER(c) <= 'z')
#define IS_NUM(c)           ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)      (IS_ALPHA(c) || IS_NUM(c))
#define IS_HEX(c)           (IS_NUM(c) || (LOWER(c) >= 'a' && LOWER(c) <= 'f'))
#define IS_MARK(c)          ((c) == '-' || (c) == '_' || (c) == '.' || \
  (c) == '!' || (c) == '~' || (c) == '*' || (c) == '\'' || (c) == '(' || \
  (c) == ')')
#define IS_USERINFO_CHAR(c) (IS_ALPHANUM(c) || IS_MARK(c) || (c) == '%' || \
  (c) == ';' || (c) == ':' || (c) == '&' || (c) == '=' || (c) == '+' || \
  (c) == '$' || (c) == ',')

#define STRICT_TOKEN(c)     ((c == ' ') ? 0 : tokens[(unsigned char)c])

#if HTTP_PARSER_STRICT
#define TOKEN(c)            STRICT_TOKEN(c)
#define IS_URL_CHAR(c)      (BIT_AT(normal_url_char, (unsigned char)c))
#define IS_HOST_CHAR(c)     (IS_ALPHANUM(c) || (c) == '.' || (c) == '-')
#else
#define TOKEN(c)            tokens[(unsigned char)c]
#define IS_URL_CHAR(c)                                                         \
  (BIT_AT(normal_url_char, (unsigned char)c) || ((c) & 0x80))
#define IS_HOST_CHAR(c)                                                        \
  (IS_ALPHANUM(c) || (c) == '.' || (c) == '-' || (c) == '_')
#endif

enum http_parser_url_fields
  { UF_SCHEMA           = 0
  , UF_HOST             = 1
  , UF_PORT             = 2
  , UF_PATH             = 3
  , UF_QUERY            = 4
  , UF_FRAGMENT         = 5
  , UF_USERINFO         = 6
  , UF_MAX              = 7
  };

  enum http_host_state
    {
      s_http_host_dead = 1
    , s_http_userinfo_start
    , s_http_userinfo
    , s_http_host_start
    , s_http_host_v6_start
    , s_http_host
    , s_http_host_v6
    , s_http_host_v6_end
    , s_http_host_v6_zone_start
    , s_http_host_v6_zone
    , s_http_host_port_start
    , s_http_host_port
  };


/* Result structure for http_parser_parse_url().
 *
 * Callers should index into field_data[] with UF_* values iff field_set
 * has the relevant (1 << UF_*) bit set. As a courtesy to clients (and
 * because we probably have padding left over), we convert any port to
 * a uint16_t.
 */
struct http_parser_url {
  uint16_t field_set;           /* Bitmask of (1 << UF_*) values */
  uint16_t port;                /* Converted UF_PORT string */

  struct {
    uint16_t off;               /* Offset into buffer in which field starts */
    uint16_t len;               /* Length of run in buffer */
  } field_data[UF_MAX];
};

enum state { 
  s_dead = 1, /* important that this is > 0 */

  s_start_req_or_res,
  s_res_or_resp_H,
  s_start_res,
  s_res_H,
  s_res_HT,
  s_res_HTT,
  s_res_HTTP,
  s_res_http_major,
  s_res_http_dot,
  s_res_http_minor,
  s_res_http_end,
  s_res_first_status_code,
  s_res_status_code,
  s_res_status_start,
  s_res_status,
  s_res_line_almost_done,

  s_start_req,

  s_req_method,
  s_req_spaces_before_url,
  s_req_schema,
  s_req_schema_slash,
  s_req_schema_slash_slash,
  s_req_server_start,
  s_req_server,
  s_req_server_with_at,
  s_req_path,
  s_req_query_string_start,
  s_req_query_string,
  s_req_fragment_start,
  s_req_fragment,
  s_req_http_start,
  s_req_http_H,
  s_req_http_HT,
  s_req_http_HTT,
  s_req_http_HTTP,
  s_req_http_I,
  s_req_http_IC,
  s_req_http_major,
  s_req_http_dot,
  s_req_http_minor,
  s_req_http_end,
  s_req_line_almost_done,

  s_header_field_start,
  s_header_field,
  s_header_value_discard_ws,
  s_header_value_discard_ws_almost_done,
  s_header_value_discard_lws,
  s_header_value_start,
  s_header_value,
  s_header_value_lws,

  s_header_almost_done,
  s_chunk_size_start,
  s_chunk_size,
  s_chunk_parameters,
  s_chunk_size_almost_done,

  s_headers_almost_done,
  s_headers_done,

  /* Important: 's_headers_done' must be the last 'header' state. All
   * states beyond this must be 'body' states. It is used for overflow
   * checking. See the PARSING_HEADER() macro.
   */

  s_chunk_data,
  s_chunk_data_almost_done,
  s_chunk_data_done,

  s_body_identity,
  s_body_identity_eof,

  s_message_done
};


/* Our URL parser.
 *
 * This is designed to be shared by http_parser_execute() for URL validation,
 * hence it has a state transition + byte-for-byte interface. In addition, it
 * is meant to be embedded in http_parser_parse_url(), which does the dirty
 * work of turning state transitions URL components for its API.
 *
 * This function should only be invoked with non-space characters. It is
 * assumed that the caller cares about (and can detect) the transition between
 * URL and non-URL states by looking for these.
 */
static enum state parse_url_char(enum state s, const char ch) {
  if (ch == ' ' || ch == '\r' || ch == '\n') {
    return s_dead;
  }

#if HTTP_PARSER_STRICT
  if (ch == '\t' || ch == '\f') {
    return s_dead;
  }
#endif

  switch (s) {
    case s_req_spaces_before_url:
      /* Proxied requests are followed by scheme of an absolute URI (alpha).
       * All methods except CONNECT are followed by '/' or '*'.
       */

      if (ch == '/' || ch == '*') {
        return s_req_path;
      }

      if (IS_ALPHA(ch)) {
        return s_req_schema;
      }

      break;

    case s_req_schema:
      if (IS_ALPHA(ch)) {
        return s;
      }

      if (ch == ':') {
        return s_req_schema_slash;
      }

      break;

    case s_req_schema_slash:
      if (ch == '/') {
        return s_req_schema_slash_slash;
      }

      break;

    case s_req_schema_slash_slash:
      if (ch == '/') {
        return s_req_server_start;
      }

      break;

    case s_req_server_with_at:
      if (ch == '@') {
        return s_dead;
      }

    /* fall through */
    case s_req_server_start:
    case s_req_server:
      if (ch == '/') {
        return s_req_path;
      }

      if (ch == '?') {
        return s_req_query_string_start;
      }

      if (ch == '@') {
        return s_req_server_with_at;
      }

      if (IS_USERINFO_CHAR(ch) || ch == '[' || ch == ']') {
        return s_req_server;
      }

      break;

    case s_req_path:
      if (IS_URL_CHAR(ch)) {
        return s;
      }

      switch (ch) {
        case '?':
          return s_req_query_string_start;

        case '#':
          return s_req_fragment_start;
      }

      break;

    case s_req_query_string_start:
    case s_req_query_string:
      if (IS_URL_CHAR(ch)) {
        return s_req_query_string;
      }

      switch (ch) {
        case '?':
          /* allow extra '?' in query string */
          return s_req_query_string;

        case '#':
          return s_req_fragment_start;
      }

      break;

    case s_req_fragment_start:
      if (IS_URL_CHAR(ch)) {
        return s_req_fragment;
      }

      switch (ch) {
        case '?':
          return s_req_fragment;

        case '#':
          return s;
      }

      break;

    case s_req_fragment:
      if (IS_URL_CHAR(ch)) {
        return s;
      }

      switch (ch) {
        case '?':
        case '#':
          return s;
      }

      break;

    default:
      break;
  }

  /* We should never fall out of the switch above unless there's an error */
  return s_dead;
}

static enum http_host_state
http_parse_host_char(enum http_host_state s, const char ch) {
  switch(s) {
    case s_http_userinfo:
    case s_http_userinfo_start:
      if (ch == '@') {
        return s_http_host_start;
      }

      if (IS_USERINFO_CHAR(ch)) {
        return s_http_userinfo;
      }
      break;

    case s_http_host_start:
      if (ch == '[') {
        return s_http_host_v6_start;
      }

      if (IS_HOST_CHAR(ch)) {
        return s_http_host;
      }

      break;

    case s_http_host:
      if (IS_HOST_CHAR(ch)) {
        return s_http_host;
      }

    /* fall through */
    case s_http_host_v6_end:
      if (ch == ':') {
        return s_http_host_port_start;
      }

      break;

    case s_http_host_v6:
      if (ch == ']') {
        return s_http_host_v6_end;
      }

    /* fall through */
    case s_http_host_v6_start:
      if (IS_HEX(ch) || ch == ':' || ch == '.') {
        return s_http_host_v6;
      }

      if (s == s_http_host_v6 && ch == '%') {
        return s_http_host_v6_zone_start;
      }
      break;

    case s_http_host_v6_zone:
      if (ch == ']') {
        return s_http_host_v6_end;
      }

    /* fall through */
    case s_http_host_v6_zone_start:
      /* RFC 6874 Zone ID consists of 1*( unreserved / pct-encoded) */
      if (IS_ALPHANUM(ch) || ch == '%' || ch == '.' || ch == '-' || ch == '_' ||
          ch == '~') {
        return s_http_host_v6_zone;
      }
      break;

    case s_http_host_port:
    case s_http_host_port_start:
      if (IS_NUM(ch)) {
        return s_http_host_port;
      }

      break;

    default:
      break;
  }
  return s_http_host_dead;
}


static int
http_parse_host(const char * buf, struct http_parser_url *u, int found_at) {
  enum http_host_state s;

  const char *p;
  size_t buflen = u->field_data[UF_HOST].off + u->field_data[UF_HOST].len;

  assert(u->field_set & (1 << UF_HOST));

  u->field_data[UF_HOST].len = 0;

  s = found_at ? s_http_userinfo_start : s_http_host_start;

  for (p = buf + u->field_data[UF_HOST].off; p < buf + buflen; p++) {
    enum http_host_state new_s = http_parse_host_char(s, *p);

    if (new_s == s_http_host_dead) {
      return 1;
    }

    switch(new_s) {
      case s_http_host:
        if (s != s_http_host) {
          u->field_data[UF_HOST].off = (uint16_t)(p - buf);
        }
        u->field_data[UF_HOST].len++;
        break;

      case s_http_host_v6:
        if (s != s_http_host_v6) {
          u->field_data[UF_HOST].off = (uint16_t)(p - buf);
        }
        u->field_data[UF_HOST].len++;
        break;

      case s_http_host_v6_zone_start:
      case s_http_host_v6_zone:
        u->field_data[UF_HOST].len++;
        break;

      case s_http_host_port:
        if (s != s_http_host_port) {
          u->field_data[UF_PORT].off = (uint16_t)(p - buf);
          u->field_data[UF_PORT].len = 0;
          u->field_set |= (1 << UF_PORT);
        }
        u->field_data[UF_PORT].len++;
        break;

      case s_http_userinfo:
        if (s != s_http_userinfo) {
          u->field_data[UF_USERINFO].off = (uint16_t)(p - buf);
          u->field_data[UF_USERINFO].len = 0;
          u->field_set |= (1 << UF_USERINFO);
        }
        u->field_data[UF_USERINFO].len++;
        break;

      default:
        break;
    }
    s = new_s;
  }

  /* Make sure we don't end somewhere unexpected */
  switch (s) {
    case s_http_host_start:
    case s_http_host_v6_start:
    case s_http_host_v6:
    case s_http_host_v6_zone_start:
    case s_http_host_v6_zone:
    case s_http_host_port_start:
    case s_http_userinfo:
    case s_http_userinfo_start:
      return 1;
    default:
      break;
  }

  return 0;
}


int
http_parser_parse_url(const char *buf, size_t buflen, int is_connect,
                      struct http_parser_url *u)
{
  enum state s;
  const char *p;
  enum http_parser_url_fields uf, old_uf;
  int found_at = 0;

  if (buflen == 0) {
    return 1;
  }

  u->port = u->field_set = 0;
  s = is_connect ? s_req_server_start : s_req_spaces_before_url;
  old_uf = UF_MAX;

  for (p = buf; p < buf + buflen; p++) {
    s = parse_url_char(s, *p);

    /* Figure out the next field that we're operating on */
    switch (s) {
      case s_dead:
        return 1;

      /* Skip delimeters */
      case s_req_schema_slash:
      case s_req_schema_slash_slash:
      case s_req_server_start:
      case s_req_query_string_start:
      case s_req_fragment_start:
        continue;

      case s_req_schema:
        uf = UF_SCHEMA;
        break;

      case s_req_server_with_at:
        found_at = 1;

      /* fall through */
      case s_req_server:
        uf = UF_HOST;
        break;

      case s_req_path:
        uf = UF_PATH;
        break;

      case s_req_query_string:
        uf = UF_QUERY;
        break;

      case s_req_fragment:
        uf = UF_FRAGMENT;
        break;

      default:
        assert(!"Unexpected state");
        return 1;
    }

    /* Nothing's changed; soldier on */
    if (uf == old_uf) {
      u->field_data[uf].len++;
      continue;
    }

    u->field_data[uf].off = (uint16_t)(p - buf);
    u->field_data[uf].len = 1;

    u->field_set |= (1 << uf);
    old_uf = uf;
  }

  /* host must be present if there is a schema */
  /* parsing http:///toto will fail */
  if ((u->field_set & (1 << UF_SCHEMA)) &&
      (u->field_set & (1 << UF_HOST)) == 0) {
    return 1;
  }

  if (u->field_set & (1 << UF_HOST)) {
    if (http_parse_host(buf, u, found_at) != 0) {
      return 1;
    }
  }

  /* CONNECT requests can only contain "hostname:port" */
  if (is_connect && u->field_set != ((1 << UF_HOST)|(1 << UF_PORT))) {
    return 1;
  }

  if (u->field_set & (1 << UF_PORT)) {
    uint16_t off;
    uint16_t len;
    const char* p;
    const char* end;
    unsigned long v;

    off = u->field_data[UF_PORT].off;
    len = u->field_data[UF_PORT].len;
    end = buf + off + len;

    /* NOTE: The characters are already validated and are in the [0-9] range */
    assert(off + len <= buflen && "Port number overflow");
    v = 0;
    for (p = buf + off; p < end; p++) {
      v *= 10;
      v += *p - '0';

      /* Ports have a max value of 2^16 */
      if (v > 0xffff) {
        return 1;
      }
    }

    u->port = (uint16_t) v;
  }

  return 0;
}

srs_error_t SrsHttpUri::initialize(std::string _url) {
    schema = host = path = query = "";

    url = _url;
    const char* purl = url.c_str();

    http_parser_url hp_u;
    int r0;
    if ((r0 = http_parser_parse_url(purl, url.length(), 0, &hp_u)) != 0){
        return srs_error_new(ERROR_HTTP_PARSE_URI, "parse url %s failed, code=%d", purl, r0);
    }

    std::string field = get_uri_field(url, &hp_u, UF_SCHEMA);
    if (!field.empty()){
        schema = field;
    }

    host = get_uri_field(url, &hp_u, UF_HOST);

    field = get_uri_field(url, &hp_u, UF_PORT);
    if (!field.empty()) {
        port = atoi(field.c_str());
    }
    if (port <= 0) {
        if (schema == "https") {
            port = SRS_DEFAULT_HTTPS_PORT;
        } else if (schema == "rtmp") {
            port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        } else if (schema == "redis") {
            port = SRS_DEFAULT_REDIS_PORT;
        } else {
            port = SRS_DEFAULT_HTTP_PORT;
        }
    }

    path = get_uri_field(url, &hp_u, UF_PATH);
    query = get_uri_field(url, &hp_u, UF_QUERY);

    username_ = get_uri_field(url, &hp_u, UF_USERINFO);
    size_t pos = username_.find(":");
    if (pos != string::npos) {
        password_ = username_.substr(pos+1);
        username_ = username_.substr(0, pos);
    }

    return parse_query();
}

void SrsHttpUri::set_schema(std::string v) {
    schema = v;

    // Update url with new schema.
    size_t pos = url.find("://");
    if (pos != string::npos) {
        url = schema + "://" + url.substr(pos + 3);
    }
}

string SrsHttpUri::get_url() {
    return url;
}

string SrsHttpUri::get_schema() {
    return schema;
}

string SrsHttpUri::get_host() {
    return host;
}

int SrsHttpUri::get_port() {
    return port;
}

string SrsHttpUri::get_path() {
    return path;
}

string SrsHttpUri::get_query() {
    return query;
}

string SrsHttpUri::get_query_by_key(std::string key) {
    std::map<string, string>::iterator it = query_values_.find(key);
    if(it == query_values_.end()) {
      return "";
    }
    return it->second;
}

std::string SrsHttpUri::username() {
    return username_;
}

std::string SrsHttpUri::password() {
    return password_;
}

string SrsHttpUri::get_uri_field(string uri, void* php_u, int ifield) {
	http_parser_url* hp_u = (http_parser_url*)php_u;
	http_parser_url_fields field = (http_parser_url_fields)ifield;

    if((hp_u->field_set & (1 << field)) == 0){
        return "";
    }

    int offset = hp_u->field_data[field].off;
    int len = hp_u->field_data[field].len;

    return uri.substr(offset, len);
}

srs_error_t SrsHttpUri::parse_query() {
    srs_error_t err = srs_success;
    if(query.empty()) {
        return err;
    }

    size_t begin = query.find("?");
    if(string::npos != begin) {
        begin++;
    } else {
        begin = 0;
    }
    string query_str = query.substr(begin);
    query_values_.clear();
    srs_parse_query_string(query_str, query_values_);

    return err;
}

// @see golang net/url/url.go
namespace {
  enum EncodeMode {
	  encodePath,
	  encodePathSegment,
	  encodeHost,
	  encodeZone,
	  encodeUserPassword,
	  encodeQueryComponent,
	  encodeFragment,
  };

  bool should_escape(uint8_t c, EncodeMode mode) {
    // §2.3 Unreserved characters (alphanum)
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
      return false;
    }

    if(encodeHost == mode || encodeZone == mode) {
      // §3.2.2 Host allows
      //	sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
      // as part of reg-name.
      // We add : because we include :port as part of host.
      // We add [ ] because we include [ipv6]:port as part of host.
      // We add < > because they're the only characters left that
      // we could possibly allow, and Parse will reject them if we
      // escape them (because hosts can't use %-encoding for
      // ASCII bytes).
      switch(c) {
        case '!':
        case '$':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case ';':
        case '=':
        case ':':
        case '[':
        case ']':
        case '<':
        case '>':
        case '"':
          return false;
      }
    }

    switch(c) {
    case '-':
    case '_':
    case '.':
    case '~': // §2.3 Unreserved characters (mark)
      return false; 
    case '$':
    case '&':
    case '+':
    case ',':
    case '/':
    case ':':
    case ';':
    case '=':
    case '?':
    case '@': // §2.2 Reserved characters (reserved)
      // Different sections of the URL allow a few of
      // the reserved characters to appear unescaped.
      switch (mode) {
      case encodePath: // §3.3
        // The RFC allows : @ & = + $ but saves / ; , for assigning
        // meaning to individual path segments. This package
        // only manipulates the path as a whole, so we allow those
        // last three as well. That leaves only ? to escape.
        return c == '?';

      case encodePathSegment: // §3.3
        // The RFC allows : @ & = + $ but saves / ; , for assigning
        // meaning to individual path segments.
        return c == '/' || c == ';' || c == ',' || c == '?';

      case encodeUserPassword: // §3.2.1
        // The RFC allows ';', ':', '&', '=', '+', '$', and ',' in
        // userinfo, so we must escape only '@', '/', and '?'.
        // The parsing of userinfo treats ':' as special so we must escape
        // that too.
        return c == '@' || c == '/' || c == '?' || c == ':';

      case encodeQueryComponent: // §3.4
        // The RFC reserves (so we must escape) everything.
        return true;

      case encodeFragment: // §4.1
        // The RFC text is silent but the grammar allows
        // everything, so escape nothing.
        return false;
      default:
        break;
      }
    }

    if(mode == encodeFragment) {
      // RFC 3986 §2.2 allows not escaping sub-delims. A subset of sub-delims are
      // included in reserved from RFC 2396 §2.2. The remaining sub-delims do not
      // need to be escaped. To minimize potential breakage, we apply two restrictions:
      // (1) we always escape sub-delims outside of the fragment, and (2) we always
      // escape single quote to avoid breaking callers that had previously assumed that
      // single quotes would be escaped. See issue #19917.
      switch (c) {
      case '!':
      case '(':
      case ')':
      case '*':
        return false;
      }
    }

    // Everything else must be escaped.
    return true;
  }

  bool ishex(uint8_t c) {
    if( '0' <= c && c <= '9') {
      return true;
    } else if('a' <= c && c <= 'f') {
      return true;
    } else if( 'A' <= c && c <= 'F') {
      return true;
    }
    return false;
  }

  uint8_t hex_to_num(uint8_t c) {
    if('0' <= c && c <= '9') {
      return c - '0';
    } else if('a' <= c && c <= 'f') {
      return c - 'a' + 10;
    } else if('A' <= c && c <= 'F') {
      return c - 'A' + 10;
    }
    return 0;
  }

  srs_error_t unescapse(string s, string& value, EncodeMode mode) {
    srs_error_t err = srs_success;
    int n = 0;
    bool has_plus = false;
    int i = 0;
    // Count %, check that they're well-formed.
    while(i < (int)s.length()) {
      switch (s.at(i)) {
      case '%':
        {
          n++;
          if((i+2) >= (int)s.length() || !ishex(s.at(i+1)) || !ishex(s.at(i+2))) {
              string msg = s.substr(i);
              if(msg.length() > 3) {
                msg = msg.substr(0, 3);
              }
              return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escape: %s", msg.c_str());
          }

          // Per https://tools.ietf.org/html/rfc3986#page-21
          // in the host component %-encoding can only be used
          // for non-ASCII bytes.
          // But https://tools.ietf.org/html/rfc6874#section-2
          // introduces %25 being allowed to escape a percent sign
          // in IPv6 scoped-address literals. Yay.
          if(encodeHost == mode && hex_to_num(s.at(i+1)) < 8 && s.substr(i, 3) != "%25") {
            return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escap: %s", s.substr(i, 3).c_str());
          }

          if(encodeZone == mode) {
            // RFC 6874 says basically "anything goes" for zone identifiers
            // and that even non-ASCII can be redundantly escaped,
            // but it seems prudent to restrict %-escaped bytes here to those
            // that are valid host name bytes in their unescaped form.
            // That is, you can use escaping in the zone identifier but not
            // to introduce bytes you couldn't just write directly.
            // But Windows puts spaces here! Yay.
            uint8_t v = (hex_to_num(s.at(i+1)) << 4) | (hex_to_num(s.at(i+2)));
            if("%25" != s.substr(i, 3) && ' ' != v && should_escape(v, encodeHost)) {
              return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escap: %s", s.substr(i, 3).c_str());
            }
          }
          i += 3;
        }
        break;
      case '+':
        has_plus = encodeQueryComponent == mode;
        i++;
      break;
      default:
        if((encodeHost == mode || encodeZone == mode) && ((uint8_t)s.at(i) < 0x80) && should_escape(s.at(i), mode)) {
          return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid character %u in host name", s.at(i));
        }
        i++;
        break;
      } 
    }

    if(0 == n && !has_plus) {
      value = s;
      return err;
    }

    value.clear();
    //value.resize(s.length() - 2*n);
    for(int i = 0; i < (int)s.length(); ++i) {
      switch(s.at(i)) {
      case '%':
        value += (hex_to_num(s.at(i+1))<<4 | hex_to_num(s.at(i+2)));
        i += 2;
        break;
      case '+':
        if(encodeQueryComponent == mode) {
          value += " ";
        } else {
          value += "+";
        }
        break;
      default:
        value += s.at(i);
        break;
      }
    }

    return srs_success;
  }

  string escape(string s, EncodeMode mode) {
    int space_count = 0;
    int hex_count = 0;
    for(int i = 0; i < (int)s.length(); ++i) {
      uint8_t c = s.at(i);
      if(should_escape(c, mode)) {
        if(' ' == c && encodeQueryComponent == mode) {
          space_count++;
        } else {
          hex_count++;
        }
      }
    }

    if(0 == space_count && 0 == hex_count) {
      return s;
    }

    string value;
    if(0 == hex_count) {
      value = s;
      for(int i = 0; i < (int)s.length(); ++i) {
        if(' ' == s.at(i)) {
          value[i] = '+';
        }
      }
      return value;
    }

    //value.resize(s.length() + 2*hex_count);
    const char escape_code[] = "0123456789ABCDEF";
    //int j = 0;
    for(int i = 0; i < (int)s.length(); ++i) {
      uint8_t c = s.at(i);
      if(' ' == c && encodeQueryComponent == mode) {
        value += '+';
      } else if (should_escape(c, mode)) {
        value += '%';
        value += escape_code[c>>4];
        value += escape_code[c&15];
        //j += 3;
      } else {
        value += s[i];
        
      }
    }

    return value;
  }

}

string SrsHttpUri::query_escape(std::string s)
{
  return escape(s, encodeQueryComponent);
}

string SrsHttpUri::path_escape(std::string s)
{
  return escape(s, encodePathSegment);
}

srs_error_t SrsHttpUri::query_unescape(std::string s, std::string& value)
{
  return unescapse(s, value, encodeQueryComponent);
}

srs_error_t SrsHttpUri::path_unescape(std::string s, std::string& value)
{
  return unescapse(s, value, encodePathSegment);
}

///////////////////////////////
//SrsHttpHeader
void SrsHttpHeader::set(const string& key, const string& value) {
  // Convert to UpperCamelCase, for example:
  //      transfer-encoding
  // transform to:
  //      Transfer-Encoding
  char pchar = 0;
  for (int i = 0; i < (int)key.length(); i++) {
      char ch = key.at(i);

      if (i == 0 || pchar == '-') {
          if (ch >= 'a' && ch <= 'z') {
              ((char*)key.data())[i] = ch - 32;
          }
      }
      pchar = ch;
  }

  headers[key] = value;
}

string SrsHttpHeader::get(const string& key) const {
  std::string v;

  auto it = headers.find(key);
  if (it != headers.end()) {
      v = it->second;
  }
  
  return v;
}

void SrsHttpHeader::del(const string& key) {
  map<string, string>::iterator it = headers.find(key);
  if (it != headers.end()) {
    headers.erase(it);
  }
}

int SrsHttpHeader::count() {
   return (int)headers.size();
}

int64_t SrsHttpHeader::content_length() {
  std::string cl = get("Content-Length");
  
  if (cl.empty()) {
      return -1;
  }
  
  return (int64_t)::atof(cl.c_str());
}

void SrsHttpHeader::set_content_length(int64_t size) {
  set("Content-Length", srs_int2str(size));
}

std::string SrsHttpHeader::content_type() {
  return get("Content-Type");
}

void SrsHttpHeader::set_content_type(const std::string& ct) {
  set("Content-Type", ct);
}

void SrsHttpHeader::write(std::stringstream& ss) {
  std::map<std::string, std::string>::iterator it;
  for (it = headers.begin(); it != headers.end(); ++it) {
      ss << it->first << ": " << it->second << SRS_HTTP_CRLF;
  }
}

const std::map<std::string, std::string>&SrsHttpHeader::header() {
  return headers;
}

// get the status text of code.
std::string_view generate_http_status_text(int status) {
  static std::string UNKNOW_STATUS = "Status Unknown";
  static std::map<int, std::string> _status_map;
  if (_status_map.empty()) {
    _status_map[SRS_CONSTS_HTTP_Continue] = SRS_CONSTS_HTTP_Continue_str;
    _status_map[SRS_CONSTS_HTTP_SwitchingProtocols] = SRS_CONSTS_HTTP_SwitchingProtocols_str;
    _status_map[SRS_CONSTS_HTTP_OK] = SRS_CONSTS_HTTP_OK_str;
    _status_map[SRS_CONSTS_HTTP_Created] = SRS_CONSTS_HTTP_Created_str;
    _status_map[SRS_CONSTS_HTTP_Accepted] = SRS_CONSTS_HTTP_Accepted_str;
    _status_map[SRS_CONSTS_HTTP_NonAuthoritativeInformation] = SRS_CONSTS_HTTP_NonAuthoritativeInformation_str;
    _status_map[SRS_CONSTS_HTTP_NoContent] = SRS_CONSTS_HTTP_NoContent_str;
    _status_map[SRS_CONSTS_HTTP_ResetContent] = SRS_CONSTS_HTTP_ResetContent_str;
    _status_map[SRS_CONSTS_HTTP_PartialContent] = SRS_CONSTS_HTTP_PartialContent_str;
    _status_map[SRS_CONSTS_HTTP_MultipleChoices] = SRS_CONSTS_HTTP_MultipleChoices_str;
    _status_map[SRS_CONSTS_HTTP_MovedPermanently] = SRS_CONSTS_HTTP_MovedPermanently_str;
    _status_map[SRS_CONSTS_HTTP_Found] = SRS_CONSTS_HTTP_Found_str;
    _status_map[SRS_CONSTS_HTTP_SeeOther] = SRS_CONSTS_HTTP_SeeOther_str;
    _status_map[SRS_CONSTS_HTTP_NotModified] = SRS_CONSTS_HTTP_NotModified_str;
    _status_map[SRS_CONSTS_HTTP_UseProxy] = SRS_CONSTS_HTTP_UseProxy_str;
    _status_map[SRS_CONSTS_HTTP_TemporaryRedirect] = SRS_CONSTS_HTTP_TemporaryRedirect_str;
    _status_map[SRS_CONSTS_HTTP_BadRequest] = SRS_CONSTS_HTTP_BadRequest_str;
    _status_map[SRS_CONSTS_HTTP_Unauthorized] = SRS_CONSTS_HTTP_Unauthorized_str;
    _status_map[SRS_CONSTS_HTTP_PaymentRequired] = SRS_CONSTS_HTTP_PaymentRequired_str;
    _status_map[SRS_CONSTS_HTTP_Forbidden] = SRS_CONSTS_HTTP_Forbidden_str;
    _status_map[SRS_CONSTS_HTTP_NotFound] = SRS_CONSTS_HTTP_NotFound_str;
    _status_map[SRS_CONSTS_HTTP_MethodNotAllowed] = SRS_CONSTS_HTTP_MethodNotAllowed_str;
    _status_map[SRS_CONSTS_HTTP_NotAcceptable] = SRS_CONSTS_HTTP_NotAcceptable_str;
    _status_map[SRS_CONSTS_HTTP_ProxyAuthenticationRequired] = SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str;
    _status_map[SRS_CONSTS_HTTP_RequestTimeout] = SRS_CONSTS_HTTP_RequestTimeout_str;
    _status_map[SRS_CONSTS_HTTP_Conflict] = SRS_CONSTS_HTTP_Conflict_str;
    _status_map[SRS_CONSTS_HTTP_Gone] = SRS_CONSTS_HTTP_Gone_str;
    _status_map[SRS_CONSTS_HTTP_LengthRequired] = SRS_CONSTS_HTTP_LengthRequired_str;
    _status_map[SRS_CONSTS_HTTP_PreconditionFailed] = SRS_CONSTS_HTTP_PreconditionFailed_str;
    _status_map[SRS_CONSTS_HTTP_RequestEntityTooLarge] = SRS_CONSTS_HTTP_RequestEntityTooLarge_str;
    _status_map[SRS_CONSTS_HTTP_RequestURITooLarge] = SRS_CONSTS_HTTP_RequestURITooLarge_str;
    _status_map[SRS_CONSTS_HTTP_UnsupportedMediaType] = SRS_CONSTS_HTTP_UnsupportedMediaType_str;
    _status_map[SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable] = SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str;
    _status_map[SRS_CONSTS_HTTP_ExpectationFailed] = SRS_CONSTS_HTTP_ExpectationFailed_str;
    _status_map[SRS_CONSTS_HTTP_InternalServerError] = SRS_CONSTS_HTTP_InternalServerError_str;
    _status_map[SRS_CONSTS_HTTP_NotImplemented] = SRS_CONSTS_HTTP_NotImplemented_str;
    _status_map[SRS_CONSTS_HTTP_BadGateway] = SRS_CONSTS_HTTP_BadGateway_str;
    _status_map[SRS_CONSTS_HTTP_ServiceUnavailable] = SRS_CONSTS_HTTP_ServiceUnavailable_str;
    _status_map[SRS_CONSTS_HTTP_GatewayTimeout] = SRS_CONSTS_HTTP_GatewayTimeout_str;
    _status_map[SRS_CONSTS_HTTP_HTTPVersionNotSupported] = SRS_CONSTS_HTTP_HTTPVersionNotSupported_str;
  }
  
  std::string_view status_text;
  if (_status_map.find(status) == _status_map.end()) {
      status_text = UNKNOW_STATUS;
  } else {
      status_text = _status_map[status];
  }
  
  return status_text;
}

HttpMessage::HttpMessage(std::string_view body)
  : status_{SRS_CONSTS_HTTP_OK},
    body_{body.data(), body.length()},
    uri_{std::make_unique<SrsHttpUri>()}
{
}

void HttpMessage::set_basic(
  uint8_t type, uint8_t method, uint16_t status, int64_t content_length)
{
  type_ = type;
  method_ = method;
  status_ = status;
  if (content_length_ == -1) {
    content_length_ = content_length;
  }
}

void HttpMessage::set_header(const SrsHttpHeader& header, bool keep_alive)
{
  header_ = header;
  keep_alive_ = keep_alive;

  // whether chunked.
  chunked_ = (header.get("Transfer-Encoding") == "chunked");

  // Update the content-length in header.
  std::string clv = header.get("Content-Length");
  if (!clv.empty()) {
    content_length_ = ::atoll(clv.c_str());
  }
}

std::shared_ptr<IMediaConnection> HttpMessage::connection() {
  return owner_;
}

void HttpMessage::connection(std::shared_ptr<IMediaConnection> conn) {
  owner_ = conn;
}

bool srs_string_contains(const std::string& str, const std::string& flag) {
  return str.find(flag) != std::string::npos;
}

static std::string _public_internet_address;

// Get local ip, fill to @param ips
struct SrsIPAddress
{
  // The network interface name, such as eth0, en0, eth1.
  std::string ifname;
  // The IP v4 or v6 address.
  std::string ip;
  // Whether the ip is IPv4 address.
  bool is_ipv4;
  // Whether the ip is internet public IP address.
  bool is_internet;
  // Whether the ip is loopback, such as 127.0.0.1
  bool is_loopback;
};


// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> _srs_device_ifs;

bool srs_net_device_is_internet(const std::string& ifname)
{ 
  if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
      return false;
  }
  return _srs_device_ifs[ifname];
}

bool srs_net_device_is_internet(const sockaddr* addr)
{
  if(addr->sa_family == AF_INET) {
    const in_addr inaddr = ((sockaddr_in*)addr)->sin_addr;
    const uint32_t addr_h = ntohl(inaddr.s_addr);

    // lo, 127.0.0.0-127.0.0.1
    if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
        return false;
    }

    // Class A 10.0.0.0-10.255.255.255
    if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
        return false;
    }

    // Class B 172.16.0.0-172.31.255.255
    if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
        return false;
    }

    // Class C 192.168.0.0-192.168.255.255
    if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
        return false;
    }
  } else if(addr->sa_family == AF_INET6) {
    const sockaddr_in6* a6 = (const sockaddr_in6*)addr;

    // IPv6 loopback is ::1
    if (IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr)) {
        return false;
    }

    // IPv6 unspecified is ::
    if (IN6_IS_ADDR_UNSPECIFIED(&a6->sin6_addr)) {
        return false;
    }

    // From IPv4, you might know APIPA (Automatic Private IP Addressing) or AutoNet.
    // Whenever automatic IP configuration through DHCP fails.
    // The prefix of a site-local address is FE80::/10.
    if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr)) {
        return false;
    }

    // Site-local addresses are equivalent to private IP addresses in IPv4.
    // The prefix of a site-local address is FEC0::/10.
    // https://4sysops.com/archives/ipv6-tutorial-part-6-site-local-addresses-and-link-local-addresses/
    if (IN6_IS_ADDR_SITELOCAL(&a6->sin6_addr)) {
       return false;
    }

    // Others.
    if (IN6_IS_ADDR_MULTICAST(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_NODELOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_LINKLOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_SITELOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_ORGLOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_GLOBAL(&a6->sin6_addr)) {
        return false;
    }
  }
  
  return true;
}

void discover_network_iface(ifaddrs* cur, 
                            std::vector<SrsIPAddress*>& ips, 
                            std::stringstream& ss0, 
                            std::stringstream& ss1, 
                            bool ipv6,
                            bool loopback)
{
    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo(cur->ifa_addr, sizeof(sockaddr_storage), h, nbh, NULL, 0, NI_NUMERICHOST);
    if(r0) {
        MLOG_WARN("convert local ip failed: " << gai_strerror(r0));
        return;
    }
    
    std::string ip(saddr, strlen(saddr));
    ss0 << ", iface[" << (int)ips.size() << "] " << cur->ifa_name << " " << (ipv6? "ipv6":"ipv4")
    << " 0x" << std::hex << cur->ifa_flags  << std::dec << " " << ip;

    SrsIPAddress* ip_address = new SrsIPAddress();
    ip_address->ip = ip;
    ip_address->is_ipv4 = !ipv6;
    ip_address->is_loopback = loopback;
    ip_address->ifname = cur->ifa_name;
    ip_address->is_internet = srs_net_device_is_internet(cur->ifa_addr);
    ips.push_back(ip_address);
    
    // set the device internet status.
    if (!ip_address->is_internet) {
        ss1 << ", intranet ";
        _srs_device_ifs[cur->ifa_name] = false;
    } else {
        ss1 << ", internet ";
        _srs_device_ifs[cur->ifa_name] = true;
    }
    ss1 << cur->ifa_name << " " << ip;
}

static std::vector<SrsIPAddress*> _srs_system_ips;

void retrieve_local_ips() {
  std::vector<SrsIPAddress*>& ips = _srs_system_ips;

  // Release previous IPs.
  for (int i = 0; i < (int)ips.size(); i++) {
      SrsIPAddress* ip = ips[i];
      srs_freep(ip);
  }
  ips.clear();

  // Get the addresses.
  ifaddrs* ifap;
  if (getifaddrs(&ifap) == -1) {
      MLOG_WARN("retrieve local ips, getifaddrs failed.");
      return;
  }
  
  std::stringstream ss0;
  ss0 << "ips";
  
  std::stringstream ss1;
  ss1 << "devices";
  
  // Discover IPv4 first.
  for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
      ifaddrs* cur = p;
      
      // Ignore if no address for this interface.
      // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
      if (!cur->ifa_addr) {
          continue;
      }
      
      // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
      // @see: https://github.com/ossrs/srs/issues/141
      bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
      bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
      // Ignore IFF_PROMISC(Interface is in promiscuous mode), which may be set by Wireshark.
      bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_LOOPBACK) || (cur->ifa_flags & IFF_POINTOPOINT);
      bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
      if (ipv4 && ready && !ignored) {
          discover_network_iface(cur, ips, ss0, ss1, false, loopback);
      }
  }
  
  // Then, discover IPv6 addresses.
  for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
      ifaddrs* cur = p;
      
      // Ignore if no address for this interface.
      // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
      if (!cur->ifa_addr) {
          continue;
      }
      
      // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
      // @see: https://github.com/ossrs/srs/issues/141
      bool ipv6 = (cur->ifa_addr->sa_family == AF_INET6);
      bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
      bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC) || (cur->ifa_flags & IFF_LOOPBACK);
      bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
      if (ipv6 && ready && !ignored) {
          discover_network_iface(cur, ips, ss0, ss1, true, loopback);
      }
  }
  
  // If empty, disover IPv4 loopback.
  if (ips.empty()) {
      for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
          ifaddrs* cur = p;
          
          // Ignore if no address for this interface.
          // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
          if (!cur->ifa_addr) {
              continue;
          }
          
          // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
          // @see: https://github.com/ossrs/srs/issues/141
          bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
          bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
          bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC);
          bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
          if (ipv4 && ready && !ignored) {
              discover_network_iface(cur, ips, ss0, ss1, false, loopback);
          }
      }
  }
  
  MLOG_INFO(ss0.str().c_str() << "," << ss1.str().c_str());
  
  freeifaddrs(ifap);
}

std::vector<SrsIPAddress*>& srs_get_local_ips() {
    if (_srs_system_ips.empty()) {
        retrieve_local_ips();
    }
    
    return _srs_system_ips;
}

std::string srs_get_public_internet_address(bool ipv4_only) {
  if (!_public_internet_address.empty()) {
      return _public_internet_address;
  }
  
  std::vector<SrsIPAddress*>& ips = srs_get_local_ips();
  
  // find the best match public address.
  for (int i = 0; i < (int)ips.size(); i++) {
      SrsIPAddress* ip = ips[i];
      if (!ip->is_internet) {
          continue;
      }
      if (ipv4_only && !ip->is_ipv4) {
          continue;
      }

      MLOG_WARN("use public address as ip:"<< ip->ip.c_str() <<
        ", ifname=" << ip->ifname.c_str());
      _public_internet_address = ip->ip;
      return ip->ip;
  }
  
  // no public address, use private address.
  for (int i = 0; i < (int)ips.size(); i++) {
      SrsIPAddress* ip = ips[i];
      if (ip->is_loopback) {
          continue;
      }
      if (ipv4_only && !ip->is_ipv4) {
          continue;
      }

      MLOG_WARN("use private address as ip: "<< ip->ip.c_str() <<
        ", ifname:" << ip->ifname.c_str());
      _public_internet_address = ip->ip;
      return ip->ip;
  }
  
  // Finally, use first whatever kind of address.
  if (!ips.empty() && _public_internet_address.empty()) {
      SrsIPAddress* ip = ips[0];

      MLOG_WARN("use first address as ip:"<< ip->ip.c_str() << 
        ", ifname=" << ip->ifname.c_str());
      _public_internet_address = ip->ip;
      return ip->ip;
  }
  
  return "";
}

srs_error_t HttpMessage::set_url(const std::string& url, bool allow_jsonp) {
  srs_error_t err = srs_success;
  
  url_ = url;

  // parse uri from schema/server:port/path?query
  std::string uri = url_;

  if (!srs_string_contains(uri, "://")) {
    // use server public ip when host not specified.
    // to make telnet happy.
    std::string host = header_.get("Host");

    // If no host in header, we use local discovered IP, IPv4 first.
    if (host.empty()) {
      host = srs_get_public_internet_address(true);
    }

    if (!host.empty()) {
      uri = "http://" + host + url_;
    }
  }

  if ((err = uri_->initialize(uri)) != srs_success) {
    return srs_error_wrap(err, "init uri %s", uri.c_str());
  }
  
  // parse ext.
  ext_ = srs_path_filext(uri_->get_path());
  
  // parse query string.
  srs_parse_query_string(uri_->get_query(), _query);
  
  // parse jsonp request message.
  if (allow_jsonp) {
    if (!query_get("callback").empty()) {
      jsonp = true;
    }
    if (jsonp) {
      jsonp_method_ = query_get("method");
    }
  }
  
  return err;
}

void HttpMessage::set_https(bool v) {
  schema_ = v? "https" : "http";
  uri_->set_schema(schema_);
}

const std::string& HttpMessage::schema() {
  return schema_;
}

std::string HttpMessage::method() {
  if (jsonp && !jsonp_method_.empty()) {
    return jsonp_method_;
  }
  
  if (is_http_get()) {
    return "GET";
  }
  if (is_http_put()) {
    return "PUT";
  }
  if (is_http_post()) {
    return "POST";
  }
  if (is_http_delete()) {
    return "DELETE";
  }
  if (is_http_options()) {
    return "OPTIONS";
  }
  
  return "OTHER";
}

uint16_t HttpMessage::status_code() {
  return status_;
}

// For http parser macros
#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

uint8_t HttpMessage::method_i() {
  if (jsonp && !jsonp_method_.empty()) {
    if (jsonp_method_ == "GET") {
      return SRS_CONSTS_HTTP_GET;
    } else if (jsonp_method_ == "PUT") {
      return SRS_CONSTS_HTTP_PUT;
    } else if (jsonp_method_ == "POST") {
      return SRS_CONSTS_HTTP_POST;
    } else if (jsonp_method_ == "DELETE") {
      return SRS_CONSTS_HTTP_DELETE;
    }
  }

  return method_;
}

bool HttpMessage::is_http_get() {
  return method_i() == SRS_CONSTS_HTTP_GET;
}

bool HttpMessage::is_http_put() {
  return method_i() == SRS_CONSTS_HTTP_PUT;
}

bool HttpMessage::is_http_post() {
  return method_i() == SRS_CONSTS_HTTP_POST;
}

bool HttpMessage::is_http_delete() {
  return method_i() == SRS_CONSTS_HTTP_DELETE;
}

bool HttpMessage::is_http_options() {
  return method_i() == SRS_CONSTS_HTTP_OPTIONS;
}

bool HttpMessage::is_chunked() {
  return chunked_;
}

bool HttpMessage::is_keep_alive() {
  return keep_alive_;
}

std::string HttpMessage::uri() {
  std::string uri = uri_->get_schema();
  if (uri.empty()) {
      uri += "http";
  }
  uri += "://";
  
  uri += host();
  uri += path();
  
  return uri;
}

std::string HttpMessage::url() {
  return uri_->get_url();
}

std::string HttpMessage::host() {
  auto it = _query.find("vhost");
  if (it != _query.end() && !it->second.empty()) {
      return it->second;
  }

  it = _query.find("domain");
  if (it != _query.end() && !it->second.empty()) {
      return it->second;
  }

  return uri_->get_host();
}

int HttpMessage::port() {
  return uri_->get_port();
}

std::string HttpMessage::path() {
  return uri_->get_path();
}

std::string HttpMessage::query() {
  return uri_->get_query();
}

std::string HttpMessage::ext() {
  return ext_;
}

std::string HttpMessage::parse_rest_id(std::string pattern) {
  std::string p = uri_->get_path();
  if (p.length() <= pattern.length()) {
      return "";
  }
  
  std::string id = p.substr((int)pattern.length());
  if (!id.empty()) {
      return id;
  }
  
  return "";
}

bool HttpMessage::is_jsonp(){
  return jsonp;
}

std::string HttpMessage::query_get(const std::string& key) {
  std::string v;
  
  if (_query.find(key) != _query.end()) {
    v = _query[key];
  }
  
  return v;
}

SrsHttpHeader& HttpMessage::header() {
  return header_;
}

const std::string& HttpMessage::get_body() {
  return body_;
}

int64_t HttpMessage::content_length() {
  return content_length_;
}

void HttpMessage::on_body(std::string_view data) {
  body_.append(data.data(), data.length());

  if (content_length_ != -1) {
    if (body_.length() == (size_t)content_length_) {
      set_body_eof();
    }
  } else {
    //chuncked
    SignalOnBody_(body_);
    body_.clear();
  }
}

std::shared_ptr<MediaRequest> 
HttpMessage::to_request(const std::string& vhost) {
  auto req = std::make_shared<MediaRequest>();
  
  // http path, for instance, /live/livestream.flv, parse to
  //      app: /live
  //      stream: livestream.flv
  srs_parse_rtmp_url(uri_->get_path(), req->app, req->stream);
  
  // trim the start slash, for instance, /live to live
  req->app = srs_string_trim_start(req->app, "/");
  
  // remove the extension, for instance, livestream.flv to livestream
  req->stream = srs_path_filename(req->stream);
  
  // generate others.
  req->tcUrl = "rtmp://" + vhost + "/" + req->app;
  req->pageUrl = header_.get("Referer");
  req->objectEncoding = 0;

  std::string query = uri_->get_query();
  if (!query.empty()) {
    req->param = "?" + query;
  }
  
  srs_discovery_tc_url(req->tcUrl, 
                       req->schema, 
                       req->host, 
                       req->vhost, 
                       req->app, 
                       req->stream, 
                       req->port, 
                       req->param);
  req->strip();
  
  // reset the host to http request host.
  if (req->host == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
    req->host = uri_->get_host();
  }

  // Set ip by remote ip of connection.
  if (owner_) {
    req->ip = owner_->Ip();
  }

  // Overwrite by ip from proxy.
  //string oip = srs_get_original_ip(this);
  //if (!oip.empty()) {
  //    req->ip = oip;
  //}
  
  return std::move(req);
}


static constexpr uint32_t max_header_size = HTTP_MAX_HEADER_SIZE;

#ifndef ULLONG_MAX
# define ULLONG_MAX ((uint64_t) -1) /* 2^64-1 */
#endif

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef BIT_AT
# define BIT_AT(a, i)                                                \
  (!!((unsigned int) (a)[(unsigned int) (i) >> 3] &                  \
   (1 << ((unsigned int) (i) & 7))))
#endif

#ifndef ELEM_AT
# define ELEM_AT(a, i, v) ((unsigned int) (i) < ARRAY_SIZE(a) ? (a)[(i)] : (v))
#endif


/* Status Codes */
#define HTTP_STATUS_MAP(XX)                                                 \
  XX(100, CONTINUE,                        Continue)                        \
  XX(101, SWITCHING_PROTOCOLS,             Switching Protocols)             \
  XX(102, PROCESSING,                      Processing)                      \
  XX(200, OK,                              OK)                              \
  XX(201, CREATED,                         Created)                         \
  XX(202, ACCEPTED,                        Accepted)                        \
  XX(203, NON_AUTHORITATIVE_INFORMATION,   Non-Authoritative Information)   \
  XX(204, NO_CONTENT,                      No Content)                      \
  XX(205, RESET_CONTENT,                   Reset Content)                   \
  XX(206, PARTIAL_CONTENT,                 Partial Content)                 \
  XX(207, MULTI_STATUS,                    Multi-Status)                    \
  XX(208, ALREADY_REPORTED,                Already Reported)                \
  XX(226, IM_USED,                         IM Used)                         \
  XX(300, MULTIPLE_CHOICES,                Multiple Choices)                \
  XX(301, MOVED_PERMANENTLY,               Moved Permanently)               \
  XX(302, FOUND,                           Found)                           \
  XX(303, SEE_OTHER,                       See Other)                       \
  XX(304, NOT_MODIFIED,                    Not Modified)                    \
  XX(305, USE_PROXY,                       Use Proxy)                       \
  XX(307, TEMPORARY_REDIRECT,              Temporary Redirect)              \
  XX(308, PERMANENT_REDIRECT,              Permanent Redirect)              \
  XX(400, BAD_REQUEST,                     Bad Request)                     \
  XX(401, UNAUTHORIZED,                    Unauthorized)                    \
  XX(402, PAYMENT_REQUIRED,                Payment Required)                \
  XX(403, FORBIDDEN,                       Forbidden)                       \
  XX(404, NOT_FOUND,                       Not Found)                       \
  XX(405, METHOD_NOT_ALLOWED,              Method Not Allowed)              \
  XX(406, NOT_ACCEPTABLE,                  Not Acceptable)                  \
  XX(407, PROXY_AUTHENTICATION_REQUIRED,   Proxy Authentication Required)   \
  XX(408, REQUEST_TIMEOUT,                 Request Timeout)                 \
  XX(409, CONFLICT,                        Conflict)                        \
  XX(410, GONE,                            Gone)                            \
  XX(411, LENGTH_REQUIRED,                 Length Required)                 \
  XX(412, PRECONDITION_FAILED,             Precondition Failed)             \
  XX(413, PAYLOAD_TOO_LARGE,               Payload Too Large)               \
  XX(414, URI_TOO_LONG,                    URI Too Long)                    \
  XX(415, UNSUPPORTED_MEDIA_TYPE,          Unsupported Media Type)          \
  XX(416, RANGE_NOT_SATISFIABLE,           Range Not Satisfiable)           \
  XX(417, EXPECTATION_FAILED,              Expectation Failed)              \
  XX(421, MISDIRECTED_REQUEST,             Misdirected Request)             \
  XX(422, UNPROCESSABLE_ENTITY,            Unprocessable Entity)            \
  XX(423, LOCKED,                          Locked)                          \
  XX(424, FAILED_DEPENDENCY,               Failed Dependency)               \
  XX(426, UPGRADE_REQUIRED,                Upgrade Required)                \
  XX(428, PRECONDITION_REQUIRED,           Precondition Required)           \
  XX(429, TOO_MANY_REQUESTS,               Too Many Requests)               \
  XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, Request Header Fields Too Large) \
  XX(451, UNAVAILABLE_FOR_LEGAL_REASONS,   Unavailable For Legal Reasons)   \
  XX(500, INTERNAL_SERVER_ERROR,           Internal Server Error)           \
  XX(501, NOT_IMPLEMENTED,                 Not Implemented)                 \
  XX(502, BAD_GATEWAY,                     Bad Gateway)                     \
  XX(503, SERVICE_UNAVAILABLE,             Service Unavailable)             \
  XX(504, GATEWAY_TIMEOUT,                 Gateway Timeout)                 \
  XX(505, HTTP_VERSION_NOT_SUPPORTED,      HTTP Version Not Supported)      \
  XX(506, VARIANT_ALSO_NEGOTIATES,         Variant Also Negotiates)         \
  XX(507, INSUFFICIENT_STORAGE,            Insufficient Storage)            \
  XX(508, LOOP_DETECTED,                   Loop Detected)                   \
  XX(510, NOT_EXTENDED,                    Not Extended)                    \
  XX(511, NETWORK_AUTHENTICATION_REQUIRED, Network Authentication Required) \

enum http_status {
#define XX(num, name, string) HTTP_STATUS_##name = num,
  HTTP_STATUS_MAP(XX)
#undef XX
};


/* Flag values for http_parser.flags field */
enum /*flags*/ { 
  F_CHUNKED               = 1 << 0, 
  F_CONNECTION_KEEP_ALIVE = 1 << 1, 
  F_CONNECTION_CLOSE      = 1 << 2, 
  F_CONNECTION_UPGRADE    = 1 << 3,
  F_TRAILING              = 1 << 4,
  F_UPGRADE               = 1 << 5,
  F_SKIPBODY              = 1 << 6,
  F_CONTENTLENGTH         = 1 << 7,
};

/* Map errno values to strings for human-readable output */
#define HTTP_STRERROR_GEN(n, s) { "HPE_" #n, s },
static struct {
  const char *name;
  const char *description;
} http_strerror_tab[] = {
  HTTP_ERRNO_MAP(HTTP_STRERROR_GEN)
};
#undef HTTP_STRERROR_GEN

const char* http_errno_name(enum http_errno err) {
  assert(((size_t) err) < ARRAY_SIZE(http_strerror_tab));
  return http_strerror_tab[err].name;
}

const char* http_errno_description(enum http_errno err) {
  assert(((size_t) err) < ARRAY_SIZE(http_strerror_tab));
  return http_strerror_tab[err].description;
}

#define PARSING_HEADER(state) (state <= s_headers_done)

enum header_states { 
  h_general = 0, 
  h_C,
  h_CO,
  h_CON,

  h_matching_connection,
  h_matching_proxy_connection,
  h_matching_content_length,
  h_matching_transfer_encoding,
  h_matching_upgrade,

  h_connection,
  h_content_length,
  h_content_length_num,
  h_content_length_ws,
  h_transfer_encoding,
  h_upgrade,

  h_matching_transfer_encoding_chunked,
  h_matching_connection_token_start,
  h_matching_connection_keep_alive,
  h_matching_connection_close,
  h_matching_connection_upgrade,
  h_matching_connection_token,

  h_transfer_encoding_chunked,
  h_connection_keep_alive,
  h_connection_close,
  h_connection_upgrade,
};


//helper macro definition for http_parser_execute
#define SET_ERRNO(e)                                                 \
  do {                                                                 \
    parser->nread = nread;                                             \
    parser->http_errno = (e);                                          \
  } while(0)
  
#define CURRENT_STATE() p_state
#define UPDATE_STATE(V) p_state = (enum state) (V);

#define RETURN(V)                                                    \
do {                                                                 \
  parser->nread = nread;                                             \
  parser->state = CURRENT_STATE();                                   \
  return (V);                                                        \
} while (0);

#define REEXECUTE()                                                  \
  goto reexecute; 


/* Run the notify callback FOR, returning ER if it fails */
#define CALLBACK_NOTIFY_(FOR, ER)                                    \
  do {                                                                 \
    assert(HTTP_PARSER_ERRNO(parser) == HPE_OK);                       \
                                                                       \
    if (LIKELY(settings->on_##FOR)) {                                  \
      parser->state = CURRENT_STATE();                                 \
      if (UNLIKELY(0 != settings->on_##FOR(parser))) {                 \
        SET_ERRNO(HPE_CB_##FOR);                                       \
      }                                                                \
      UPDATE_STATE(parser->state);                                     \
                                                                       \
      /* We either errored above or got paused; get out */             \
      if (UNLIKELY(HTTP_PARSER_ERRNO(parser) != HPE_OK)) {             \
        return (ER);                                                   \
      }                                                                \
    }                                                                  \
  } while (0)
  
  
/* Run the notify callback FOR and consume the current byte */
#define CALLBACK_NOTIFY(FOR)            CALLBACK_NOTIFY_(FOR, p - data + 1)
  
/* Run the notify callback FOR and don't consume the current byte */
#define CALLBACK_NOTIFY_NOADVANCE(FOR)  CALLBACK_NOTIFY_(FOR, p - data)

/* Run data callback FOR with LEN bytes, returning ER if it fails */
#define CALLBACK_DATA_(FOR, LEN, ER)                                 \
do {                                                                 \
  assert(HTTP_PARSER_ERRNO(parser) == HPE_OK);                       \
                                                                     \
  if (FOR##_mark) {                                                  \
    if (LIKELY(settings->on_##FOR)) {                                \
      parser->state = CURRENT_STATE();                               \
      if (UNLIKELY(0 !=                                              \
                   settings->on_##FOR(parser, FOR##_mark, (LEN)))) { \
        SET_ERRNO(HPE_CB_##FOR);                                     \
      }                                                              \
      UPDATE_STATE(parser->state);                                   \
                                                                     \
      /* We either errored above or got paused; get out */           \
      if (UNLIKELY(HTTP_PARSER_ERRNO(parser) != HPE_OK)) {           \
        return (ER);                                                 \
      }                                                              \
    }                                                                \
    FOR##_mark = NULL;                                               \
  }                                                                  \
} while (0)

/* Run the data callback FOR and consume the current byte */
#define CALLBACK_DATA(FOR)                                           \
    CALLBACK_DATA_(FOR, p - FOR##_mark, p - data + 1)

/* Run the data callback FOR and don't consume the current byte */
#define CALLBACK_DATA_NOADVANCE(FOR)                                 \
    CALLBACK_DATA_(FOR, p - FOR##_mark, p - data)

/* Set the mark FOR; non-destructive if mark is already set */
#define MARK(FOR)                                                    \
do {                                                                 \
  if (!FOR##_mark) {                                                 \
    FOR##_mark = p;                                                  \
  }                                                                  \
} while (0)


/* Don't allow the total size of the HTTP headers (including the status
 * line) to exceed max_header_size.  This check is here to protect
 * embedders against denial-of-service attacks where the attacker feeds
 * us a never-ending header that the embedder keeps buffering.
 *
 * This check is arguably the responsibility of embedders but we're doing
 * it on the embedder's behalf because most won't bother and this way we
 * make the web a little safer.  max_header_size is still far bigger
 * than any reasonable request or response so this should never affect
 * day-to-day operation.
 */
#define COUNT_HEADER_SIZE(V)                                         \
do {                                                                 \
  nread += (uint32_t)(V);                                            \
  if (UNLIKELY(nread > max_header_size)) {                           \
    SET_ERRNO(HPE_HEADER_OVERFLOW);                                  \
    goto error;                                                      \
  }                                                                  \
} while (0)

/**
 * Verify that a char is a valid visible (printable) US-ASCII
 * character or %x80-FF
 **/

#define IS_HEADER_CHAR(ch)                                                     \
    (ch == CR || ch == LF || ch == 9 || ((unsigned char)ch > 31 && ch != 127))
  
#define start_state (parser->type == HTTP_REQUEST ? s_start_req : s_start_res)


#if HTTP_PARSER_STRICT
# define STRICT_CHECK(cond)                                          \
  do {                                                                 \
    if (cond) {                                                        \
      SET_ERRNO(HPE_STRICT);                                           \
      goto error;                                                      \
    }                                                                  \
  } while (0)
# define NEW_MESSAGE() (http_should_keep_alive(parser) ? start_state : s_dead)
#else
# define STRICT_CHECK(cond)
# define NEW_MESSAGE() start_state
#endif

#define PROXY_CONNECTION "proxy-connection"
#define CONNECTION "connection"
#define CONTENT_LENGTH "content-length"
#define TRANSFER_ENCODING "transfer-encoding"
#define UPGRADE "upgrade"
#define CHUNKED "chunked"
#define KEEP_ALIVE "keep-alive"
#define CLOSE "close"

static const char *method_strings[] = {
#define XX(num, name, string) #string,
  HTTP_METHOD_MAP(XX)
#undef XX
};

void http_parser_init(http_parser *parser, 
                      enum http_parser_type t) {
  void *data = parser->data; /* preserve application data */
  memset(parser, 0, sizeof(http_parser));
  parser->data = data;
  parser->type = t;
  parser->state = 
      (t == HTTP_REQUEST ? 
          s_start_req : (t == HTTP_RESPONSE ? s_start_res : s_start_req_or_res));
  parser->http_errno = HPE_OK;
}

/* Does the parser need to see an EOF to find the end of the message? */
int http_message_needs_eof (const http_parser *parser) {
  if (parser->type == HTTP_REQUEST) {
    return 0;
  }

  /* See RFC 2616 section 4.4 */
  if (parser->status_code / 100 == 1 || /* 1xx e.g. Continue */
      parser->status_code == 204 ||     /* No Content */
      parser->status_code == 304 ||     /* Not Modified */
      parser->flags & F_SKIPBODY) {     /* response to a HEAD request */
    return 0;
  }

  if ((parser->flags & F_CHUNKED) || parser->content_length != ULLONG_MAX) {
    return 0;
  }

  return 1;
}

int http_should_keep_alive (const http_parser *parser) {
  if (parser->http_major > 0 && parser->http_minor > 0) {
    /* HTTP/1.1 */
    if (parser->flags & F_CONNECTION_CLOSE) {
      return 0;
    }
  } else {
    /* HTTP/1.0 or earlier */
    if (!(parser->flags & F_CONNECTION_KEEP_ALIVE)) {
      return 0;
    }
  }

  return !http_message_needs_eof(parser);
}

size_t http_parser_execute(http_parser *parser,
                           const http_parser_settings *settings,
                           const char *data,
                           const size_t len) {
  char c, ch;
  int8_t unhex_val;
  const char *p = data;
  const char *header_field_mark = 0;
  const char *header_value_mark = 0;
  const char *url_mark = 0;
  const char *body_mark = 0;
  const char *status_mark = 0;
  enum state p_state = (enum state) parser->state;
  const unsigned int lenient = parser->lenient_http_headers;
  uint32_t nread = parser->nread;

  /* We're in an error state. Don't bother doing anything. */
  if (HTTP_PARSER_ERRNO(parser) != HPE_OK) {
    return 0;
  }

  if (len == 0) {
    switch (CURRENT_STATE()) {
      case s_body_identity_eof:
        /* Use of CALLBACK_NOTIFY() here would erroneously return 1 byte read if
         * we got paused.
         */
        CALLBACK_NOTIFY_NOADVANCE(message_complete);
        return 0;

      case s_dead:
      case s_start_req_or_res:
      case s_start_res:
      case s_start_req:
        return 0;

      default:
        SET_ERRNO(HPE_INVALID_EOF_STATE);
        return 1;
    }
  }

  if (CURRENT_STATE() == s_header_field) {
    header_field_mark = data;
  }
  
  if (CURRENT_STATE() == s_header_value) {
    header_value_mark = data;
  }
  
  switch (CURRENT_STATE()) {
  case s_req_path:
  case s_req_schema:
  case s_req_schema_slash:
  case s_req_schema_slash_slash:
  case s_req_server_start:
  case s_req_server:
  case s_req_server_with_at:
  case s_req_query_string_start:
  case s_req_query_string:
  case s_req_fragment_start:
  case s_req_fragment:
    url_mark = data;
    break;
  case s_res_status:
    status_mark = data;
    break;
  default:
    break;
  }

  for (p=data; p!=data+len; p++) {
    ch = *p;

    if (PARSING_HEADER(CURRENT_STATE()))
      COUNT_HEADER_SIZE(1);

reexecute:
    switch (CURRENT_STATE()) {

      case s_dead:
        /* this state is used after a 'Connection: close' message
         * the parser will error out if it reads another message
         */
        if (LIKELY(ch == CR || ch == LF))
          break;

        SET_ERRNO(HPE_CLOSED_CONNECTION);
        goto error;

      case s_start_req_or_res:
      {
        if (ch == CR || ch == LF)
          break;
        parser->flags = 0;
        parser->content_length = ULLONG_MAX;

        if (ch == 'H') {
          UPDATE_STATE(s_res_or_resp_H);

          CALLBACK_NOTIFY(message_begin);
        } else {
          parser->type = HTTP_REQUEST;
          UPDATE_STATE(s_start_req);
          REEXECUTE();
        }

        break;
      }

      case s_res_or_resp_H:
        if (ch == 'T') {
          parser->type = HTTP_RESPONSE;
          UPDATE_STATE(s_res_HT);
        } else {
          if (UNLIKELY(ch != 'E')) {
            SET_ERRNO(HPE_INVALID_CONSTANT);
            goto error;
          }

          parser->type = HTTP_REQUEST;
          parser->method = HTTP_HEAD;
          parser->index = 2;
          UPDATE_STATE(s_req_method);
        }
        break;

      case s_start_res:
      {
        if (ch == CR || ch == LF)
          break;
        parser->flags = 0;
        parser->content_length = ULLONG_MAX;

        if (ch == 'H') {
          UPDATE_STATE(s_res_H);
        } else {
          SET_ERRNO(HPE_INVALID_CONSTANT);
          goto error;
        }

        CALLBACK_NOTIFY(message_begin);
        break;
      }

      case s_res_H:
        STRICT_CHECK(ch != 'T');
        UPDATE_STATE(s_res_HT);
        break;

      case s_res_HT:
        STRICT_CHECK(ch != 'T');
        UPDATE_STATE(s_res_HTT);
        break;

      case s_res_HTT:
        STRICT_CHECK(ch != 'P');
        UPDATE_STATE(s_res_HTTP);
        break;

      case s_res_HTTP:
        STRICT_CHECK(ch != '/');
        UPDATE_STATE(s_res_http_major);
        break;

      case s_res_http_major:
        if (UNLIKELY(!IS_NUM(ch))) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_major = ch - '0';
        UPDATE_STATE(s_res_http_dot);
        break;

      case s_res_http_dot:
      {
        if (UNLIKELY(ch != '.')) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        UPDATE_STATE(s_res_http_minor);
        break;
      }

      case s_res_http_minor:
        if (UNLIKELY(!IS_NUM(ch))) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_minor = ch - '0';
        UPDATE_STATE(s_res_http_end);
        break;

      case s_res_http_end:
      {
        if (UNLIKELY(ch != ' ')) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        UPDATE_STATE(s_res_first_status_code);
        break;
      }

      case s_res_first_status_code:
      {
        if (!IS_NUM(ch)) {
          if (ch == ' ') {
            break;
          }

          SET_ERRNO(HPE_INVALID_STATUS);
          goto error;
        }
        parser->status_code = ch - '0';
        UPDATE_STATE(s_res_status_code);
        break;
      }

      case s_res_status_code:
      {
        if (!IS_NUM(ch)) {
          switch (ch) {
            case ' ':
              UPDATE_STATE(s_res_status_start);
              break;
            case CR:
            case LF:
              UPDATE_STATE(s_res_status_start);
              REEXECUTE();
              break;
            default:
              SET_ERRNO(HPE_INVALID_STATUS);
              goto error;
          }
          break;
        }

        parser->status_code *= 10;
        parser->status_code += ch - '0';

        if (UNLIKELY(parser->status_code > 999)) {
          SET_ERRNO(HPE_INVALID_STATUS);
          goto error;
        }

        break;
      }

      case s_res_status_start:
      {
        MARK(status);
        UPDATE_STATE(s_res_status);
        parser->index = 0;

        if (ch == CR || ch == LF)
          REEXECUTE();

        break;
      }

      case s_res_status:
        if (ch == CR) {
          UPDATE_STATE(s_res_line_almost_done);
          CALLBACK_DATA(status);
          break;
        }

        if (ch == LF) {
          UPDATE_STATE(s_header_field_start);
          CALLBACK_DATA(status);
          break;
        }

        break;

      case s_res_line_almost_done:
        STRICT_CHECK(ch != LF);
        UPDATE_STATE(s_header_field_start);
        break;

      case s_start_req:
      {
        if (ch == CR || ch == LF)
          break;
        parser->flags = 0;
        parser->content_length = ULLONG_MAX;

        if (UNLIKELY(!IS_ALPHA(ch))) {
          SET_ERRNO(HPE_INVALID_METHOD);
          goto error;
        }

        parser->method = (enum http_method) 0;
        parser->index = 1;
        switch (ch) {
          case 'A': parser->method = HTTP_ACL; break;
          case 'B': parser->method = HTTP_BIND; break;
          case 'C': parser->method = HTTP_CONNECT; /* or COPY, CHECKOUT */ break;
          case 'D': parser->method = HTTP_DELETE; break;
          case 'G': parser->method = HTTP_GET; break;
          case 'H': parser->method = HTTP_HEAD; break;
          case 'L': parser->method = HTTP_LOCK; /* or LINK */ break;
          case 'M': parser->method = HTTP_MKCOL; /* or MOVE, MKACTIVITY, MERGE, M-SEARCH, MKCALENDAR */ break;
          case 'N': parser->method = HTTP_NOTIFY; break;
          case 'O': parser->method = HTTP_OPTIONS; break;
          case 'P': parser->method = HTTP_POST;
            /* or PROPFIND|PROPPATCH|PUT|PATCH|PURGE */
            break;
          case 'R': parser->method = HTTP_REPORT; /* or REBIND */ break;
          case 'S': parser->method = HTTP_SUBSCRIBE; /* or SEARCH, SOURCE */ break;
          case 'T': parser->method = HTTP_TRACE; break;
          case 'U': parser->method = HTTP_UNLOCK; /* or UNSUBSCRIBE, UNBIND, UNLINK */ break;
          default:
            SET_ERRNO(HPE_INVALID_METHOD);
            goto error;
        }
        UPDATE_STATE(s_req_method);

        CALLBACK_NOTIFY(message_begin);

        break;
      }

      case s_req_method:
      {
        const char *matcher;
        if (UNLIKELY(ch == '\0')) {
          SET_ERRNO(HPE_INVALID_METHOD);
          goto error;
        }

        matcher = method_strings[parser->method];
        if (ch == ' ' && matcher[parser->index] == '\0') {
          UPDATE_STATE(s_req_spaces_before_url);
        } else if (ch == matcher[parser->index]) {
          ; /* nada */
        } else if ((ch >= 'A' && ch <= 'Z') || ch == '-') {

          switch (parser->method << 16 | parser->index << 8 | ch) {
#define XX(meth, pos, ch, new_meth) \
            case (HTTP_##meth << 16 | pos << 8 | ch): \
              parser->method = HTTP_##new_meth; break;

            XX(POST,      1, 'U', PUT)
            XX(POST,      1, 'A', PATCH)
            XX(POST,      1, 'R', PROPFIND)
            XX(PUT,       2, 'R', PURGE)
            XX(CONNECT,   1, 'H', CHECKOUT)
            XX(CONNECT,   2, 'P', COPY)
            XX(MKCOL,     1, 'O', MOVE)
            XX(MKCOL,     1, 'E', MERGE)
            XX(MKCOL,     1, '-', MSEARCH)
            XX(MKCOL,     2, 'A', MKACTIVITY)
            XX(MKCOL,     3, 'A', MKCALENDAR)
            XX(SUBSCRIBE, 1, 'E', SEARCH)
            XX(SUBSCRIBE, 1, 'O', SOURCE)
            XX(REPORT,    2, 'B', REBIND)
            XX(PROPFIND,  4, 'P', PROPPATCH)
            XX(LOCK,      1, 'I', LINK)
            XX(UNLOCK,    2, 'S', UNSUBSCRIBE)
            XX(UNLOCK,    2, 'B', UNBIND)
            XX(UNLOCK,    3, 'I', UNLINK)
#undef XX
            default:
              SET_ERRNO(HPE_INVALID_METHOD);
              goto error;
          }
        } else {
          SET_ERRNO(HPE_INVALID_METHOD);
          goto error;
        }

        ++parser->index;
        break;
      }

      case s_req_spaces_before_url:
      {
        if (ch == ' ') break;

        MARK(url);
        if (parser->method == HTTP_CONNECT) {
          UPDATE_STATE(s_req_server_start);
        }

        UPDATE_STATE(parse_url_char(CURRENT_STATE(), ch));
        if (UNLIKELY(CURRENT_STATE() == s_dead)) {
          SET_ERRNO(HPE_INVALID_URL);
          goto error;
        }

        break;
      }

      case s_req_schema:
      case s_req_schema_slash:
      case s_req_schema_slash_slash:
      case s_req_server_start:
      {
        switch (ch) {
          /* No whitespace allowed here */
          case ' ':
          case CR:
          case LF:
            SET_ERRNO(HPE_INVALID_URL);
            goto error;
          default:
            UPDATE_STATE(parse_url_char(CURRENT_STATE(), ch));
            if (UNLIKELY(CURRENT_STATE() == s_dead)) {
              SET_ERRNO(HPE_INVALID_URL);
              goto error;
            }
        }

        break;
      }

      case s_req_server:
      case s_req_server_with_at:
      case s_req_path:
      case s_req_query_string_start:
      case s_req_query_string:
      case s_req_fragment_start:
      case s_req_fragment:
      {
        switch (ch) {
          case ' ':
            UPDATE_STATE(s_req_http_start);
            CALLBACK_DATA(url);
            break;
          case CR:
          case LF:
            parser->http_major = 0;
            parser->http_minor = 9;
            UPDATE_STATE((ch == CR) ?
              s_req_line_almost_done :
              s_header_field_start);
            CALLBACK_DATA(url);
            break;
          default:
            UPDATE_STATE(parse_url_char(CURRENT_STATE(), ch));
            if (UNLIKELY(CURRENT_STATE() == s_dead)) {
              SET_ERRNO(HPE_INVALID_URL);
              goto error;
            }
        }
        break;
      }

      case s_req_http_start:
        switch (ch) {
          case ' ':
            break;
          case 'H':
            UPDATE_STATE(s_req_http_H);
            break;
          case 'I':
            if (parser->method == HTTP_SOURCE) {
              UPDATE_STATE(s_req_http_I);
              break;
            }
            /* fall through */
          default:
            SET_ERRNO(HPE_INVALID_CONSTANT);
            goto error;
        }
        break;

      case s_req_http_H:
        STRICT_CHECK(ch != 'T');
        UPDATE_STATE(s_req_http_HT);
        break;

      case s_req_http_HT:
        STRICT_CHECK(ch != 'T');
        UPDATE_STATE(s_req_http_HTT);
        break;

      case s_req_http_HTT:
        STRICT_CHECK(ch != 'P');
        UPDATE_STATE(s_req_http_HTTP);
        break;

      case s_req_http_I:
        STRICT_CHECK(ch != 'C');
        UPDATE_STATE(s_req_http_IC);
        break;

      case s_req_http_IC:
        STRICT_CHECK(ch != 'E');
        UPDATE_STATE(s_req_http_HTTP);  /* Treat "ICE" as "HTTP". */
        break;

      case s_req_http_HTTP:
        STRICT_CHECK(ch != '/');
        UPDATE_STATE(s_req_http_major);
        break;

      case s_req_http_major:
        if (UNLIKELY(!IS_NUM(ch))) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_major = ch - '0';
        UPDATE_STATE(s_req_http_dot);
        break;

      case s_req_http_dot:
      {
        if (UNLIKELY(ch != '.')) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        UPDATE_STATE(s_req_http_minor);
        break;
      }

      case s_req_http_minor:
        if (UNLIKELY(!IS_NUM(ch))) {
          SET_ERRNO(HPE_INVALID_VERSION);
          goto error;
        }

        parser->http_minor = ch - '0';
        UPDATE_STATE(s_req_http_end);
        break;

      case s_req_http_end:
      {
        if (ch == CR) {
          UPDATE_STATE(s_req_line_almost_done);
          break;
        }

        if (ch == LF) {
          UPDATE_STATE(s_header_field_start);
          break;
        }

        SET_ERRNO(HPE_INVALID_VERSION);
        goto error;
        break;
      }

      /* end of request line */
      case s_req_line_almost_done:
      {
        if (UNLIKELY(ch != LF)) {
          SET_ERRNO(HPE_LF_EXPECTED);
          goto error;
        }

        UPDATE_STATE(s_header_field_start);
        break;
      }

      case s_header_field_start:
      {
        if (ch == CR) {
          UPDATE_STATE(s_headers_almost_done);
          break;
        }

        if (ch == LF) {
          /* they might be just sending \n instead of \r\n so this would be
           * the second \n to denote the end of headers*/
          UPDATE_STATE(s_headers_almost_done);
          REEXECUTE();
        }

        c = TOKEN(ch);

        if (UNLIKELY(!c)) {
          SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
          goto error;
        }

        MARK(header_field);

        parser->index = 0;
        UPDATE_STATE(s_header_field);

        switch (c) {
          case 'c':
            parser->header_state = h_C;
            break;

          case 'p':
            parser->header_state = h_matching_proxy_connection;
            break;

          case 't':
            parser->header_state = h_matching_transfer_encoding;
            break;

          case 'u':
            parser->header_state = h_matching_upgrade;
            break;

          default:
            parser->header_state = h_general;
            break;
        }
        break;
      }

      case s_header_field:
      {
        const char* start = p;
        for (; p != data + len; p++) {
          ch = *p;
          c = TOKEN(ch);

          if (!c)
            break;

          switch (parser->header_state) {
            case h_general: {
              size_t left = data + len - p;
              const char* pe = p + MIN(left, max_header_size);
              while (p+1 < pe && TOKEN(p[1])) {
                p++;
              }
              break;
            }

            case h_C:
              parser->index++;
              parser->header_state = (c == 'o' ? h_CO : h_general);
              break;

            case h_CO:
              parser->index++;
              parser->header_state = (c == 'n' ? h_CON : h_general);
              break;

            case h_CON:
              parser->index++;
              switch (c) {
                case 'n':
                  parser->header_state = h_matching_connection;
                  break;
                case 't':
                  parser->header_state = h_matching_content_length;
                  break;
                default:
                  parser->header_state = h_general;
                  break;
              }
              break;

            /* connection */

            case h_matching_connection:
              parser->index++;
              if (parser->index > sizeof(CONNECTION)-1
                  || c != CONNECTION[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CONNECTION)-2) {
                parser->header_state = h_connection;
              }
              break;

            /* proxy-connection */

            case h_matching_proxy_connection:
              parser->index++;
              if (parser->index > sizeof(PROXY_CONNECTION)-1
                  || c != PROXY_CONNECTION[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(PROXY_CONNECTION)-2) {
                parser->header_state = h_connection;
              }
              break;

            /* content-length */

            case h_matching_content_length:
              parser->index++;
              if (parser->index > sizeof(CONTENT_LENGTH)-1
                  || c != CONTENT_LENGTH[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(CONTENT_LENGTH)-2) {
                parser->header_state = h_content_length;
              }
              break;

            /* transfer-encoding */

            case h_matching_transfer_encoding:
              parser->index++;
              if (parser->index > sizeof(TRANSFER_ENCODING)-1
                  || c != TRANSFER_ENCODING[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(TRANSFER_ENCODING)-2) {
                parser->header_state = h_transfer_encoding;
              }
              break;

            /* upgrade */

            case h_matching_upgrade:
              parser->index++;
              if (parser->index > sizeof(UPGRADE)-1
                  || c != UPGRADE[parser->index]) {
                parser->header_state = h_general;
              } else if (parser->index == sizeof(UPGRADE)-2) {
                parser->header_state = h_upgrade;
              }
              break;

            case h_connection:
            case h_content_length:
            case h_transfer_encoding:
            case h_upgrade:
              if (ch != ' ') parser->header_state = h_general;
              break;

            default:
              assert(0 && "Unknown header_state");
              break;
          }
        }

        if (p == data + len) {
          --p;
          COUNT_HEADER_SIZE(p - start);
          break;
        }

        COUNT_HEADER_SIZE(p - start);

        if (ch == ':') {
          UPDATE_STATE(s_header_value_discard_ws);
          CALLBACK_DATA(header_field);
          break;
        }

        SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
        goto error;
      }

      case s_header_value_discard_ws:
        if (ch == ' ' || ch == '\t') break;

        if (ch == CR) {
          UPDATE_STATE(s_header_value_discard_ws_almost_done);
          break;
        }

        if (ch == LF) {
          UPDATE_STATE(s_header_value_discard_lws);
          break;
        }

        /* fall through */

      case s_header_value_start:
      {
        MARK(header_value);

        UPDATE_STATE(s_header_value);
        parser->index = 0;

        c = LOWER(ch);

        switch (parser->header_state) {
          case h_upgrade:
            parser->flags |= F_UPGRADE;
            parser->header_state = h_general;
            break;

          case h_transfer_encoding:
            /* looking for 'Transfer-Encoding: chunked' */
            if ('c' == c) {
              parser->header_state = h_matching_transfer_encoding_chunked;
            } else {
              parser->header_state = h_general;
            }
            break;

          case h_content_length:
            if (UNLIKELY(!IS_NUM(ch))) {
              SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
              goto error;
            }

            if (parser->flags & F_CONTENTLENGTH) {
              SET_ERRNO(HPE_UNEXPECTED_CONTENT_LENGTH);
              goto error;
            }

            parser->flags |= F_CONTENTLENGTH;
            parser->content_length = ch - '0';
            parser->header_state = h_content_length_num;
            break;

          /* when obsolete line folding is encountered for content length
           * continue to the s_header_value state */
          case h_content_length_ws:
            break;

          case h_connection:
            /* looking for 'Connection: keep-alive' */
            if (c == 'k') {
              parser->header_state = h_matching_connection_keep_alive;
            /* looking for 'Connection: close' */
            } else if (c == 'c') {
              parser->header_state = h_matching_connection_close;
            } else if (c == 'u') {
              parser->header_state = h_matching_connection_upgrade;
            } else {
              parser->header_state = h_matching_connection_token;
            }
            break;

          /* Multi-value `Connection` header */
          case h_matching_connection_token_start:
            break;

          default:
            parser->header_state = h_general;
            break;
        }
        break;
      }

      case s_header_value:
      {
        const char* start = p;
        enum header_states h_state = (enum header_states) parser->header_state;
        for (; p != data + len; p++) {
          ch = *p;
          if (ch == CR) {
            UPDATE_STATE(s_header_almost_done);
            parser->header_state = h_state;
            CALLBACK_DATA(header_value);
            break;
          }

          if (ch == LF) {
            UPDATE_STATE(s_header_almost_done);
            COUNT_HEADER_SIZE(p - start);
            parser->header_state = h_state;
            CALLBACK_DATA_NOADVANCE(header_value);
            REEXECUTE();
          }

          if (!lenient && !IS_HEADER_CHAR(ch)) {
            SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
            goto error;
          }

          c = LOWER(ch);

          switch (h_state) {
            case h_general:
              {
                size_t left = data + len - p;
                const char* pe = p + MIN(left, max_header_size);

                for (; p != pe; p++) {
                  ch = *p;
                  if (ch == CR || ch == LF) {
                    --p;
                    break;
                  }
                  if (!lenient && !IS_HEADER_CHAR(ch)) {
                    SET_ERRNO(HPE_INVALID_HEADER_TOKEN);
                    goto error;
                  }
                }
                if (p == data + len)
                  --p;
                break;
              }

            case h_connection:
            case h_transfer_encoding:
              assert(0 && "Shouldn't get here.");
              break;

            case h_content_length:
              if (ch == ' ') break;
              h_state = h_content_length_num;
              /* fall through */

            case h_content_length_num:
            {
              uint64_t t;

              if (ch == ' ') {
                h_state = h_content_length_ws;
                break;
              }

              if (UNLIKELY(!IS_NUM(ch))) {
                SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                parser->header_state = h_state;
                goto error;
              }

              t = parser->content_length;
              t *= 10;
              t += ch - '0';

              /* Overflow? Test against a conservative limit for simplicity. */
              if (UNLIKELY((ULLONG_MAX - 10) / 10 < parser->content_length)) {
                SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
                parser->header_state = h_state;
                goto error;
              }

              parser->content_length = t;
              break;
            }

            case h_content_length_ws:
              if (ch == ' ') break;
              SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
              parser->header_state = h_state;
              goto error;

            /* Transfer-Encoding: chunked */
            case h_matching_transfer_encoding_chunked:
              parser->index++;
              if (parser->index > sizeof(CHUNKED)-1
                  || c != CHUNKED[parser->index]) {
                h_state = h_general;
              } else if (parser->index == sizeof(CHUNKED)-2) {
                h_state = h_transfer_encoding_chunked;
              }
              break;

            case h_matching_connection_token_start:
              /* looking for 'Connection: keep-alive' */
              if (c == 'k') {
                h_state = h_matching_connection_keep_alive;
              /* looking for 'Connection: close' */
              } else if (c == 'c') {
                h_state = h_matching_connection_close;
              } else if (c == 'u') {
                h_state = h_matching_connection_upgrade;
              } else if (STRICT_TOKEN(c)) {
                h_state = h_matching_connection_token;
              } else if (c == ' ' || c == '\t') {
                /* Skip lws */
              } else {
                h_state = h_general;
              }
              break;

            /* looking for 'Connection: keep-alive' */
            case h_matching_connection_keep_alive:
              parser->index++;
              if (parser->index > sizeof(KEEP_ALIVE)-1
                  || c != KEEP_ALIVE[parser->index]) {
                h_state = h_matching_connection_token;
              } else if (parser->index == sizeof(KEEP_ALIVE)-2) {
                h_state = h_connection_keep_alive;
              }
              break;

            /* looking for 'Connection: close' */
            case h_matching_connection_close:
              parser->index++;
              if (parser->index > sizeof(CLOSE)-1 || c != CLOSE[parser->index]) {
                h_state = h_matching_connection_token;
              } else if (parser->index == sizeof(CLOSE)-2) {
                h_state = h_connection_close;
              }
              break;

            /* looking for 'Connection: upgrade' */
            case h_matching_connection_upgrade:
              parser->index++;
              if (parser->index > sizeof(UPGRADE) - 1 ||
                  c != UPGRADE[parser->index]) {
                h_state = h_matching_connection_token;
              } else if (parser->index == sizeof(UPGRADE)-2) {
                h_state = h_connection_upgrade;
              }
              break;

            case h_matching_connection_token:
              if (ch == ',') {
                h_state = h_matching_connection_token_start;
                parser->index = 0;
              }
              break;

            case h_transfer_encoding_chunked:
              if (ch != ' ') h_state = h_general;
              break;

            case h_connection_keep_alive:
            case h_connection_close:
            case h_connection_upgrade:
              if (ch == ',') {
                if (h_state == h_connection_keep_alive) {
                  parser->flags |= F_CONNECTION_KEEP_ALIVE;
                } else if (h_state == h_connection_close) {
                  parser->flags |= F_CONNECTION_CLOSE;
                } else if (h_state == h_connection_upgrade) {
                  parser->flags |= F_CONNECTION_UPGRADE;
                }
                h_state = h_matching_connection_token_start;
                parser->index = 0;
              } else if (ch != ' ') {
                h_state = h_matching_connection_token;
              }
              break;

            default:
              UPDATE_STATE(s_header_value);
              h_state = h_general;
              break;
          }
        }
        parser->header_state = h_state;

        if (p == data + len)
          --p;

        COUNT_HEADER_SIZE(p - start);
        break;
      }

      case s_header_almost_done:
      {
        if (UNLIKELY(ch != LF)) {
          SET_ERRNO(HPE_LF_EXPECTED);
          goto error;
        }

        UPDATE_STATE(s_header_value_lws);
        break;
      }

      case s_header_value_lws:
      {
        if (ch == ' ' || ch == '\t') {
          if (parser->header_state == h_content_length_num) {
              /* treat obsolete line folding as space */
              parser->header_state = h_content_length_ws;
          }
          UPDATE_STATE(s_header_value_start);
          REEXECUTE();
        }

        /* finished the header */
        switch (parser->header_state) {
          case h_connection_keep_alive:
            parser->flags |= F_CONNECTION_KEEP_ALIVE;
            break;
          case h_connection_close:
            parser->flags |= F_CONNECTION_CLOSE;
            break;
          case h_transfer_encoding_chunked:
            parser->flags |= F_CHUNKED;
            break;
          case h_connection_upgrade:
            parser->flags |= F_CONNECTION_UPGRADE;
            break;
          default:
            break;
        }

        UPDATE_STATE(s_header_field_start);
        REEXECUTE();
      }

      case s_header_value_discard_ws_almost_done:
      {
        STRICT_CHECK(ch != LF);
        UPDATE_STATE(s_header_value_discard_lws);
        break;
      }

      case s_header_value_discard_lws:
      {
        if (ch == ' ' || ch == '\t') {
          UPDATE_STATE(s_header_value_discard_ws);
          break;
        } else {
          switch (parser->header_state) {
            case h_connection_keep_alive:
              parser->flags |= F_CONNECTION_KEEP_ALIVE;
              break;
            case h_connection_close:
              parser->flags |= F_CONNECTION_CLOSE;
              break;
            case h_connection_upgrade:
              parser->flags |= F_CONNECTION_UPGRADE;
              break;
            case h_transfer_encoding_chunked:
              parser->flags |= F_CHUNKED;
              break;
            case h_content_length:
              /* do not allow empty content length */
              SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
              goto error;
              break;
            default:
              break;
          }

          /* header value was empty */
          MARK(header_value);
          UPDATE_STATE(s_header_field_start);
          CALLBACK_DATA_NOADVANCE(header_value);
          REEXECUTE();
        }
      }

      case s_headers_almost_done:
      {
        STRICT_CHECK(ch != LF);

        if (parser->flags & F_TRAILING) {
          /* End of a chunked request */
          UPDATE_STATE(s_message_done);
          CALLBACK_NOTIFY_NOADVANCE(chunk_complete);
          REEXECUTE();
        }

        /* Cannot use chunked encoding and a content-length header together
           per the HTTP specification. */
        if ((parser->flags & F_CHUNKED) &&
            (parser->flags & F_CONTENTLENGTH)) {
          SET_ERRNO(HPE_UNEXPECTED_CONTENT_LENGTH);
          goto error;
        }

        UPDATE_STATE(s_headers_done);

        /* Set this here so that on_headers_complete() callbacks can see it */
        if ((parser->flags & F_UPGRADE) &&
            (parser->flags & F_CONNECTION_UPGRADE)) {
          /* For responses, "Upgrade: foo" and "Connection: upgrade" are
           * mandatory only when it is a 101 Switching Protocols response,
           * otherwise it is purely informational, to announce support.
           */
          parser->upgrade =
              (parser->type == HTTP_REQUEST || parser->status_code == 101);
        } else {
          parser->upgrade = (parser->method == HTTP_CONNECT);
        }

        /* Here we call the headers_complete callback. This is somewhat
         * different than other callbacks because if the user returns 1, we
         * will interpret that as saying that this message has no body. This
         * is needed for the annoying case of recieving a response to a HEAD
         * request.
         *
         * We'd like to use CALLBACK_NOTIFY_NOADVANCE() here but we cannot, so
         * we have to simulate it by handling a change in errno below.
         */
        if (settings->on_headers_complete) {
          switch (settings->on_headers_complete(parser)) {
            case 0:
              break;

            case 2:
              parser->upgrade = 1;

              /* fall through */
            case 1:
              parser->flags |= F_SKIPBODY;
              break;

            default:
              SET_ERRNO(HPE_CB_headers_complete);
              RETURN(p - data); /* Error */
          }
        }

        if (HTTP_PARSER_ERRNO(parser) != HPE_OK) {
          RETURN(p - data);
        }

        REEXECUTE();
      }

      case s_headers_done:
      {
        int hasBody;
        STRICT_CHECK(ch != LF);

        parser->nread = 0;
        nread = 0;

        hasBody = parser->flags & F_CHUNKED ||
          (parser->content_length > 0 && parser->content_length != ULLONG_MAX);
        if (parser->upgrade && (parser->method == HTTP_CONNECT ||
                                (parser->flags & F_SKIPBODY) || !hasBody)) {
          /* Exit, the rest of the message is in a different protocol. */
          UPDATE_STATE(NEW_MESSAGE());
          CALLBACK_NOTIFY(message_complete);
          RETURN((p - data) + 1);
        }

        if (parser->flags & F_SKIPBODY) {
          UPDATE_STATE(NEW_MESSAGE());
          CALLBACK_NOTIFY(message_complete);
        } else if (parser->flags & F_CHUNKED) {
          /* chunked encoding - ignore Content-Length header */
          UPDATE_STATE(s_chunk_size_start);
        } else {
          if (parser->content_length == 0) {
            /* Content-Length header given but zero: Content-Length: 0\r\n */
            UPDATE_STATE(NEW_MESSAGE());
            CALLBACK_NOTIFY(message_complete);
          } else if (parser->content_length != ULLONG_MAX) {
            /* Content-Length header given and non-zero */
            UPDATE_STATE(s_body_identity);
          } else {
            if (!http_message_needs_eof(parser)) {
              /* Assume content-length 0 - read the next */
              UPDATE_STATE(NEW_MESSAGE());
              CALLBACK_NOTIFY(message_complete);
            } else {
              /* Read body until EOF */
              UPDATE_STATE(s_body_identity_eof);
            }
          }
        }

        break;
      }

      case s_body_identity:
      {
        uint64_t to_read = MIN(parser->content_length,
                               (uint64_t) ((data + len) - p));

        assert(parser->content_length != 0
            && parser->content_length != ULLONG_MAX);

        /* The difference between advancing content_length and p is because
         * the latter will automaticaly advance on the next loop iteration.
         * Further, if content_length ends up at 0, we want to see the last
         * byte again for our message complete callback.
         */
        MARK(body);
        parser->content_length -= to_read;
        p += to_read - 1;

        if (parser->content_length == 0) {
          UPDATE_STATE(s_message_done);

          /* Mimic CALLBACK_DATA_NOADVANCE() but with one extra byte.
           *
           * The alternative to doing this is to wait for the next byte to
           * trigger the data callback, just as in every other case. The
           * problem with this is that this makes it difficult for the test
           * harness to distinguish between complete-on-EOF and
           * complete-on-length. It's not clear that this distinction is
           * important for applications, but let's keep it for now.
           */
          CALLBACK_DATA_(body, p - body_mark + 1, p - data);
          REEXECUTE();
        }

        break;
      }

      /* read until EOF */
      case s_body_identity_eof:
        MARK(body);
        p = data + len - 1;

        break;

      case s_message_done:
        UPDATE_STATE(NEW_MESSAGE());
        CALLBACK_NOTIFY(message_complete);
        if (parser->upgrade) {
          /* Exit, the rest of the message is in a different protocol. */
          RETURN((p - data) + 1);
        }
        break;

      case s_chunk_size_start:
      {
        assert(nread == 1);
        assert(parser->flags & F_CHUNKED);

        unhex_val = unhex[(unsigned char)ch];
        if (UNLIKELY(unhex_val == -1)) {
          SET_ERRNO(HPE_INVALID_CHUNK_SIZE);
          goto error;
        }

        parser->content_length = unhex_val;
        UPDATE_STATE(s_chunk_size);
        break;
      }

      case s_chunk_size:
      {
        uint64_t t;

        assert(parser->flags & F_CHUNKED);

        if (ch == CR) {
          UPDATE_STATE(s_chunk_size_almost_done);
          break;
        }

        unhex_val = unhex[(unsigned char)ch];

        if (unhex_val == -1) {
          if (ch == ';' || ch == ' ') {
            UPDATE_STATE(s_chunk_parameters);
            break;
          }

          SET_ERRNO(HPE_INVALID_CHUNK_SIZE);
          goto error;
        }

        t = parser->content_length;
        t *= 16;
        t += unhex_val;

        /* Overflow? Test against a conservative limit for simplicity. */
        if (UNLIKELY((ULLONG_MAX - 16) / 16 < parser->content_length)) {
          SET_ERRNO(HPE_INVALID_CONTENT_LENGTH);
          goto error;
        }

        parser->content_length = t;
        break;
      }

      case s_chunk_parameters:
      {
        assert(parser->flags & F_CHUNKED);
        /* just ignore this shit. TODO check for overflow */
        if (ch == CR) {
          UPDATE_STATE(s_chunk_size_almost_done);
          break;
        }
        break;
      }

      case s_chunk_size_almost_done:
      {
        assert(parser->flags & F_CHUNKED);
        STRICT_CHECK(ch != LF);

        parser->nread = 0;
        nread = 0;

        if (parser->content_length == 0) {
          parser->flags |= F_TRAILING;
          UPDATE_STATE(s_header_field_start);
        } else {
          UPDATE_STATE(s_chunk_data);
        }
        CALLBACK_NOTIFY(chunk_header);
        break;
      }

      case s_chunk_data:
      {
        uint64_t to_read = MIN(parser->content_length,
                               (uint64_t) ((data + len) - p));

        assert(parser->flags & F_CHUNKED);
        assert(parser->content_length != 0
            && parser->content_length != ULLONG_MAX);

        /* See the explanation in s_body_identity for why the content
         * length and data pointers are managed this way.
         */
        MARK(body);
        parser->content_length -= to_read;
        p += to_read - 1;

        if (parser->content_length == 0) {
          UPDATE_STATE(s_chunk_data_almost_done);
        }

        break;
      }

      case s_chunk_data_almost_done:
        assert(parser->flags & F_CHUNKED);
        assert(parser->content_length == 0);
        STRICT_CHECK(ch != CR);
        UPDATE_STATE(s_chunk_data_done);
        CALLBACK_DATA(body);
        break;

      case s_chunk_data_done:
        assert(parser->flags & F_CHUNKED);
        STRICT_CHECK(ch != LF);
        parser->nread = 0;
        nread = 0;
        UPDATE_STATE(s_chunk_size_start);
        CALLBACK_NOTIFY(chunk_complete);
        break;

      default:
        assert(0 && "unhandled state");
        SET_ERRNO(HPE_INVALID_INTERNAL_STATE);
        goto error;
    }
  }

  /* Run callbacks for any marks that we have leftover after we ran out of
   * bytes. There should be at most one of these set, so it's OK to invoke
   * them in series (unset marks will not result in callbacks).
   *
   * We use the NOADVANCE() variety of callbacks here because 'p' has already
   * overflowed 'data' and this allows us to correct for the off-by-one that
   * we'd otherwise have (since CALLBACK_DATA() is meant to be run with a 'p'
   * value that's in-bounds).
   */

  assert(((header_field_mark ? 1 : 0) +
          (header_value_mark ? 1 : 0) +
          (url_mark ? 1 : 0)  +
          (body_mark ? 1 : 0) +
          (status_mark ? 1 : 0)) <= 1);

  CALLBACK_DATA_NOADVANCE(header_field);
  CALLBACK_DATA_NOADVANCE(header_value);
  CALLBACK_DATA_NOADVANCE(url);
  CALLBACK_DATA_NOADVANCE(body);
  CALLBACK_DATA_NOADVANCE(status);

  RETURN(len);

error:
  if (HTTP_PARSER_ERRNO(parser) == HPE_OK) {
    SET_ERRNO(HPE_UNKNOWN);
  }

  RETURN(p - data);
}

bool srs_go_http_body_allowd(int status) {
 if (status >= SRS_CONSTS_HTTP_Continue && status < SRS_CONSTS_HTTP_OK) {
   return false;
 } else if (status == SRS_CONSTS_HTTP_NoContent || status == SRS_CONSTS_HTTP_NotModified) {
   return false;
 }
 
 return true;
}

} //namespace ma

