//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#include "encoder/media_codec.h"

#include "common/media_log.h"
#include "utils/media_kernel_buffer.h"
#include "encoder/media_codec.h"
#include "utils/media_msg_chain.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.encoder");

std::string srs_audio_codec_id2str(SrsAudioCodecId codec) {
  switch (codec) {
    case SrsAudioCodecIdAAC:
      return "AAC";
    case SrsAudioCodecIdMP3:
      return "MP3";
    case SrsAudioCodecIdOpus:
      return "Opus";
    case SrsAudioCodecIdReserved1:
    case SrsAudioCodecIdLinearPCMPlatformEndian:
    case SrsAudioCodecIdADPCM:
    case SrsAudioCodecIdLinearPCMLittleEndian:
    case SrsAudioCodecIdNellymoser16kHzMono:
    case SrsAudioCodecIdNellymoser8kHzMono:
    case SrsAudioCodecIdNellymoser:
    case SrsAudioCodecIdReservedG711AlawLogarithmicPCM:
    case SrsAudioCodecIdReservedG711MuLawLogarithmicPCM:
    case SrsAudioCodecIdReserved:
    case SrsAudioCodecIdSpeex:
    case SrsAudioCodecIdReservedMP3_8kHz:
    case SrsAudioCodecIdReservedDeviceSpecificSound:
    default:
      return "Other";
  }
}


srs_error_t srs_avc_nalu_read_bit(SrsBitBuffer* stream, int8_t& v) {
  srs_error_t err = srs_success;
  
  if (stream->empty()) {
    return srs_error_new(ERROR_AVC_NALU_UEV, "empty stream");
  }
  
  v = stream->read_bit();
  
  return err;
}


srs_error_t srs_avc_nalu_read_uev(SrsBitBuffer* stream, int32_t& v) {
  srs_error_t err = srs_success;
  
  if (stream->empty()) {
    return srs_error_new(ERROR_AVC_NALU_UEV, "empty stream");
  }
  
  // ue(v) in 9.1 Parsing process for Exp-Golomb codes
  // ISO_IEC_14496-10-AVC-2012.pdf, page 227.
  // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
  //      leadingZeroBits = -1;
  //      for( b = 0; !b; leadingZeroBits++ )
  //          b = read_bits( 1 )
  // The variable codeNum is then assigned as follows:
  //      codeNum = (2<<leadingZeroBits) - 1 + read_bits( leadingZeroBits )
  int leadingZeroBits = -1;
  for (int8_t b = 0; !b && !stream->empty(); leadingZeroBits++) {
    b = stream->read_bit();
  }
  
  if (leadingZeroBits >= 31) {
    return srs_error_new(ERROR_AVC_NALU_UEV, 
        "%dbits overflow 31bits", leadingZeroBits);
  }
  
  v = (1 << leadingZeroBits) - 1;
  for (int i = 0; i < (int)leadingZeroBits; i++) {
    if (stream->empty()) {
      return srs_error_new(ERROR_AVC_NALU_UEV, 
          "no bytes for leadingZeroBits=%d", leadingZeroBits);
    }
    
    int32_t b = stream->read_bit();
    v += b << (leadingZeroBits - 1 - i);
  }
  
  return err;
}


bool srs_avc_startswith_annexb(SrsBuffer* stream, int* pnb_start_code) {
  if (!stream) {
    return false;
  }

  char* bytes = stream->data() + stream->pos();
  char* p = bytes;

  while(true) {
    if (!stream->require((int)(p - bytes + 3))) {
      return false;
    }
    
    // not match
    if (p[0] != (char)0x00 || p[1] != (char)0x00) {
      return false;
    }
    
    // match N[00] 00 00 01, where N>=0
    if (p[2] == (char)0x01) {
      if (pnb_start_code) {
        *pnb_start_code = (int)(p - bytes) + 3;
      }
      return true;
    }
    
    p++;
  }

  return false;
}

// MediaFlvVideo
bool MediaFlvVideo::Keyframe(MessageChain& mc) {
  char frame_type;
  int ret = mc.Peek(&frame_type, 1);
  if (MessageChain::error_part_data == ret) {
    MA_ASSERT(false);
    return false;
  }
  frame_type = (frame_type >> 4) & 0x0F;
  return frame_type == SrsVideoAvcFrameTypeKeyFrame;
}

bool MediaFlvVideo::Sh(MessageChain& mc) {
  // sequence header only for h264
  if (!H264(mc)) {
    return false;
  }
  
  // 2bytes required.
  char data[2];
  int ret = mc.Peek(data, 2);
  if (MessageChain::error_part_data == ret) {
    MA_ASSERT(false);
    return false;
  }
  
  char frame_type = data[0];
  frame_type = (frame_type >> 4) & 0x0F;
  
  char avc_packet_type = data[1];
  
  return frame_type == SrsVideoAvcFrameTypeKeyFrame
      && avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader;
}

bool MediaFlvVideo::H264(MessageChain& mc) {
  // 1bytes required  
  char codec_id;
  int ret = mc.Peek(&codec_id, 1);
  if (MessageChain::error_part_data == ret) {
    MA_ASSERT(false);
    return false;
  }
  codec_id = codec_id & 0x0F;
  
  return codec_id == SrsVideoCodecIdAVC;
}

bool MediaFlvVideo::Acceptable(MessageChain& mc) {
  // 1bytes required
  char frame_type;
  int ret = mc.Peek(&frame_type, 1);
  if (MessageChain::error_part_data == ret) {
    MA_ASSERT(false);
    return false;
  }

  char codec_id = frame_type & 0x0f;
  frame_type = (frame_type >> 4) & 0x0f;
  
  if (frame_type < 1 || frame_type > 5) {
    return false;
  }
  
  if (codec_id < 2 || codec_id > 7) {
    return false;
  }
  
  return true;
}

bool MediaFlvAudio::Sh(MessageChain& mc) {
  // 2bytes required.
  char data[2];
  int ret = mc.Peek(data, 2);
  if (MessageChain::error_part_data == ret) {
    MA_ASSERT(false);
    return false;
  }

  // sequence header only for aac
  if (!Aac(data[0])) {
    return false;
  }
  
  char aac_packet_type = data[1];
  return aac_packet_type == SrsAudioAacFrameTraitSequenceHeader;
}

