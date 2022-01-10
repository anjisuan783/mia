#ifndef __WA_RTP_DEFINE_H__
#define __WA_RTP_DEFINE_H__

#define RTCP_MIN_PT         194  // per https://tools.ietf.org/html/rfc5761
#define RTCP_MAX_PT         223

#define RTCP_AUDIO_INTERVAL  5000
#define RTCP_VIDEO_INTERVAL  1000

// Payload types
#define RTCP_Sender_PT         200 // RTCP Sender Report
#define RTCP_Receiver_PT       201 // RTCP Receiver Report
#define RTCP_SDES_PT           202
#define RTCP_BYE               203
#define RTCP_APP               204
#define RTCP_RTP_Feedback_PT   205 // RTCP Transport Layer Feedback Packet
#define RTCP_PS_Feedback_PT    206 // RTCP Payload Specific Feedback Packet
#define RTCP_XR_PT             207 // RTCP Payload Specific Feedback Packet


#define RTCP_PLI_FMT        1
#define RTCP_SLI_FMT        2
#define RTCP_FIR_FMT        4
#define RTCP_AFB            15

#define RTX_90000_PT        96  // RTX packet
#define VP8_90000_PT        100 // VP8 Video Codec
#define VP9_90000_PT        101 // VP9 Video Codec
#define H264_90000_PT       127 // H264 Video Codec
#define H265_90000_PT       121 // H265 Video Codec
#define RED_90000_PT        116 // REDundancy (RFC 2198)
#define ULP_90000_PT        117 // ULP/FEC
#define ISAC_16000_PT       103 // ISAC Audio Codec
#define ISAC_32000_PT       104 // ISAC Audio Codec
#define PCMU_8000_PT        0   // PCMU Audio Codec
#define OPUS_48000_PT       120 // Opus Audio Codec
#define PCMA_8000_PT        8   // PCMA Audio Codec
#define CN_8000_PT          13  // CN Audio Codec
#define CN_16000_PT         105 // CN Audio Codec
#define CN_32000_PT         106 // CN Audio Codec
#define CN_48000_PT         107 // CN Audio Codec
#define TEL_8000_PT         126 // Tel Audio Events
#define ILBC_8000_PT        102 // ILBC Audio Codec
#define G722_16000_1_PT     9   // G722 Mono Audio Codec
#define G722_16000_2_PT     119 // G722 Stereo Audio Codec

#define L16_48000_PT        83  // PCM 48khz Stereo

#define INVALID_PT          -1  // Not a valid PT

#endif //!__WA_RTP_DEFINE_H__

