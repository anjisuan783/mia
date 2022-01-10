//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "helper.h"

namespace wa {

std::string wa_string_replace(const std::string& str, 
                              const std::string& old_str, 
                              const std::string& new_str) {
  std::string ret = str;
  
  if (old_str == new_str) {
      return ret;
  }
  
  size_t pos = 0;
  while ((pos = ret.find(old_str, pos)) != std::string::npos) {
      ret = ret.replace(pos, old_str.length(), new_str);
  }
  
  return ret;
}

std::vector<std::string> wa_split_str(const std::string& str, const std::string& delim) {
  std::vector<std::string> ret;
  size_t pre_pos = 0;
  std::string tmp;
  size_t pos = 0;
  do {
      pos = str.find(delim, pre_pos);
      tmp = str.substr(pre_pos, pos - pre_pos);
      ret.push_back(tmp);
      pre_pos = pos + delim.size();
  } while (pos != std::string::npos);

  return ret;
}

} //namespace wa