bool MediaFlvAudio::Aac(char sound_format) {
  sound_format = (sound_format >> 4) & 0x0F;
  return sound_format == SrsAudioCodecIdAAC;
}

// the sample rates in the codec,
// in the sequence header.
static int srs_aac_srates[] = {
  96000, 88200, 64000, 48000,
  44100, 32000, 24000, 22050,
  16000, 12000, 11025,  8000,
  7350,     0,     0,    0
};

int GetAacSampleRate(uint8_t index) {
  return srs_aac_srates[index];
}

std::string srs_audio_sample_bits2str(SrsAudioSampleBits v) {
  switch (v) {
    case SrsAudioSampleBits16bit: return "16bits";
    case SrsAudioSampleBits8bit: return "8bits";
    default: return "Other";
  }
}

std::string srs_audio_channels2str(SrsAudioChannels v) {
  switch (v) {
    case SrsAudioChannelsStereo: return "Stereo";
    case SrsAudioChannelsMono: return "Mono";
    default: return "Other";
  }
}

std::string srs_avc_nalu2str(SrsAvcNaluType nalu_type) {
  switch (nalu_type) {
    case SrsAvcNaluTypeNonIDR: return "NonIDR";
    case SrsAvcNaluTypeDataPartitionA: return "DataPartitionA";
    case SrsAvcNaluTypeDataPartitionB: return "DataPartitionB";
    case SrsAvcNaluTypeDataPartitionC: return "DataPartitionC";
    case SrsAvcNaluTypeIDR: return "IDR";
    case SrsAvcNaluTypeSEI: return "SEI";
    case SrsAvcNaluTypeSPS: return "SPS";
    case SrsAvcNaluTypePPS: return "PPS";
    case SrsAvcNaluTypeAccessUnitDelimiter: return "AccessUnitDelimiter";
    case SrsAvcNaluTypeEOSequence: return "EOSequence";
    case SrsAvcNaluTypeEOStream: return "EOStream";
    case SrsAvcNaluTypeFilterData: return "FilterData";
    case SrsAvcNaluTypeSPSExt: return "SPSExt";
    case SrsAvcNaluTypePrefixNALU: return "PrefixNALU";
    case SrsAvcNaluTypeSubsetSPS: return "SubsetSPS";
    case SrsAvcNaluTypeLayerWithoutPartition: return "LayerWithoutPartition";
    case SrsAvcNaluTypeCodedSliceExt: return "CodedSliceExt";
    case SrsAvcNaluTypeReserved: default: return "Other";
  }
}

std::string srs_aac_profile2str(SrsAacProfile aac_profile) {
  switch (aac_profile) {
    case SrsAacProfileMain: return "Main";
    case SrsAacProfileLC: return "LC";
    case SrsAacProfileSSR: return "SSR";
    default: return "Other";
  }
}

std::string srs_aac_object2str(SrsAacObjectType aac_object) {
  switch (aac_object) {
    case SrsAacObjectTypeAacMain: return "Main";
    case SrsAacObjectTypeAacHE: return "HE";
    case SrsAacObjectTypeAacHEV2: return "HEv2";
    case SrsAacObjectTypeAacLC: return "LC";
    case SrsAacObjectTypeAacSSR: return "SSR";
    default: return "Other";
  }
}

SrsAacObjectType srs_aac_ts2rtmp(SrsAacProfile profile) {
  switch (profile) {
    case SrsAacProfileMain: return SrsAacObjectTypeAacMain;
    case SrsAacProfileLC: return SrsAacObjectTypeAacLC;
    case SrsAacProfileSSR: return SrsAacObjectTypeAacSSR;
    default: return SrsAacObjectTypeReserved;
  }
}

SrsAacProfile srs_aac_rtmp2ts(SrsAacObjectType object_type) {
  switch (object_type) {
    case SrsAacObjectTypeAacMain: return SrsAacProfileMain;
    case SrsAacObjectTypeAacHE:
    case SrsAacObjectTypeAacHEV2:
    case SrsAacObjectTypeAacLC: return SrsAacProfileLC;
    case SrsAacObjectTypeAacSSR: return SrsAacProfileSSR;
    default: return SrsAacProfileReserved;
  }
}

std::string srs_avc_profile2str(SrsAvcProfile profile) {
  switch (profile) {
    case SrsAvcProfileBaseline: return "Baseline";
    case SrsAvcProfileConstrainedBaseline: return "Baseline(Constrained)";
    case SrsAvcProfileMain: return "Main";
    case SrsAvcProfileExtended: return "Extended";
    case SrsAvcProfileHigh: return "High";
    case SrsAvcProfileHigh10: return "High(10)";
    case SrsAvcProfileHigh10Intra: return "High(10+Intra)";
    case SrsAvcProfileHigh422: return "High(422)";
    case SrsAvcProfileHigh422Intra: return "High(422+Intra)";
    case SrsAvcProfileHigh444: return "High(444)";
    case SrsAvcProfileHigh444Predictive: return "High(444+Predictive)";
    case SrsAvcProfileHigh444Intra: return "High(444+Intra)";
    default: return "Other";
  }
}

std::string srs_avc_level2str(SrsAvcLevel level) {
  switch (level) {
    case SrsAvcLevel_1: return "1";
    case SrsAvcLevel_11: return "1.1";
    case SrsAvcLevel_12: return "1.2";
    case SrsAvcLevel_13: return "1.3";
    case SrsAvcLevel_2: return "2";
    case SrsAvcLevel_21: return "2.1";
    case SrsAvcLevel_22: return "2.2";
    case SrsAvcLevel_3: return "3";
    case SrsAvcLevel_31: return "3.1";
    case SrsAvcLevel_32: return "3.2";
    case SrsAvcLevel_4: return "4";
    case SrsAvcLevel_41: return "4.1";
    case SrsAvcLevel_5: return "5";
    case SrsAvcLevel_51: return "5.1";
    default: return "Other";
  }
}

SrsSample::SrsSample() {
  size = 0;
  bytes = NULL;
  bframe = false;
  own = false;
}

SrsSample::SrsSample(char* b, int s) {
  size = s;
  bytes = b;
  bframe = false;
  own = false;
}

SrsSample::~SrsSample() {
  if (own) {
    delete bytes;
  }
}

