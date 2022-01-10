#include "owt_base/AudioUtilitiesNew.h"

#include <vector>
#include "common/rtputils.h"
#include "owt_base/MediaFramePipeline.h"

#define __codecIns_OPTIMIZE__

namespace owt_base {

struct AudioCodecInsMap {
  FrameFormat format;
  CodecInst codec;
};

static std::vector<int> codecInsDB_idx {
  -1, //FRAME_FORMAT_UNKNOWN
  -1, //FRAME_FORMAT_I420
  -1, //FRAME_FORMAT_VP8
  -1, //FRAME_FORMAT_VP9
  -1, //FRAME_FORMAT_H264
  -1, //FRAME_FORMAT_H265
  -1, //FRAME_FORMAT_MSDK
  5, //FRAME_FORMAT_PCM_48000_2
  0, //FRAME_FORMAT_PCMU
  1, //FRAME_FORMAT_PCMA
  4, //FRAME_FORMAT_OPUS
  2, //FRAME_FORMAT_ISAC16
  3, //FRAME_FORMAT_ISAC32
  6, //FRAME_FORMAT_ILBC
  7, //FRAME_FORMAT_G722_16000_1
  8, //FRAME_FORMAT_G722_16000_2
  -1, //FRAME_FORMAT_AAC
  -1, //FRAME_FORMAT_AAC_48000_2
  -1, //FRAME_FORMAT_AC3
  -1, //FRAME_FORMAT_NELLYMOSER
  -1, //FRAME_FORMAT_DATA
  -1 //FRAME_FORMAT_MAX
};

static const AudioCodecInsMap codecInsDB[] = {
  {
    FRAME_FORMAT_PCMU,
    {
      PCMU_8000_PT,
      "PCMU",
      8000,
      160,
      1,
      64000
    }
  },
  {
    FRAME_FORMAT_PCMA,
    {
      PCMA_8000_PT,
      "PCMA",
      8000,
      160,
      1,
      64000
    }
  },
  {
    FRAME_FORMAT_ISAC16,
    {
      ISAC_16000_PT,
      "ISAC",
      16000,
      480,
      1,
      32000
    }
  },
  {
    FRAME_FORMAT_ISAC32,
    {
      ISAC_32000_PT,
      "ISAC",
      32000,
      960,
      1,
      56000
    }
  },
  {
    FRAME_FORMAT_OPUS,
    {
      OPUS_48000_PT,
      "opus",
      48000,
      960,
      2,
      64000
    }
  },
  {
    FRAME_FORMAT_PCM_48000_2,
    {
      L16_48000_PT,
      "L16",
      48000,
      480,
      2,
      768000
    }
  },
  {
    FRAME_FORMAT_ILBC,
    {
      ILBC_8000_PT,
      "ILBC",
      8000,
      240,
      1,
      13300
    }
  },
  {
    FRAME_FORMAT_G722_16000_1,
    {
      G722_16000_1_PT,
      "G722",
      16000,
      320,
      1,
      64000
    }
  },
  {
    FRAME_FORMAT_G722_16000_2,
    {
      G722_16000_2_PT,
      "G722",
      16000,
      320,
      2,
      64000
    }
  },
};

static const int numCodecIns = sizeof(codecInsDB) / sizeof(codecInsDB[0]);

bool getAudioCodecInst(FrameFormat format, CodecInst& audioCodec) {
#ifdef __codecIns_OPTIMIZE__
  int idx = codecInsDB_idx[format];
  if (idx != -1) {
    audioCodec = codecInsDB[idx].codec;
    return true;
  }
#else
  for (size_t i = 0; i < numCodecIns; i++) {
    if (codecInsDB[i].format == format) {
      audioCodec = codecInsDB[i].codec;
      return true;
    }
  }
#endif
  return false;
}

int getAudioPltype(FrameFormat format) {
#ifdef __codecIns_OPTIMIZE__
  int idx = codecInsDB_idx[format];
  if (idx != -1) {
    return codecInsDB[idx].codec.pltype;
  }
#else
  for (size_t i = 0; i < numCodecIns; i++) {
    if (codecInsDB[i].format == format) {
      return codecInsDB[i].codec.pltype;
    }
  }
#endif
  return INVALID_PT;
}

FrameFormat getAudioFrameFormat(int pltype) {
#ifdef __codecIns_OPTIMIZE__
  switch(pltype) {
    case OPUS_48000_PT:
      return FRAME_FORMAT_OPUS;
      break;
    default:
      break;
  }
#endif

  for (size_t i = 0; i < numCodecIns; i++) {
    if (codecInsDB[i].codec.pltype == pltype) {
      return codecInsDB[i].format;
    }
  }

  return FRAME_FORMAT_UNKNOWN;
}

int32_t getAudioSampleRate(const FrameFormat format) {
  if (format == FRAME_FORMAT_AAC_48000_2) {
    return 48000;
  }
#ifdef __codecIns_OPTIMIZE__
  int idx = codecInsDB_idx[format];
  if (idx != -1) {
    return codecInsDB[idx].codec.plfreq;
  }
#else
  for (size_t i = 0; i < numCodecIns; i++) {
    if (codecInsDB[i].format == format) {
      return codecInsDB[i].codec.plfreq;
    }
  }
#endif
  return 0;
}

uint32_t getAudioChannels(const FrameFormat format) {
  if (format == FRAME_FORMAT_AAC_48000_2) {
    return 2;
  }

#ifdef __codecIns_OPTIMIZE__
  int idx = codecInsDB_idx[format];
  if (idx != -1) {
    return codecInsDB[idx].codec.channels;
  }
#else
  for (size_t i = 0; i < numCodecIns; i++) {
    if (codecInsDB[i].format == format) {
      return codecInsDB[i].codec.channels;
    }
  }
#endif
  return 0;
}

} /* namespace owt_base */
