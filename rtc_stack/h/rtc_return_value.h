//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WA_RTC_RETURN_VALUE_H__
#define __WA_RTC_RETURN_VALUE_H__

namespace wa {

enum {
  wa_ok = 0,
  wa_failed = -1,
  wa_e_invalid_param = -2,
  wa_e_already_initialized = -3,
  wa_e_parse_offer_failed = -4,
  wa_e_found = -5,
  wa_e_not_found = -6,
};

}

#endif //!__WA_RTC_RETURN_VALUE_H__
