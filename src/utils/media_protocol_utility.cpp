//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include "utils/media_protocol_utility.h"

#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sstream>
#include <string_view>
#include <random>
#include <sys/time.h>

#include "common/media_consts.h"
#include "common/media_log.h"
#include "rtmp/media_rtmp_const.h"

using namespace std;

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.utils");

void srs_parse_hostport(std::string_view hostport, 
    std::string_view& host, int& port) {
  // No host or port.
  if (hostport.empty()) {
      return;
  }

  size_t pos = std::string_view::npos;

  // Host only for ipv4.
  if ((pos = hostport.rfind(":")) == std::string_view::npos) {
      host = hostport;
      return;
  }

  // For ipv4(only one colon), host:port.
  if (hostport.find(":") == pos) {
      host = hostport.substr(0, pos);
      std::string_view p = hostport.substr(pos + 1);
      if (!p.empty()) {
          port = ::atoi(p.data());
      }
      return;
  }

  // Host only for ipv6.
  if (hostport.at(0) != '[' || (pos = hostport.rfind("]:")) == std::string_view::npos) {
      host = hostport;
      return;
  }

  // For ipv6, [host]:port.
  host = hostport.substr(1, pos - 1);
  std::string_view p = hostport.substr(pos + 2);
  if (!p.empty()) {
      port = ::atoi(p.data());
  }
}

void split_schema_host_port(std::string_view addr, 
    std::string_view& schema, std::string_view& host, int& port) {
  size_t pos = addr.find("://");
  if (pos != std::string_view::npos) {
    schema = addr.substr(0, pos);
  }

  srs_parse_hostport(addr.substr(pos+3), host, port);
}


//why not use memcmp
bool srs_bytes_equals(void* pa, void* pb, int size) {
  uint8_t* a = (uint8_t*)pa;
  uint8_t* b = (uint8_t*)pb;
  
  if (!a && !b) {
    return true;
  }
  
  if (!a || !b) {
    return false;
  }
  
  for(int i = 0; i < size; i++){
    if(a[i] != b[i]){
      return false;
    }
  }
  
  return true;
}


string srs_string_replace(string_view str, 
                          const string& old_str, 
                          const string& new_str) {
  std::string ret(str.data(), str.length());
  
  if (old_str == new_str) {
    return std::move(ret);
  }
  
  size_t pos = 0;
  while ((pos = ret.find(old_str, pos)) != std::string::npos) {
    ret = ret.replace(pos, old_str.length(), new_str);
  }
  
  return std::move(ret);
}

vector<string> srs_string_split(const string& s, const string& seperator) {
  vector<string> result;
  if(seperator.empty()){
      result.push_back(s);
      return result;
  }
  
  size_t posBegin = 0;
  size_t posSeperator = s.find(seperator);
  while (posSeperator != string::npos) {
      result.push_back(s.substr(posBegin, posSeperator - posBegin));
      posBegin = posSeperator + seperator.length(); // next byte of seperator
      posSeperator = s.find(seperator, posBegin);
  }
  // push the last element
  result.push_back(s.substr(posBegin));
  return result;
}

string srs_string_min_match(const string& str, const vector<string>& seperators) {
  string match;
  
  if (seperators.empty()) {
    return str;
  }
  
  size_t min_pos = string::npos;
  for (auto it = seperators.begin(); it != seperators.end(); ++it) {
    string seperator = *it;
    
    size_t pos = str.find(seperator);
    if (pos == string::npos) {
        continue;
    }
    
    if (min_pos == string::npos || pos < min_pos) {
        min_pos = pos;
        match = seperator;
    }
  }
  
  return match;
}

vector<string> srs_string_split(const string& str, const vector<string>& seperators) {
  vector<string> arr;
  
  size_t pos = string::npos;
  string s = str;
  
  while (true) {
    string seperator = srs_string_min_match(s, seperators);
    if (seperator.empty()) {
      break;
    }
    
    if ((pos = s.find(seperator)) == string::npos) {
      break;
    }

    arr.push_back(s.substr(0, pos));
    s = s.substr(pos + seperator.length());
  }
  
  if (!s.empty()) {
    arr.push_back(s);
  }
  
  return arr;
}