srs_error_t SrsSample::parse_bframe() {
  srs_error_t err = srs_success;

  uint8_t header = bytes[0];
  SrsAvcNaluType nal_type = (SrsAvcNaluType)(header & kNalTypeMask);

  if (nal_type != SrsAvcNaluTypeNonIDR && 
      nal_type != SrsAvcNaluTypeDataPartitionA && 
      nal_type != SrsAvcNaluTypeIDR) {
    return err;
  }

  SrsBuffer stream(bytes, size);

  // Skip nalu header.
  stream.skip(1);

  SrsBitBuffer bitstream(&stream);
  int32_t first_mb_in_slice = 0;
  if ((err = 
      srs_avc_nalu_read_uev(&bitstream, first_mb_in_slice)) != srs_success) {
    return srs_error_wrap(err, "nalu read uev");
  }

  int32_t slice_type_v = 0;
  if ((err = srs_avc_nalu_read_uev(&bitstream, slice_type_v)) != srs_success) {
    return srs_error_wrap(err, "nalu read uev");
  }
  SrsAvcSliceType slice_type = (SrsAvcSliceType)slice_type_v;

  if (slice_type == SrsAvcSliceTypeB || slice_type == SrsAvcSliceTypeB1) {
    bframe = true;
    MLOG_CINFO("nal_type=%d, slice type=%d", nal_type, slice_type);
  }

  return err;
}

SrsSample* SrsSample::copy() {
  SrsSample* p = new SrsSample();
  p->bytes = bytes;
  p->size = size;
  p->bframe = bframe;
  return p;
}

void SrsSample::set_own() {
  char* new_buf = new char[size];
  memcpy(new_buf, bytes, size);
  bytes = new_buf;
  own = true;
}


SrsCodecConfig::SrsCodecConfig() = default;

SrsCodecConfig::~SrsCodecConfig() = default;

SrsAudioCodecConfig::SrsAudioCodecConfig() {
  id = SrsAudioCodecIdForbidden;
  sound_rate = SrsAudioSampleRateForbidden;
  sound_size = SrsAudioSampleBitsForbidden;
  sound_type = SrsAudioChannelsForbidden;
  
  audio_data_rate = 0;
  
  aac_object = SrsAacObjectTypeForbidden;
  aac_sample_rate = SrsAacSampleRateUnset; // sample rate ignored
  aac_channels = 0;
}

SrsAudioCodecConfig::~SrsAudioCodecConfig() = default;

bool SrsAudioCodecConfig::is_aac_codec_ok() {
  return !aac_extra_data.empty();
}

SrsVideoCodecConfig::SrsVideoCodecConfig() {
  id = SrsVideoCodecIdForbidden;
  video_data_rate = 0;
  frame_rate = duration = 0;
  
  width = 0;
  height = 0;
  
  NAL_unit_length = 0;
  avc_profile = SrsAvcProfileReserved;
  avc_level = SrsAvcLevelReserved;
  
  payload_format = SrsAvcPayloadFormatGuess;
}

SrsVideoCodecConfig::~SrsVideoCodecConfig() = default;

bool SrsVideoCodecConfig::is_avc_codec_ok() {
  return !avc_extra_data.empty();
}

SrsFrame::SrsFrame() = default;

SrsFrame::~SrsFrame() = default;

srs_error_t SrsFrame::initialize(SrsCodecConfig* c) {
  codec = c;
  nb_samples = 0;
  dts = 0;
  cts = 0;
  return srs_success;
}

srs_error_t SrsFrame::add_sample(char* bytes, int size) {
  srs_error_t err = srs_success;
  
  if (nb_samples >= SrsMaxNbSamples) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "Frame samples overflow");
  }
  
  SrsSample* sample = &samples[nb_samples++];
  sample->bytes = bytes;
  sample->size = size;
  sample->bframe = false;
  
  return err;
}

SrsAudioFrame::SrsAudioFrame() {
  aac_packet_type = SrsAudioAacFrameTraitForbidden;
}

SrsAudioFrame::~SrsAudioFrame() = default;

SrsAudioCodecConfig* SrsAudioFrame::acodec() {
  return (SrsAudioCodecConfig*)codec;
}

SrsVideoFrame::SrsVideoFrame() {
  frame_type = SrsVideoAvcFrameTypeForbidden;
  avc_packet_type = SrsVideoAvcFrameTraitForbidden;
  has_idr = has_aud = has_sps_pps = false;
  first_nalu_type = SrsAvcNaluTypeForbidden;
}

SrsVideoFrame::~SrsVideoFrame() = default; 

srs_error_t SrsVideoFrame::initialize(SrsCodecConfig* c) {
  first_nalu_type = SrsAvcNaluTypeForbidden;
  has_idr = has_sps_pps = has_aud = false;
  return SrsFrame::initialize(c);
}

srs_error_t SrsVideoFrame::add_sample(char* bytes, int size) {
  srs_error_t err = srs_success;
  
  if ((err = SrsFrame::add_sample(bytes, size)) != srs_success) {
    return srs_error_wrap(err, "add frame");
  }
  
  // for video, parse the nalu type, set the IDR flag.
  SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(bytes[0] & 0x1f);
  
  if (nal_unit_type == SrsAvcNaluTypeIDR) {
    has_idr = true;
  } else if (nal_unit_type == SrsAvcNaluTypeSPS || 
       nal_unit_type == SrsAvcNaluTypePPS) {
    has_sps_pps = true;
  } else if (nal_unit_type == SrsAvcNaluTypeAccessUnitDelimiter) {
    has_aud = true;
  }
  
  if (first_nalu_type == SrsAvcNaluTypeReserved) {
    first_nalu_type = nal_unit_type;
  }
  
  return err;
}

SrsVideoCodecConfig* SrsVideoFrame::vcodec() {
  return (SrsVideoCodecConfig*)codec;
}

//////////////////////////////////////////////////////////////////////////////////SrsFormat
////////////////////////////////////////////////////////////////////////////////
SrsFormat::SrsFormat() {
  acodec_ = NULL;
  vcodec_ = NULL;
  audio_ = NULL;
  video_ = NULL;
  avc_parse_sps_ = true;
  raw_ = NULL;
  nb_raw_ = 0;
}

