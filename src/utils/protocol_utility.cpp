//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#include "utils/protocol_utility.h"

#include <inttypes.h>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include <sstream>
#include <string_view>

#include "http/http_consts.h"
#include "common/media_log.h"

using namespace std;

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("protocol_utility");

void srs_parse_hostport(std::string_view hostport, std::string_view& host, int& port) {
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


string srs_string_replace(const string& str, 
                          const string& old_str, 
                          const string& new_str) {
  std::string ret = str;
  
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

bool srs_string_ends_with(string str, string flag) {
  const size_t pos = str.rfind(flag);
  return (pos != string::npos) && (pos == str.length() - flag.length());
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

}