void srs_parse_query_string(const string& q, std::map<string,string>& query) {
  // query string flags.
  static vector<string> flags;
  if (flags.empty()) {
    flags.push_back("=");
    flags.push_back(",");
    flags.push_back("&&");
    flags.push_back("&");
    flags.push_back(";");
  }
  
  vector<string> kvs = srs_string_split(q, flags);
  for (int i = 0; i < (int)kvs.size(); i+=2) {
    string k = kvs.at(i);
    string v = (i < (int)kvs.size() - 1)? kvs.at(i+1):"";
    
    query[k] = v;
  }
}

string srs_generate_stream_url(const string& vhost, const string& app, const string& stream) {
  std::string url = "";
  
  if (SRS_CONSTS_RTMP_DEFAULT_VHOST != vhost){
      url += vhost;
  }
  url += "/";
  url += app;
  url += "/";
  url += stream;
  
  return url;
}

void srs_parse_rtmp_url(const string& url, string& tcUrl, string& stream) {
  size_t pos;
  
  if ((pos = url.rfind("/")) != string::npos) {
    stream = url.substr(pos + 1);
    tcUrl = url.substr(0, pos);
  } else {
    tcUrl = url;
  }
}

bool srs_is_ipv4(const string& domain) {
  for (int i = 0; i < (int)domain.length(); i++) {
    char ch = domain.at(i);
    if (ch == '.') {
        continue;
    }
    if (ch >= '0' && ch <= '9') {
        continue;
    }
    
    return false;
  }
  
  return true;
}

std::string srs_int2str(int64_t value) {
    // len(max int64_t) is 20, plus one "+-."
    char tmp[22];
    snprintf(tmp, 22, "%" PRId64, value);
    return tmp;
}

string srs_string_remove(string str, string remove_chars) {
  std::string ret = str;
  
  for (int i = 0; i < (int)remove_chars.length(); i++) {
    char ch = remove_chars.at(i);
    
    for (std::string::iterator it = ret.begin(); it != ret.end();) {
      if (ch == *it) {
        it = ret.erase(it);
        
        // ok, matched, should reset the search
        i = -1;
      } else {
        ++it;
      }
    }
  }
  
  return ret;
}

string srs_erase_last_substr(string str, string erase_string) {
	std::string ret = str;

	size_t pos = ret.rfind(erase_string);

	if (pos != std::string::npos) {
		ret.erase(pos, erase_string.length());
	}
    
	return ret;
}

string srs_string_trim_end(string str, string trim_chars) {
    std::string ret = str;
    
    for (int i = 0; i < (int)trim_chars.length(); i++) {
        char ch = trim_chars.at(i);
        
        while (!ret.empty() && ret.at(ret.length() - 1) == ch) {
            ret.erase(ret.end() - 1);
            
            // ok, matched, should reset the search
            i = -1;
        }
    }
    
    return ret;
}

string srs_string_trim_start(string str, string trim_chars) {
    std::string ret = str;
    
    for (int i = 0; i < (int)trim_chars.length(); i++) {
        char ch = trim_chars.at(i);
        
        while (!ret.empty() && ret.at(0) == ch) {
            ret.erase(ret.begin());
            
            // ok, matched, should reset the search
            i = -1;
        }
    }
    
    return ret;
}

bool srs_string_starts_with(
    const std::string& str, const std::string& flag) {
  return str.find(flag) == 0;
}

bool srs_string_starts_with(const std::string& str, 
    const std::string& flag0, const std::string& flag1) {
  return srs_string_starts_with(str, flag0) || 
      srs_string_starts_with(str, flag1);
}

bool srs_string_starts_with(const std::string& str, const std::string& flag0,
    const std::string& flag1, const std::string& flag2) {
  return srs_string_starts_with(str, flag0, flag1) || 
      srs_string_starts_with(str, flag2);
}

bool srs_string_starts_with(const std::string& str, 
    const std::string& flag0, 
    const std::string& flag1, 
    const std::string& flag2, 
    const std::string& flag3) {
  return srs_string_starts_with(str, flag0, flag1, flag2) ||
      srs_string_starts_with(str, flag3);
}

bool srs_string_ends_with(
    const std::string& str, const std::string& flag) {
  const size_t pos = str.rfind(flag);
  return (pos != string::npos) && (pos == str.length() - flag.length());
}

bool srs_path_exists(const std::string& path) {
  struct stat st;
  
  // stat current dir, if exists, return error.
  if (stat(path.c_str(), &st) == 0) {
      return true;
  }
  
  return false;
}