SrsFormat::~SrsFormat() {
  srs_freep(audio_);
  srs_freep(video_);
  srs_freep(acodec_);
  srs_freep(vcodec_);
}

srs_error_t SrsFormat::initialize() {
  return srs_success;
}

srs_error_t SrsFormat::on_audio(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;
  
  if (!data || size <= 0) {
    MLOG_INFO("no audio present, ignore it.");
    return err;
  }
  
  SrsBuffer buffer(data, size);

  // We already checked the size is positive and data is not NULL.
  srs_assert(buffer.require(1));
  
  // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
  uint8_t v = buffer.read_1bytes();
  SrsAudioCodecId codec = (SrsAudioCodecId)((v >> 4) & 0x0f);
  
  if (codec != SrsAudioCodecIdMP3 && codec != SrsAudioCodecIdAAC) {
    return err;
  }
  
  if (!acodec_) {
    acodec_ = new SrsAudioCodecConfig();
  }
  if (!audio_) {
    audio_ = new SrsAudioFrame();
  }
  
  if ((err = audio_->initialize(acodec_)) != srs_success) {
    return srs_error_wrap(err, "init audio");
  }
  
  // Parse by specified codec.
  buffer.skip(-1 * buffer.pos());
  
  if (codec == SrsAudioCodecIdMP3) {
    return audio_mp3_demux(&buffer, timestamp);
  }
  
  return audio_aac_demux(&buffer, timestamp);
}

srs_error_t SrsFormat::on_video(int64_t timestamp, char* data, int size) {
  srs_error_t err = srs_success;
  
  if (!data || size <= 0) {
    MLOG_TRACE("no video present, ignore it.");
    return err;
  }
  
  SrsBuffer buffer(data, size);

  // We already checked the size is positive and data is not NULL.
  srs_assert(buffer.require(1));
  
  // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
  int8_t frame_type = buffer.read_1bytes();
  SrsVideoCodecId codec_id = (SrsVideoCodecId)(frame_type & 0x0f);
  
  // TODO: Support other codecs.
  if (codec_id != SrsVideoCodecIdAVC) {
    return err;
  }
  
  if (!vcodec_) {
    vcodec_ = new SrsVideoCodecConfig();
  }
  if (!video_) {
    video_ = new SrsVideoFrame();
  }
  
  if ((err = video_->initialize(vcodec_)) != srs_success) {
    return srs_error_wrap(err, "init video");
  }
  
  // rewind 1 byte, read by frame_type
  buffer.skip(-1 * buffer.pos());
  return video_avc_demux(&buffer, timestamp);
}

srs_error_t SrsFormat::on_aac_sequence_header(char* data, int size) {
  srs_error_t err = srs_success;
  
  if (!acodec_) {
    acodec_ = new SrsAudioCodecConfig();
  }
  if (!audio_) {
    audio_ = new SrsAudioFrame();
  }
  
  if ((err = audio_->initialize(acodec_)) != srs_success) {
    return srs_error_wrap(err, "init audio");
  }
  
  return audio_aac_sequence_header_demux(data, size);
}

bool SrsFormat::is_aac_sequence_header() {
  return acodec_ && acodec_->id == SrsAudioCodecIdAAC
      && audio_ && audio_->aac_packet_type == SrsAudioAacFrameTraitSequenceHeader;
}

bool SrsFormat::is_avc_sequence_header() {
  bool h264 = (vcodec_ && vcodec_->id == SrsVideoCodecIdAVC);
  bool h265 = (vcodec_ && vcodec_->id == SrsVideoCodecIdHEVC);
  bool av1 = (vcodec_ && vcodec_->id == SrsVideoCodecIdAV1);
  return vcodec_ && (h264 || h265 || av1)
      && video_ && video_->avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader;
}

srs_error_t SrsFormat::video_avc_demux(SrsBuffer* stream, int64_t timestamp) {
  srs_error_t err = srs_success;
  
  // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
  int8_t frame_type = stream->read_1bytes();
  SrsVideoCodecId codec_id = (SrsVideoCodecId)(frame_type & 0x0f);
  frame_type = (frame_type >> 4) & 0x0f;
  
  video_->frame_type = (SrsVideoAvcFrameType)frame_type;
  
  // ignore info frame without error,
  if (video_->frame_type == SrsVideoAvcFrameTypeVideoInfoFrame) {
    MLOG_CWARN("avc igone the info frame");
    return err;
  }
  
  // only support h.264/avc
  if (codec_id != SrsVideoCodecIdAVC) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, 
        "avc only support video h.264/avc, actual=%d", codec_id);
  }
  vcodec_->id = codec_id;
  
  if (!stream->require(4)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "avc decode avc_packet_type");
  }
  int8_t avc_packet_type = stream->read_1bytes();
  int32_t composition_time = stream->read_3bytes();
  
  // pts = dts + cts.
  video_->dts = timestamp;
  video_->cts = composition_time;
  video_->avc_packet_type = (SrsVideoAvcFrameTrait)avc_packet_type;
  
  // Update the RAW AVC data.
  raw_ = stream->data() + stream->pos();
  nb_raw_ = stream->size() - stream->pos();
  
  if (avc_packet_type == SrsVideoAvcFrameTraitSequenceHeader) {
    // TODO: FIXME: Maybe we should ignore any error for parsing sps/pps.
    if ((err = avc_demux_sps_pps(stream)) != srs_success) {
        return srs_error_wrap(err, "demux SPS/PPS");
    }
  } else if (avc_packet_type == SrsVideoAvcFrameTraitNALU){
    if ((err = video_nalu_demux(stream)) != srs_success) {
        return srs_error_wrap(err, "demux NALU");
    }
  } else {
    // ignored.
  }
  
  return err;
}

// For media server, we don't care the codec, 
// so we just try to parse sps-pps, and we could ignore any error if fail.
// LCOV_EXCL_START

