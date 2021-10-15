//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//


#ifndef __MEDIA_RETURN_CODE_H__
#define __MEDIA_RETURN_CODE_H__

namespace ma {

typedef int MediaCode;

enum {
  kma_ok = 0,
  kma_already_initilized = 1,
  kma_unimplement = 2,
  kma_url_parse_failed = 3,
  kma_failure = 4,
  kma_streamurl_error = 5,
  kma_wrong_status = 6,
  kma_listen_failed = 7,
  kma_invalid_argument = 8,
};

}

#endif //!__MEDIA_RETURN_CODE_H__