std::string srs_path_filext(const std::string& path) {
  size_t pos = std::string::npos;
  
  if ((pos = path.rfind(".")) != std::string::npos) {
    return path.substr(pos);
  }
  
  return "";
}


/**
 * resolve the vhost in query string
 * @pram vhost, update the vhost if query contains the vhost.
 * @param app, may contains the vhost in query string format:
 *   app?vhost=request_vhost
 *   app...vhost...request_vhost
 * @param param, the query, for example, ?vhost=xxx
 */
void srs_vhost_resolve(string& vhost, string& app, string& param) {
  // get original param
  size_t pos = 0;
  if ((pos = app.find("?")) != std::string::npos) {
    param = app.substr(pos);
  }
  
  // filter tcUrl
  app = srs_string_replace(app, ",", "?");
  app = srs_string_replace(app, "...", "?");
  app = srs_string_replace(app, "&&", "?");
  app = srs_string_replace(app, "&", "?");
  app = srs_string_replace(app, "=", "?");
  
  if (srs_string_ends_with(app, "/_definst_")) {
    app = srs_erase_last_substr(app, "/_definst_");
  }
  
  if ((pos = app.find("?")) != std::string::npos) {
    std::string query = app.substr(pos + 1);
    app = app.substr(0, pos);
    
    if ((pos = query.find("vhost?")) != std::string::npos) {
      query = query.substr(pos + 6);
      if (!query.empty()) {
        vhost = query;
      }
    }
  }

  // vhost with params.
  if ((pos = vhost.find("?")) != std::string::npos) {
    vhost = vhost.substr(0, pos);
  }
  
  /* others */
}

void srs_discovery_tc_url(const std::string& tcUrl, 
                          std::string& schema, 
                          std::string& host, 
                          std::string& vhost, 
                          std::string& app, 
                          std::string& stream, 
                          int& port, 
                          std::string& param) {
  size_t pos = std::string_view::npos;
  std::string_view url{tcUrl};
  
  if ((pos = url.find("://")) != std::string_view::npos) {
    schema = url.substr(0, pos);
    url = url.substr(schema.length() + 3);
    MLOG_DEBUG("discovery schema=" << schema);
  }
  
  if ((pos = url.find("/")) != std::string_view::npos) {
    host = url.substr(0, pos);
    url = url.substr(host.length() + 1);
    MLOG_DEBUG("discovery host=" << host.c_str());
  }
  
  port = SRS_CONSTS_RTMP_DEFAULT_PORT;
  if ((pos = host.find(":")) != std::string::npos) {
    std::string_view host_view;
    srs_parse_hostport(host, host_view, port);
    host = host_view;
    MLOG_CDEBUG("discovery host=%s, port=%d", host.c_str(), port);
  }
  
  if (url.empty()) {
    app = SRS_CONSTS_RTMP_DEFAULT_APP;
  } else {
    app = url;
  }
  
  vhost = host;
  srs_vhost_resolve(vhost, app, param);
  srs_vhost_resolve(vhost, stream, param);
  
  // Ignore when the param only contains the default vhost.
  if (param == "?vhost=" SRS_CONSTS_RTMP_DEFAULT_VHOST) {
    param = "";
  }
}

std::string srs_path_filename(std::string_view path) {
  size_t pos = path.rfind(".");
  if (pos != std::string_view::npos) {
    path = path.substr(0, pos);
  }
  
  return std::move(std::string{path.data(), path.length()});
}

std::string srs_path_dirname(const std::string& path) {
  std::string dirname = path;

  // No slash, it must be current dir.
  size_t pos = string::npos;
  if ((pos = dirname.rfind("/")) == string::npos) {
    return "./";
  }

  // Path under root.
  if (pos == 0) {
    return "/";
  }

  // Fetch the directory.
  dirname = dirname.substr(0, pos);
  return dirname;
}

void srs_random_generate(char* bytes, int size) {
  for (int i = 0; i < size; i++) {
    // the common value in [0x0f, 0xf0]
    bytes[i] = 0x0f + (srs_random() % (256 - 0x0f - 0x0f));
  }
}

int64_t get_system_time() {
  timeval now;
  ::gettimeofday(&now, NULL);
  return ((int64_t)now.tv_sec) * 1000 * 1000 + (int64_t)now.tv_usec;
}

std::string srs_random_str(int len) {
  static string random_table = 
"01234567890123456789012345678901234567890123456789abcdefghijklmnopqrstuvwxyz";

  string ret;
  ret.reserve(len);
  for (int i = 0; i < len; ++i) {
    ret.append(1, random_table[srs_random() % random_table.size()]);
  }

  return ret;
}