srs_error_t SrsFormat::avc_demux_sps_pps(SrsBuffer* stream) {
  // AVCDecoderConfigurationRecord
  // 5.2.4.1.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
  int avc_extra_size = stream->size() - stream->pos();
  if (avc_extra_size > 0) {
    char *copy_stream_from = stream->data() + stream->pos();
    vcodec_->avc_extra_data = 
        std::vector<char>(copy_stream_from, copy_stream_from + avc_extra_size);
  }
  
  if (!stream->require(6)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "avc decode sequence header");
  }
  //ignore configurationVersion
  stream->read_1bytes();
  // AVCProfileIndication
  vcodec_->avc_profile = (SrsAvcProfile)stream->read_1bytes();
  //profile_compatibility
  stream->read_1bytes();
  //AVCLevelIndication
  vcodec_->avc_level = (SrsAvcLevel)stream->read_1bytes();
  
  // parse the NALU size.
  int8_t lengthSizeMinusOne = stream->read_1bytes();
  lengthSizeMinusOne &= 0x03;
  vcodec_->NAL_unit_length = lengthSizeMinusOne;
  
  // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
  // 5.2.4.1 AVC decoder configuration record
  // 5.2.4.1.2 Semantics
  // The value of this field shall be one of 0, 1, or 3 corresponding to a
  // length encoded with 1, 2, or 4 bytes, respectively.
  if (vcodec_->NAL_unit_length == 2) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, 
        "sps lengthSizeMinusOne should never be 2");
  }
  
  // 1 sps, 7.3.2.1 Sequence parameter set RBSP syntax
  // ISO_IEC_14496-10-AVC-2003.pdf, page 45.
  if (!stream->require(1)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
  }
  int8_t numOfSequenceParameterSets = stream->read_1bytes();
  numOfSequenceParameterSets &= 0x1f;
  if (numOfSequenceParameterSets != 1) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
  }
  if (!stream->require(2)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS size");
  }
  uint16_t sequenceParameterSetLength = stream->read_2bytes();
  if (!stream->require(sequenceParameterSetLength)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS data");
  }
  if (sequenceParameterSetLength > 0) {
    vcodec_->sequenceParameterSetNALUnit.resize(sequenceParameterSetLength);
    stream->read_bytes(
        &vcodec_->sequenceParameterSetNALUnit[0], sequenceParameterSetLength);
  }
  // 1 pps
  if (!stream->require(1)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS");
  }
  int8_t numOfPictureParameterSets = stream->read_1bytes();
  numOfPictureParameterSets &= 0x1f;
  if (numOfPictureParameterSets != 1) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS");
  }
  if (!stream->require(2)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS size");
  }
  uint16_t pictureParameterSetLength = stream->read_2bytes();
  if (!stream->require(pictureParameterSetLength)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode PPS data");
  }
  if (pictureParameterSetLength > 0) {
    vcodec_->pictureParameterSetNALUnit.resize(pictureParameterSetLength);
    stream->read_bytes(
        &vcodec_->pictureParameterSetNALUnit[0], pictureParameterSetLength);
  }
  
  return avc_demux_sps();
}

srs_error_t SrsFormat::avc_demux_sps() {
  srs_error_t err = srs_success;
  
  if (vcodec_->sequenceParameterSetNALUnit.empty()) {
    return err;
  }
  
  char* sps = &vcodec_->sequenceParameterSetNALUnit[0];
  int nbsps = (int)vcodec_->sequenceParameterSetNALUnit.size();
  
  SrsBuffer stream(sps, nbsps);
  
  // for NALU, 7.3.1 NAL unit syntax
  // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
  if (!stream.require(1)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "decode SPS");
  }
  int8_t nutv = stream.read_1bytes();
  
  // forbidden_zero_bit shall be equal to 0.
  int8_t forbidden_zero_bit = (nutv >> 7) & 0x01;
  if (forbidden_zero_bit) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "forbidden_zero_bit shall be equal to 0");
  }
  
  // nal_ref_idc not equal to 0 specifies that 
  // the content of the NAL unit contains a sequence parameter set or a picture
  // parameter set or a slice of a reference picture or 
  // a slice data partition of a reference picture.
  int8_t nal_ref_idc = (nutv >> 5) & 0x03;
  if (!nal_ref_idc) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, 
        "for sps, nal_ref_idc shall be not be equal to 0");
  }
  
  // 7.4.1 NAL unit semantics
  // ISO_IEC_14496-10-AVC-2012.pdf, page 61.
  // nal_unit_type specifies the type of RBSP data structure 
  // contained in the NAL unit as specified in Table 7-1.
  SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(nutv & 0x1f);
  if (nal_unit_type != 7) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, 
        "for sps, nal_unit_type shall be equal to 7");
  }
  
  // decode the rbsp from sps.
  // rbsp[ i ] a raw byte sequence payload is specified as 
  // an ordered sequence of bytes.
  std::vector<int8_t> rbsp(vcodec_->sequenceParameterSetNALUnit.size());
  
  int nb_rbsp = 0;
  while (!stream.empty()) {
    rbsp[nb_rbsp] = stream.read_1bytes();
    
    // XX 00 00 03 XX, the 03 byte should be drop.
    if (nb_rbsp > 2 && rbsp[nb_rbsp - 2] == 0 && 
        rbsp[nb_rbsp - 1] == 0 && rbsp[nb_rbsp] == 3) {
      // read 1byte more.
      if (stream.empty()) {
        break;
      }
      rbsp[nb_rbsp] = stream.read_1bytes();
      nb_rbsp++;
      
      continue;
    }
    
    nb_rbsp++;
  }
  
  return avc_demux_sps_rbsp((char*)&rbsp[0], nb_rbsp);
}

