#include "media_config.h"
#include "wa_rtp_define.h"

namespace wa {

const std::vector<std::string> extMappings {
  {"urn:ietf:params:rtp-hdrext:ssrc-audio-level"},
  {"http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"},
#ifdef WA_ENABLE_SDESMID
  "urn:ietf:params:rtp-hdrext:sdes:mid",
#endif
/*
  "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id",
  "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id",
  "urn:ietf:params:rtp-hdrext:toffset",
  "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
  "urn:3gpp:video-orientation",
  "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"
*/
};

const std::map<std::string, int> extMappings2Id = {
  {"urn:ietf:params:rtp-hdrext:ssrc-audio-level", EExtmap::AudioLevel},
  {
    "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01", 
    EExtmap::TransportCC
  },
#ifdef WA_ENABLE_SDESMID
  {"urn:ietf:params:rtp-hdrext:sdes:mid", EExtmap::SdesMid},
#endif
/*
  {"urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id", EExtmap::SdesRtpStreamId},
  {
    "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id", 
    EExtmap::SdesRepairedRtpStreamId
  }, 
  {"urn:ietf:params:rtp-hdrext:toffset", EExtmap::Toffset},  
  {
    "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
    EExtmap::AbsSendTime
  },
  {"urn:3gpp:video-orientation", EExtmap::videoOrientation},
  {
    "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay", 
    EExtmap::PlayoutDelay
  }
*/
};

erizo::RtpMap rtpH264{
  H264_90000_PT,
  "H264",
  90000,
  erizo::MediaType::VIDEO_TYPE,
  1,
  {"ccm fir", "nack", "transport-cc", "goog-remb"},
  {}
};

erizo::RtpMap rtpRed{
  RED_90000_PT,
  "red",
  90000,
  erizo::MediaType::VIDEO_TYPE,
  1,
  {},
  {}
};

erizo::RtpMap rtpRtx{
  RTX_90000_PT,
  "rtx",
  90000,
  erizo::MediaType::VIDEO_TYPE,
  1,
  {},
  {}
};

erizo::RtpMap rtpUlpfec{
  ULP_90000_PT,
  "ulpfec",
  90000,
  erizo::MediaType::VIDEO_TYPE,
  1,
  {},
  {}
};

erizo::RtpMap rtpOpus{
  OPUS_48000_PT,
  "opus",
  48000,
  erizo::MediaType::AUDIO_TYPE,
  2,
  {"transport-cc"},
  {}
};

} //namespace wa