long srs_random() {
  static bool _random_initialized = false;
  if (!_random_initialized) {
    _random_initialized = true;
    ::srandom((unsigned long)(get_system_time() | (::getpid()<<13)));
  }

  return random();
}

int srs_chunk_header_c0(int perfer_cid, uint32_t timestamp, 
  int32_t payload_length, int8_t message_type, 
  int32_t stream_id, char* cache, int nb_cache) {
  // to directly set the field.
  char* pp = NULL;
  
  // generate the header.
  char* p = cache;
  
  // no header.
  if (nb_cache < SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
    return 0;
  }
  
  // write new chunk stream header, fmt is 0
  *p++ = 0x00 | (perfer_cid & 0x3F);
  
  // chunk message header, 11 bytes
  // timestamp, 3bytes, big-endian
  if (timestamp < RTMP_EXTENDED_TIMESTAMP) {
    pp = (char*)&timestamp;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
  } else {
    *p++ = (char)0xFF;
    *p++ = (char)0xFF;
    *p++ = (char)0xFF;
  }
  
  // message_length, 3bytes, big-endian
  pp = (char*)&payload_length;
  *p++ = pp[2];
  *p++ = pp[1];
  *p++ = pp[0];
  
  // message_type, 1bytes
  *p++ = message_type;
  
  // stream_id, 4bytes, little-endian
  pp = (char*)&stream_id;
  *p++ = pp[0];
  *p++ = pp[1];
  *p++ = pp[2];
  *p++ = pp[3];
  
  // for c0
  // chunk extended timestamp header, 0 or 4 bytes, big-endian
  //
  // for c3:
  // chunk extended timestamp header, 0 or 4 bytes, big-endian
  // 6.1.3. Extended Timestamp
  // This field is transmitted only when the normal time stamp in the
  // chunk message header is set to 0x00ffffff. If normal time stamp is
  // set to any value less than 0x00ffffff, this field MUST NOT be
  // present. This field MUST NOT be present if the timestamp field is not
  // present. Type 3 chunks MUST NOT have this field.
  // adobe changed for Type3 chunk:
  //        FMLE always sendout the extended-timestamp,
  //        must send the extended-timestamp to FMS,
  //        must send the extended-timestamp to flash-player.
  // @see: ngx_rtmp_prepare_message
  // @see: http://blog.csdn.net/win_lin/article/details/13363699
  // TODO: FIXME: extract to outer.
  if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
    pp = (char*)&timestamp;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
  }
  
  // always has header
  return (int)(p - cache);
}

int srs_chunk_header_c3(int perfer_cid, uint32_t timestamp, 
  char* cache, int nb_cache) {
  // to directly set the field.
  char* pp = NULL;
  
  // generate the header.
  char* p = cache;
  
  // no header.
  if (nb_cache < SRS_CONSTS_RTMP_MAX_FMT3_HEADER_SIZE) {
    return 0;
  }
  
  // write no message header chunk stream, fmt is 3
  // @remark, if perfer_cid > 0x3F, that is, use 2B/3B chunk header,
  // SRS will rollback to 1B chunk header.
  *p++ = 0xC0 | (perfer_cid & 0x3F);
  
  // for c0
  // chunk extended timestamp header, 0 or 4 bytes, big-endian
  //
  // for c3:
  // chunk extended timestamp header, 0 or 4 bytes, big-endian
  // 6.1.3. Extended Timestamp
  // This field is transmitted only when the normal time stamp in the
  // chunk message header is set to 0x00ffffff. If normal time stamp is
  // set to any value less than 0x00ffffff, this field MUST NOT be
  // present. This field MUST NOT be present if the timestamp field is not
  // present. Type 3 chunks MUST NOT have this field.
  // adobe changed for Type3 chunk:
  //        FMLE always sendout the extended-timestamp,
  //        must send the extended-timestamp to FMS,
  //        must send the extended-timestamp to flash-player.
  // @see: ngx_rtmp_prepare_message
  // @see: http://blog.csdn.net/win_lin/article/details/13363699
  // TODO: FIXME: extract to outer.
  if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
    pp = (char*)&timestamp;
    *p++ = pp[3];
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
  }
  
  // always has header
  return (int)(p - cache);
}

} //namespace ma