srs_error_t SrsFormat::avc_demux_sps_rbsp(char* rbsp, int nb_rbsp) {
  srs_error_t err = srs_success;
  
  // we donot parse the detail of sps.
  // @see https://github.com/ossrs/srs/issues/474
  if (!avc_parse_sps_) {
    return err;
  }
  
  // reparse the rbsp.
  SrsBuffer stream(rbsp, nb_rbsp);
  
  // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
  // ISO_IEC_14496-10-AVC-2012.pdf, page 62.
  if (!stream.require(3)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps shall atleast 3bytes");
  }
  uint8_t profile_idc = stream.read_1bytes();
  if (!profile_idc) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the profile_idc invalid");
  }
  
  int8_t flags = stream.read_1bytes();
  if (flags & 0x03) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the flags invalid");
  }
  
  uint8_t level_idc = stream.read_1bytes();
  if (!level_idc) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the level_idc invalid");
  }
  
  SrsBitBuffer bs(&stream);
  
  int32_t seq_parameter_set_id = -1;
  if ((err = srs_avc_nalu_read_uev(&bs, seq_parameter_set_id)) != srs_success) {
    return srs_error_wrap(err, "read seq_parameter_set_id");
  }
  if (seq_parameter_set_id < 0) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "sps the seq_parameter_set_id invalid");
  }
  
  int32_t chroma_format_idc = -1;
  if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244
    || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118
    || profile_idc == 128) {
    if ((err = srs_avc_nalu_read_uev(&bs, chroma_format_idc)) != srs_success) {
      return srs_error_wrap(err, "read chroma_format_idc");
    }
    if (chroma_format_idc == 3) {
      int8_t separate_colour_plane_flag = -1;
      if ((err = srs_avc_nalu_read_bit(&bs, separate_colour_plane_flag)) != srs_success) {
        return srs_error_wrap(err, "read separate_colour_plane_flag");
      }
    }
    
    int32_t bit_depth_luma_minus8 = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, bit_depth_luma_minus8)) != srs_success) {
      return srs_error_wrap(err, "read bit_depth_luma_minus8");;
    }
    
    int32_t bit_depth_chroma_minus8 = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, bit_depth_chroma_minus8)) != srs_success) {
      return srs_error_wrap(err, "read bit_depth_chroma_minus8");;
    }
    
    int8_t qpprime_y_zero_transform_bypass_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, qpprime_y_zero_transform_bypass_flag)) != srs_success) {
      return srs_error_wrap(err, "read qpprime_y_zero_transform_bypass_flag");;
    }
    
    int8_t seq_scaling_matrix_present_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, seq_scaling_matrix_present_flag)) != srs_success) {
      return srs_error_wrap(err, "read seq_scaling_matrix_present_flag");;
    }
    if (seq_scaling_matrix_present_flag) {
      int nb_scmpfs = ((chroma_format_idc != 3)? 8:12);
      for (int i = 0; i < nb_scmpfs; i++) {
        int8_t seq_scaling_matrix_present_flag_i = -1;
        if ((err = srs_avc_nalu_read_bit(&bs, seq_scaling_matrix_present_flag_i)) != srs_success) {
          return srs_error_wrap(err, "read seq_scaling_matrix_present_flag_i");;
        }
      }
    }
  }
  
  int32_t log2_max_frame_num_minus4 = -1;
  if ((err = srs_avc_nalu_read_uev(&bs, log2_max_frame_num_minus4)) != srs_success) {
    return srs_error_wrap(err, "read log2_max_frame_num_minus4");;
  }
  
  int32_t pic_order_cnt_type = -1;
  if ((err = srs_avc_nalu_read_uev(&bs, pic_order_cnt_type)) != srs_success) {
    return srs_error_wrap(err, "read pic_order_cnt_type");;
  }
  
  if (pic_order_cnt_type == 0) {
    int32_t log2_max_pic_order_cnt_lsb_minus4 = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, log2_max_pic_order_cnt_lsb_minus4)) != srs_success) {
      return srs_error_wrap(err, "read log2_max_pic_order_cnt_lsb_minus4");;
    }
  } else if (pic_order_cnt_type == 1) {
    int8_t delta_pic_order_always_zero_flag = -1;
    if ((err = srs_avc_nalu_read_bit(&bs, delta_pic_order_always_zero_flag)) 
            != srs_success) {
      return srs_error_wrap(err, "read delta_pic_order_always_zero_flag");;
    }
    
    int32_t offset_for_non_ref_pic = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, offset_for_non_ref_pic)) 
            != srs_success) {
      return srs_error_wrap(err, "read offset_for_non_ref_pic");;
    }
    
    int32_t offset_for_top_to_bottom_field = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, offset_for_top_to_bottom_field)) 
            != srs_success) {
      return srs_error_wrap(err, "read offset_for_top_to_bottom_field");;
    }
    
    int32_t num_ref_frames_in_pic_order_cnt_cycle = -1;
    if ((err = srs_avc_nalu_read_uev(&bs, 
            num_ref_frames_in_pic_order_cnt_cycle)) != srs_success) {
      return srs_error_wrap(err, "read num_ref_frames_in_pic_order_cnt_cycle");;
    }
    if (num_ref_frames_in_pic_order_cnt_cycle < 0) {
      return srs_error_new(ERROR_HLS_DECODE_ERROR, 
          "sps the num_ref_frames_in_pic_order_cnt_cycle");
    }
    for (int i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
      int32_t offset_for_ref_frame_i = -1;
      if ((err = srs_avc_nalu_read_uev(&bs, offset_for_ref_frame_i)) 
              != srs_success) {
        return srs_error_wrap(err, "read offset_for_ref_frame_i");;
      }
    }
  }
  
  int32_t max_num_ref_frames = -1;
  if ((err = srs_avc_nalu_read_uev(&bs, max_num_ref_frames)) != srs_success) {
    return srs_error_wrap(err, "read max_num_ref_frames");;
  }
  
  int8_t gaps_in_frame_num_value_allowed_flag = -1;
  if ((err = srs_avc_nalu_read_bit(&bs, gaps_in_frame_num_value_allowed_flag))
          != srs_success) {
    return srs_error_wrap(err, "read gaps_in_frame_num_value_allowed_flag");;
  }
  
  int32_t pic_width_in_mbs_minus1 = -1;
  if ((err = srs_avc_nalu_read_uev(&bs, pic_width_in_mbs_minus1)) 
          != srs_success) {
    return srs_error_wrap(err, "read pic_width_in_mbs_minus1");;
  }
  
  int32_t pic_height_in_map_units_minus1 = -1;
  if ((err = srs_avc_nalu_read_uev(&bs, pic_height_in_map_units_minus1)) 
          != srs_success) {
    return srs_error_wrap(err, "read pic_height_in_map_units_minus1");;
  }
  
  vcodec_->width = (int)(pic_width_in_mbs_minus1 + 1) * 16;
  vcodec_->height = (int)(pic_height_in_map_units_minus1 + 1) * 16;
  
  return err;
}

