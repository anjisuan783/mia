#ifndef __MEDIA_RETURN_CODE_H__
#define __MEDIA_RETURN_CODE_H__

namespace ma {

typedef int MediaCode;

enum {
  kma_ok = 0,
  kma_already_initilized = 0,
  kma_unimplement,
  kma_url_parse_failed,
  kma_failure,
  kma_streamurl_error,
  kma_wrong_status,
  kma_listen_failed,
};

}

#endif //!__MEDIA_RETURN_CODE_H__

