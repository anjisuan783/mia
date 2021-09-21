//
// Copyright (c) 2013-2021 Bepartofyou
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_RTC_CODEC_H__
#define __MEDIA_RTC_CODEC_H__

#include <string>

#include "common/media_kernel_error.h"
#include "encoder/media_codec.h"
#include "common/media_log.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"
#ifdef __cplusplus
}
#endif

namespace ma {

class SrsAudioTranscoder {
  MDECLARE_LOGGER();
 public:
  struct AudioFormat {
    SrsAudioCodecId codec;
    uint16_t samplerate;   //The sample_rate specifies the sample rate of encoder, for example, 48000.
    uint16_t bitpersample; //current only suport signed 16bit per sample
    uint16_t channels;     //1: mono  2: stereo
    uint32_t bitrate;      //The bit_rate specifies the bitrate of encoder, for example, 48000
  };
 
  SrsAudioTranscoder();
  virtual ~SrsAudioTranscoder();

  // Initialize the transcoder, transcode from codec as to codec.
  srs_error_t initialize(const AudioFormat& from, const AudioFormat& to);
  // Transcode the input audio frame in, as output audio frames outs.
  srs_error_t transcode(SrsAudioFrame* in, std::vector<SrsAudioFrame*>& outs);
  // Free the generated audio frames by transcode.
  void free_frames(std::vector<SrsAudioFrame*>& frames);

  // Get the aac codec header, for example, FLV sequence header.
  // @remark User should never free the data, it's managed by this transcoder.
  void aac_codec_header(uint8_t** data, int* len);
 private:
  srs_error_t init_dec(const AudioFormat& format);
  srs_error_t init_enc(const AudioFormat& format);
  srs_error_t init_swr(const AudioFormat& from, const AudioFormat& to);
  srs_error_t init_fifo();

  srs_error_t decode_and_resample(SrsAudioFrame* pkt);
  srs_error_t encode(std::vector<SrsAudioFrame*> &pkts);

  srs_error_t add_samples_to_fifo(uint8_t** samples, int frame_size);
  void free_swr_samples();

 private:
  AVCodecContext *dec_;
  AVFrame *dec_frame_;
  AVPacket *dec_packet_;

  AVCodecContext *enc_;
  AVFrame *enc_frame_;
  AVPacket *enc_packet_;

  SwrContext *swr_;
  //buffer for swr out put
  uint8_t **swr_data_;
  AVAudioFifo *fifo_;

  int64_t new_pkt_pts_;
  int64_t next_out_pts_;
};

} //namespace ma

#endif /* __MEDIA_RTC_CODEC_H__ */