// LCOV_EXCL_STOP
srs_error_t SrsFormat::video_nalu_demux(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  // ensure the sequence header demuxed
  if (!vcodec_->is_avc_codec_ok()) {
    MLOG_CWARN("avc ignore type=%d for no sequence header", SrsVideoAvcFrameTraitNALU);
    return err;
  }
  
  // guess for the first time.
  if (vcodec_->payload_format == SrsAvcPayloadFormatGuess) {
    // One or more NALUs (Full frames are required)
    // try  "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    if ((err = avc_demux_annexb_format(stream)) != srs_success) {
      // stop try when system error.
      if (srs_error_code(err) != ERROR_HLS_AVC_TRY_OTHERS) {
        return srs_error_wrap(err, "avc demux for annexb");
      }
      srs_freep(err);
      
      // try "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
      if ((err = avc_demux_ibmf_format(stream)) != srs_success) {
        return srs_error_wrap(err, "avc demux ibmf");
      } else {
        vcodec_->payload_format = SrsAvcPayloadFormatIbmf;
      }
    } else {
      vcodec_->payload_format = SrsAvcPayloadFormatAnnexb;
    }
  } else if (vcodec_->payload_format == SrsAvcPayloadFormatIbmf) {
    // try "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
    if ((err = avc_demux_ibmf_format(stream)) != srs_success) {
        return srs_error_wrap(err, "avc demux ibmf");
    }
  } else {
    // One or more NALUs (Full frames are required)
    // try  "AnnexB" from ISO_IEC_14496-10-AVC-2003.pdf, page 211.
    if ((err = avc_demux_annexb_format(stream)) != srs_success) {
        // ok, we guess out the payload is annexb, but maybe changed to ibmf.
      if (srs_error_code(err) != ERROR_HLS_AVC_TRY_OTHERS) {
        return srs_error_wrap(err, "avc demux annexb");
      }
      srs_freep(err);
      
      // try "ISO Base Media File Format" from ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
      if ((err = avc_demux_ibmf_format(stream)) != srs_success) {
        return srs_error_wrap(err, "avc demux ibmf");
      } else {
        vcodec_->payload_format = SrsAvcPayloadFormatIbmf;
      }
    }
  }
  
  return err;
}

srs_error_t SrsFormat::avc_demux_annexb_format(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  // not annexb, try others
  if (!srs_avc_startswith_annexb(stream, NULL)) {
    return srs_error_new(ERROR_HLS_AVC_TRY_OTHERS, "try others");
  }
  
  // AnnexB
  // B.1.1 Byte stream NAL unit syntax,
  // ISO_IEC_14496-10-AVC-2003.pdf, page 211.
  while (!stream->empty()) {
    // find start code
    int nb_start_code = 0;
    if (!srs_avc_startswith_annexb(stream, &nb_start_code)) {
      return err;
    }
    
    // skip the start code.
    if (nb_start_code > 0) {
      stream->skip(nb_start_code);
    }
    
    // the NALU start bytes.
    char* p = stream->data() + stream->pos();
    
    // get the last matched NALU
    while (!stream->empty()) {
      if (srs_avc_startswith_annexb(stream, NULL)) {
        break;
      }
      
      stream->skip(1);
    }
    
    char* pp = stream->data() + stream->pos();
    
    // skip the empty.
    if (pp - p <= 0) {
      continue;
    }
    
    // got the NALU.
    if ((err = video_->add_sample(p, (int)(pp - p))) != srs_success) {
      return srs_error_wrap(err, "add video frame");
    }
  }
  
  return err;
}

srs_error_t SrsFormat::avc_demux_ibmf_format(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  int PictureLength = stream->size() - stream->pos();
  
  // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 16
  // 5.2.4.1 AVC decoder configuration record
  // 5.2.4.1.2 Semantics
  // The value of this field shall be one of 0, 1, or 3 corresponding to a
  // length encoded with 1, 2, or 4 bytes, respectively.
  srs_assert(vcodec_->NAL_unit_length != 2);
  
  // 5.3.4.2.1 Syntax, ISO_IEC_14496-15-AVC-format-2012.pdf, page 20
  for (int i = 0; i < PictureLength;) {
    // unsigned int((NAL_unit_length+1)*8) NALUnitLength;
    if (!stream->require(vcodec_->NAL_unit_length + 1)) {
      return srs_error_new(ERROR_HLS_DECODE_ERROR, "avc decode NALU size");
    }
    int32_t NALUnitLength = 0;
    if (vcodec_->NAL_unit_length == 3) {
      NALUnitLength = stream->read_4bytes();
    } else if (vcodec_->NAL_unit_length == 1) {
      NALUnitLength = stream->read_2bytes();
    } else {
      NALUnitLength = stream->read_1bytes();
    }
    
    // maybe stream is invalid format.
    // see: https://github.com/ossrs/srs/issues/183
    if (NALUnitLength < 0) {
      return srs_error_new(ERROR_HLS_DECODE_ERROR, "maybe stream is AnnexB format");
    }
    
    // NALUnit
    if (!stream->require(NALUnitLength)) {
      return srs_error_new(ERROR_HLS_DECODE_ERROR, "avc decode NALU data");
    }
    // 7.3.1 NAL unit syntax, ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    if ((err = video_->add_sample(stream->data() + stream->pos(), NALUnitLength)) != srs_success) {
      return srs_error_wrap(err, "avc add video frame");
    }
    stream->skip(NALUnitLength);
    
    i += vcodec_->NAL_unit_length + 1 + NALUnitLength;
  }
  
  return err;
}

