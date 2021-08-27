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
#include "common/media_log.h"
#include "http/http_consts.h"
#include "common/srs_kernel_error.h"
#include "utils/protocol_utility.h"


namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("http_stack");

#ifndef HTTP_PARSER_STRICT
# define HTTP_PARSER_STRICT 1
#endif


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

/**
 * Verify that a char is a valid visible (printable) US-ASCII
 * character or %x80-FF
 **/
#define IS_HEADER_CHAR(ch)                                                     \
  (ch == CR || ch == LF || ch == 9 || ((unsigned char)ch > 31 && ch != 127))


enum state
  { s_dead = 1 /* important that this is > 0 */

  , s_start_req_or_res
  , s_res_or_resp_H
  , s_start_res
  , s_res_H
  , s_res_HT
  , s_res_HTT
  , s_res_HTTP
  , s_res_http_major
  , s_res_http_dot
  , s_res_http_minor
  , s_res_http_end
  , s_res_first_status_code
  , s_res_status_code
  , s_res_status_start
  , s_res_status
  , s_res_line_almost_done

  , s_start_req

  , s_req_method
  , s_req_spaces_before_url
  , s_req_schema
  , s_req_schema_slash
  , s_req_schema_slash_slash
  , s_req_server_start
  , s_req_server
  , s_req_server_with_at
  , s_req_path
  , s_req_query_string_start
  , s_req_query_string
  , s_req_fragment_start
  , s_req_fragment
  , s_req_http_start
  , s_req_http_H
  , s_req_http_HT
  , s_req_http_HTT
  , s_req_http_HTTP
  , s_req_http_I
  , s_req_http_IC
  , s_req_http_major
  , s_req_http_dot
  , s_req_http_minor
  , s_req_http_end
  , s_req_line_almost_done

  , s_header_field_start
  , s_header_field
  , s_header_value_discard_ws
  , s_header_value_discard_ws_almost_done
  , s_header_value_discard_lws
  , s_header_value_start
  , s_header_value
  , s_header_value_lws

  , s_header_almost_done

  , s_chunk_size_start
  , s_chunk_size
  , s_chunk_parameters
  , s_chunk_size_almost_done

  , s_headers_almost_done
  , s_headers_done

  /* Important: 's_headers_done' must be the last 'header' state. All
   * states beyond this must be 'body' states. It is used for overflow
   * checking. See the PARSING_HEADER() macro.
   */

  , s_chunk_data
  , s_chunk_data_almost_done
  , s_chunk_data_done

  , s_body_identity
  , s_body_identity_eof

  , s_message_done
  };


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
static enum state
parse_url_char(enum state s, const char ch)
{
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

srs_error_t SrsHttpUri::initialize(std::string _url)
{
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

void SrsHttpUri::set_schema(std::string v)
{
    schema = v;

    // Update url with new schema.
    size_t pos = url.find("://");
    if (pos != string::npos) {
        url = schema + "://" + url.substr(pos + 3);
    }
}

string SrsHttpUri::get_url()
{
    return url;
}

string SrsHttpUri::get_schema()
{
    return schema;
}

string SrsHttpUri::get_host()
{
    return host;
}

int SrsHttpUri::get_port()
{
    return port;
}

string SrsHttpUri::get_path()
{
    return path;
}

string SrsHttpUri::get_query()
{
    return query;
}

string SrsHttpUri::get_query_by_key(std::string key)
{
    std::map<string, string>::iterator it = query_values_.find(key);
    if(it == query_values_.end()) {
      return "";
    }
    return it->second;
}

std::string SrsHttpUri::username()
{
    return username_;
}

std::string SrsHttpUri::password()
{
    return password_;
}

string SrsHttpUri::get_uri_field(string uri, void* php_u, int ifield)
{
	http_parser_url* hp_u = (http_parser_url*)php_u;
	http_parser_url_fields field = (http_parser_url_fields)ifield;

    if((hp_u->field_set & (1 << field)) == 0){
        return "";
    }

    int offset = hp_u->field_data[field].off;
    int len = hp_u->field_data[field].len;

    return uri.substr(offset, len);
}

srs_error_t SrsHttpUri::parse_query()
{
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

void SrsHttpHeader::set(string key, string value)
{
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

string SrsHttpHeader::get(const string& key) const 
{
    std::string v;

    auto it = headers.find(key);
    if (it != headers.end()) {
        v = it->second;
    }
    
    return v;
}

void SrsHttpHeader::del(const string& key)
{
    map<string, string>::iterator it = headers.find(key);
    if (it != headers.end()) {
        headers.erase(it);
    }
}

int SrsHttpHeader::count()
{
    return (int)headers.size();
}

int64_t SrsHttpHeader::content_length()
{
    std::string cl = get("Content-Length");
    
    if (cl.empty()) {
        return -1;
    }
    
    return (int64_t)::atof(cl.c_str());
}

void SrsHttpHeader::set_content_length(int64_t size)
{
    set("Content-Length", srs_int2str(size));
}

std::string SrsHttpHeader::content_type()
{
    return get("Content-Type");
}

void SrsHttpHeader::set_content_type(std::string ct)
{
    set("Content-Type", ct);
}

void SrsHttpHeader::write(std::stringstream& ss)
{
  std::map<std::string, std::string>::iterator it;
  for (it = headers.begin(); it != headers.end(); ++it) {
      ss << it->first << ": " << it->second << SRS_HTTP_CRLF;
  }
}

const std::map<std::string, std::string>&SrsHttpHeader::header()
{
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

HttpMessage::HttpMessage(const std::string& body)
  : _status{SRS_CONSTS_HTTP_OK},
    _body{std::move(body)},
    _uri{std::make_unique<SrsHttpUri>()}
{
}

void HttpMessage::set_basic(
  uint8_t type, const std::string& method, uint16_t status, int64_t content_length)
{
  type_ = type;
  _method = method;
  _status = status;
  if (_content_length == -1) {
      _content_length = content_length;
  }
}

void HttpMessage::set_header(const SrsHttpHeader& header, bool keep_alive)
{
  _header = header;
  _keep_alive = keep_alive;

  // whether chunked.
  chunked = (header.get("Transfer-Encoding") == "chunked");

  // Update the content-length in header.
  std::string clv = header.get("Content-Length");
  if (!clv.empty()) {
      _content_length = ::atoll(clv.c_str());
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

void retrieve_local_ips()
{
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

std::vector<SrsIPAddress*>& srs_get_local_ips()
{
    if (_srs_system_ips.empty()) {
        retrieve_local_ips();
    }
    
    return _srs_system_ips;
}

std::string srs_get_public_internet_address(bool ipv4_only)
{
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

std::string srs_path_filext(const std::string& path)
{
  size_t pos = std::string::npos;
  
  if ((pos = path.rfind(".")) != std::string::npos) {
    return path.substr(pos);
  }
  
  return "";
}

srs_error_t HttpMessage::set_url(const std::string& url, bool allow_jsonp)
{
  srs_error_t err = srs_success;
  
  _url = url;

  // parse uri from schema/server:port/path?query
  std::string uri = _url;

  if (!srs_string_contains(uri, "://")) {
    // use server public ip when host not specified.
    // to make telnet happy.
    std::string host = _header.get("Host");

    // If no host in header, we use local discovered IP, IPv4 first.
    if (host.empty()) {
        host = srs_get_public_internet_address(true);
    }

    if (!host.empty()) {
        uri = "http://" + host + _url;
    }
  }

  if ((err = _uri->initialize(uri)) != srs_success) {
      return srs_error_wrap(err, "init uri %s", uri.c_str());
  }
  
  // parse ext.
  _ext = srs_path_filext(_uri->get_path());
  
  // parse query string.
  srs_parse_query_string(_uri->get_query(), _query);
  
  // parse jsonp request message.
  if (allow_jsonp) {
    if (!query_get("callback").empty()) {
        jsonp = true;
    }
    if (jsonp) {
        jsonp_method = query_get("method");
    }
  }
  
  return err;
}

void HttpMessage::set_https(bool v)
{
  schema_ = v? "https" : "http";
  _uri->set_schema(schema_);
}

const std::string& HttpMessage::schema()
{
  return schema_;
}

const std::string& HttpMessage::method()
{
  return _method;
}

uint16_t HttpMessage::status_code()
{
  return _status;
}

bool HttpMessage::is_http_get()
{
  return method() == "GET";
}

bool HttpMessage::is_http_put()
{
  return method() == "PUT";
}

bool HttpMessage::is_http_post()
{
  return method() == "POST";
}

bool HttpMessage::is_http_delete()
{
  return method() == "DELETE";
}

bool HttpMessage::is_http_options()
{
  return method() == "OPTIONS";
}

bool HttpMessage::is_chunked()
{
  return chunked;
}

bool HttpMessage::is_keep_alive()
{
  return _keep_alive;
}

std::string HttpMessage::uri()
{
  std::string uri = _uri->get_schema();
  if (uri.empty()) {
      uri += "http";
  }
  uri += "://";
  
  uri += host();
  uri += path();
  
  return uri;
}

std::string HttpMessage::url()
{
  return _uri->get_url();
}

std::string HttpMessage::host()
{
  auto it = _query.find("vhost");
  if (it != _query.end() && !it->second.empty()) {
      return it->second;
  }

  it = _query.find("domain");
  if (it != _query.end() && !it->second.empty()) {
      return it->second;
  }

  return _uri->get_host();
}

int HttpMessage::port()
{
  return _uri->get_port();
}

std::string HttpMessage::path()
{
  return _uri->get_path();
}

std::string HttpMessage::query()
{
  return _uri->get_query();
}

std::string HttpMessage::ext()
{
  return _ext;
}

std::string HttpMessage::parse_rest_id(std::string pattern)
{
  std::string p = _uri->get_path();
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

std::string HttpMessage::query_get(const std::string& key)
{
  std::string v;
  
  if (_query.find(key) != _query.end()) {
    v = _query[key];
  }
  
  return v;
}

SrsHttpHeader& HttpMessage::header()
{
  return _header;
}

const std::string& HttpMessage::get_body()
{
  return _body;
}

int64_t HttpMessage::content_length()
{
  return _content_length;
}


}

