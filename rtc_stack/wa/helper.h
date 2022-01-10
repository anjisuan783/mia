//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WA_HELPER_H__
#define __WA_HELPER_H__

#include <string>
#include <vector>

namespace wa {

std::string wa_string_replace(const std::string& str, 
                              const std::string& old_str, const std::string& new_str);

std::vector<std::string> wa_split_str(const std::string& str, const std::string& delim);

}

#endif //!__WA_HELPER_H__

