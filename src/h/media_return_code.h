#ifndef __MEDIA_RETURN_CODE_H__
#define __MEDIA_RETURN_CODE_H__

namespace ma {

typedef int GsRtcCode;

enum {
  kRtc_ok = 0,
  kRtc_already_initilized = 0,
  kRtc_unimplement,
  kRtc_url_parse_failed,
  kRtc_failure,
  kRtc_streamurl_error,
  kRtc_wrong_status,
};

}

#endif //!__MEDIA_RETURN_CODE_H__

