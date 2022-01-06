//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef SRS_PROTOCOL_UTILITY_HPP
#define SRS_PROTOCOL_UTILITY_HPP

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

#include <string>
#include <vector>
#include <map>
#include <sstream>

#include "http/http_consts.h"

namespace ma {

void srs_parse_hostport(std::string_view hostport, 
                        std::string_view& host, 
                        int& port);

// schema://host:port
void split_schema_host_port(std::string_view addr, 
                            std::string_view& schema, 
                            std::string_view& host, int&);

bool srs_bytes_equals(void* pa, void* pb, int size);

std::string srs_string_replace(const std::string& str, 
                               const std::string& old_str, 
                               const std::string& new_str);

std::vector<std::string> srs_string_split(
    const std::string& s, const std::string& seperator);

std::vector<std::string> srs_string_split(
    const std::string& str, const std::vector<std::string>& seperators);


// parse query string to map(k,v).
// must format as key=value&...&keyN=valueN
void srs_parse_query_string(const std::string& q, 
    std::map<std::string, std::string>& query);

// get the stream identify, vhost/app/stream.
std::string srs_generate_stream_url(const std::string& vhost, 
                                    const std::string& app, 
                                    const std::string& stream);

// parse the rtmp url to tcUrl/stream,
// for example, rtmp://v.ossrs.net/live/livestream to
//      tcUrl: rtmp://v.ossrs.net/live
//      stream: livestream
void srs_parse_rtmp_url(const std::string& url, 
                        std::string& tcUrl, 
                        std::string& stream);

std::string srs_string_remove(std::string str, 
                              std::string remove_chars);

std::string srs_string_trim_start(std::string str, std::string trim_chars);
std::string srs_string_trim_end(std::string str, std::string trim_chars);

bool srs_string_starts_with(const std::string& str, const std::string&);
bool srs_string_ends_with(const std::string& str, const std::string& flag);

bool srs_path_exists(const std::string& path);

std::string srs_path_filext(const std::string& path);


void srs_discovery_tc_url(const std::string& tcUrl, 
                          std::string& schema, 
                          std::string& host, 
                          std::string& vhost, 
                          std::string& app, 
                          std::string& stream, 
                          int& port, 
                          std::string& param);

std::string srs_path_filename(const std::string_view path);

// join string in vector with indicated separator
template <typename T>
std::string srs_join_vector_string(const std::vector<T>& vs, 
                                   std::string separator) {
  std::stringstream ss;

  for (int i = 0; i < (int)vs.size(); i++) {
    ss << vs.at(i);
    if (i != (int)vs.size() - 1) {
        ss << separator;
    }
  }

  return ss.str();
}

// Whether domain is an IPv4 address.
bool srs_is_ipv4(std::string domain);

std::string srs_int2str(int64_t value);

}
#endif