srs_error_t SrsFormat::audio_aac_demux(SrsBuffer* stream, int64_t timestamp) {
  srs_error_t err = srs_success;
  
  audio_->cts = 0;
  audio_->dts = timestamp;
  
  // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
  int8_t sound_format = stream->read_1bytes();
  
  int8_t sound_type = sound_format & 0x01;
  int8_t sound_size = (sound_format >> 1) & 0x01;
  int8_t sound_rate = (sound_format >> 2) & 0x03;
  sound_format = (sound_format >> 4) & 0x0f;

  SrsAudioCodecId codec_id = (SrsAudioCodecId)sound_format;
  acodec_->id = codec_id;

  acodec_->sound_type = (SrsAudioChannels)sound_type;
  acodec_->sound_rate = (SrsAudioSampleRate)sound_rate;
  acodec_->sound_size = (SrsAudioSampleBits)sound_size;

  // we support h.264+mp3 for hls.
  if (codec_id == SrsAudioCodecIdMP3) {
    return srs_error_new(ERROR_HLS_TRY_MP3, "try mp3");
  }

  // only support aac
  if (codec_id != SrsAudioCodecIdAAC) {
    return srs_error_new(
        ERROR_HLS_DECODE_ERROR, "not supported codec %d", codec_id);
  }

  if (!stream->require(1)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "aac decode aac_packet_type");
  }
  
  SrsAudioAacFrameTrait aac_packet_type = 
      (SrsAudioAacFrameTrait)stream->read_1bytes();
  audio_->aac_packet_type = (SrsAudioAacFrameTrait)aac_packet_type;
  
  // Update the RAW AAC data.
  raw_ = stream->data() + stream->pos();
  nb_raw_ = stream->size() - stream->pos();
  
  if (aac_packet_type == SrsAudioAacFrameTraitSequenceHeader) {
    // AudioSpecificConfig
    // 1.6.2.1 AudioSpecificConfig, in ISO_IEC_14496-3-AAC-2001.pdf, page 33.
    if (nb_raw_ > 0) {
      acodec_->aac_extra_data = 
          std::move(std::vector<char>(raw_, raw_ + nb_raw_));
      
      if ((err = audio_aac_sequence_header_demux(
          &acodec_->aac_extra_data[0], nb_raw_)) != srs_success) {
        return srs_error_wrap(err, "demux aac sh");
      }
    }
  } else if (aac_packet_type == SrsAudioAacFrameTraitRawData) {
    // ensure the sequence header demuxed
    if (!acodec_->is_aac_codec_ok()) {
      MLOG_CWARN("aac ignore type=%d for no sequence header", aac_packet_type);
      return err;
    }
    
    // Raw AAC frame data in UI8 []
    // 6.3 Raw Data, ISO_IEC_13818-7-AAC-2004.pdf, page 28
    if ((err = audio_->add_sample(raw_, nb_raw_)) != srs_success) {
      return srs_error_wrap(err, "add audio frame");
    }
  } else {
    // ignored.
  }
  
  // reset the sample rate by sequence header
  if (acodec_->aac_sample_rate != SrsAacSampleRateUnset) {
    switch (srs_aac_srates[acodec_->aac_sample_rate]) {
      case 11025:
        acodec_->sound_rate = SrsAudioSampleRate11025;
        break;
      case 22050:
        acodec_->sound_rate = SrsAudioSampleRate22050;
        break;
      case 44100:
        acodec_->sound_rate = SrsAudioSampleRate44100;
        break;
      default:
        break;
    };
  }
  
  return err;
}

srs_error_t SrsFormat::audio_mp3_demux(SrsBuffer* stream, int64_t timestamp) {
  srs_error_t err = srs_success;
  
  audio_->cts = 0;
  audio_->dts = timestamp;
  
  // @see: E.4.2 Audio Tags, video_file_format_spec_v10_1.pdf, page 76
  int8_t sound_format = stream->read_1bytes();
  
  int8_t sound_type = sound_format & 0x01;
  int8_t sound_size = (sound_format >> 1) & 0x01;
  int8_t sound_rate = (sound_format >> 2) & 0x03;
  sound_format = (sound_format >> 4) & 0x0f;
  
  SrsAudioCodecId codec_id = (SrsAudioCodecId)sound_format;
  acodec_->id = codec_id;
  
  acodec_->sound_type = (SrsAudioChannels)sound_type;
  acodec_->sound_rate = (SrsAudioSampleRate)sound_rate;
  acodec_->sound_size = (SrsAudioSampleBits)sound_size;
  
  // we always decode aac then mp3.
  srs_assert(acodec_->id == SrsAudioCodecIdMP3);
  
  // Update the RAW MP3 data.
  raw_ = stream->data() + stream->pos();
  nb_raw_ = stream->size() - stream->pos();
  
  stream->skip(1);
  if (stream->empty()) {
    return err;
  }
  
  char* data = stream->data() + stream->pos();
  int size = stream->size() - stream->pos();
  
  // mp3 payload.
  if ((err = audio_->add_sample(data, size)) != srs_success) {
    return srs_error_wrap(err, "add audio frame");
  }
  
  return err;
}

srs_error_t SrsFormat::audio_aac_sequence_header_demux(char* data, int size) {
  srs_error_t err = srs_success;
  
  SrsBuffer buffer(data, size);

  // only need to decode the first 2bytes:
  //      audioObjectType, aac_profile, 5bits.
  //      samplingFrequencyIndex, aac_sample_rate, 4bits.
  //      channelConfiguration, aac_channels, 4bits
  if (!buffer.require(2)) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, "audio codec decode aac sh");
  }
  uint8_t profile_ObjectType = buffer.read_1bytes();
  uint8_t samplingFrequencyIndex = buffer.read_1bytes();
  
  acodec_->aac_channels = (samplingFrequencyIndex >> 3) & 0x0f;
  samplingFrequencyIndex = ((profile_ObjectType << 1) & 0x0e) | 
                           ((samplingFrequencyIndex >> 7) & 0x01);
  profile_ObjectType = (profile_ObjectType >> 3) & 0x1f;
  
  // set the aac sample rate.
  acodec_->aac_sample_rate = samplingFrequencyIndex;
  
  // convert the object type in sequence header to aac profile of ADTS.
  acodec_->aac_object = (SrsAacObjectType)profile_ObjectType;
  if (acodec_->aac_object == SrsAacObjectTypeReserved) {
    return srs_error_new(ERROR_HLS_DECODE_ERROR, 
        "aac decode sh object %d", profile_ObjectType);
  }
  
  // TODO: FIXME: to support aac he/he-v2, see: ngx_rtmp_codec_parse_aac_header
  // @see: https://github.com/winlinvip/nginx-rtmp-module/commit/3a5f9eea78fc8d11e8be922aea9ac349b9dcbfc2
  //
  // donot force to LC, @see: https://github.com/ossrs/srs/issues/81
  // the source will print the sequence header info.
  //if (aac_profile > 3) {
    // Mark all extended profiles as LC
    // to make Android as happy as possible.
    // @see: ngx_rtmp_hls_parse_aac_header
    //aac_profile = 1;
  //}
  
  return err;
}

} //namespace ma

