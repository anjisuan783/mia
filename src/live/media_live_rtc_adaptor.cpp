#include "live/media_live_rtc_adaptor.h"

#include "common/media_log.h"
#include "common/media_performance.h"
#include "rtmp/media_req.h"
#include "common/media_message.h"
#include "live/media_live_rtc_adaptor_sink.h"
#include "live/media_live_source.h"
#include "live/media_consumer.h"
#include "rtc/media_rtc_source.h"
#include "encoder/media_codec.h"
#include "live/media_meta_cache.h"
#include "encoder/media_rtc_codec.h"
#include "utils/media_kernel_buffer.h"
#include "common/media_io.h"
#include "encoder/media_flv_encoder.h"
#include "utils/protocol_utility.h"
#include "encoder/media_rtc_codec.h"

namespace ma {

namespace {
////////////////////////////////////////////////////////////////////////////////
//AudioTransform
////////////////////////////////////////////////////////////////////////////////
// ISO_IEF_13818-7-AAC-2004
// 6.2 ADTS
srs_error_t aac_raw_append_adts_header(
    SrsFormat* format, char** pbuf, int* pnn_buf) {
  srs_error_t err = srs_success;

  if (format->is_aac_sequence_header()) {
    return err;
  }

  if (format->audio->nb_samples != 1) {
    return srs_error_new(ERROR_RTC_RTP_MUXER, "adts");
  }

  int nb_buf = format->audio->samples[0].size + 7;
  char* buf = new char[nb_buf];
  SrsBuffer stream(buf, nb_buf);

  // syncword          12bit 0xFFF 
  // ID                1bt 0:MPEG-4，1:MPEG-2
  // layer             2bit '00'
  // protection_absent 1bit 0:CRC 1:no CRS
  // profile           2bit 0: AAC Main 1:AAC LC 2:AAC SSR 3:AAC LTP
  // sampling_frequency_index 4bit
  // private_bit：      1bit
  // channel_configuration：3bit
  // original_copy:    1bit
  // home:             1bit

  // copyrighted_id_bit：       1bit
  // copyrighted_id_start：     1bit
  // aac_frame_length：         13bit
  // adts_buffer_fullness：     11bit 0x7FF。
  // number_of_raw_data_blocks_in_frame：2bit

  stream.write_1bytes(0xFF);
  stream.write_1bytes(0xF1);
  stream.write_1bytes(((format->acodec->aac_object - 1) << 6) | 
                      ((format->acodec->aac_sample_rate & 0x0F) << 2) | 
                      ((format->acodec->aac_channels & 0x04) >> 2));
  stream.write_1bytes(((format->acodec->aac_channels & 0x03) << 6) | 
                      ((nb_buf >> 11) & 0x03));
  stream.write_1bytes((nb_buf >> 3) & 0xFF);
  stream.write_1bytes(((nb_buf & 0x07) << 5) | 0x1F);
  stream.write_1bytes(0xFC);

  stream.write_bytes(format->audio->samples[0].bytes, 
                     format->audio->samples[0].size);

  *pbuf = buf;
  *pnn_buf = nb_buf;

  return err;
}

}

 AudioTransform::AudioTransform() = default;

AudioTransform::~AudioTransform() {
  if (adts_writer_) {
    adts_writer_->close();
    adts_writer_.reset(nullptr);
  }
}

srs_error_t AudioTransform::Open(TransformSink* sink,
    bool debug, const std::string& fileName) {
  sink_ = sink;
  srs_error_t err = srs_success;
  if (!debug) {
    return err;
  }

  adts_writer_.reset(new SrsFileWriter);
  if (srs_success != (err = adts_writer_->open(fileName))) {
    return err;
  }

  return err;
}

srs_error_t AudioTransform::OnData(std::shared_ptr<MediaMessage> msg) {
  srs_error_t err = srs_success;
  if ((err = format_.on_audio(msg)) != srs_success) {
    return srs_error_wrap(err, "format consume audio");
  }

  // codec is not parsed, or unknown codec, just ignore.
  if (!format_.acodec) {
    return err;
  }

  SrsAudioCodecId acodec = format_.acodec->id;
  // When drop aac audio packet, never transcode.
  if (acodec != SrsAudioCodecIdAAC) {
    return err;
  }

  char* adts_audio = NULL;
  int nn_adts_audio = 0;
  if ((err = aac_raw_append_adts_header( 
      &format_, &adts_audio, &nn_adts_audio)) != srs_success) {
    return srs_error_wrap(err, "aac append header");
  }

  if (!adts_audio) {
    return err;
  }

  if (adts_writer_) {
    adts_writer_->write(adts_audio, nn_adts_audio, nullptr);
  }

  SrsAudioFrame aac;
  aac.dts = format_.audio->dts;
  aac.cts = format_.audio->cts;
  if (srs_success == (err = aac.add_sample(adts_audio, nn_adts_audio))) {
    // If OK, transcode the AAC to Opus and consume it.
    std::vector<SrsAudioFrame*> out_audios;
    if ((err = Transcode(&aac, out_audios)) == srs_success) {
      for (auto it = out_audios.begin(); it != out_audios.end(); ++it) {
        SrsAudioFrame* out_audio = *it;

        owt_base::Frame frm;
        frm.format = owt_base::FRAME_FORMAT_OPUS;
        frm.length = out_audio->samples[0].size;
        frm.payload = new uint8_t[frm.length];
        memcpy(frm.payload, out_audio->samples[0].bytes, frm.length);

        frm.timeStamp = out_audio->dts * 48;
        frm.ntpTimeMs = out_audio->dts;
        frm.additionalInfo.audio.isRtpPacket = false;
        frm.additionalInfo.audio.nbSamples = out_audio->dts * 48;
        frm.additionalInfo.audio.sampleRate = 48000;
        frm.additionalInfo.audio.channels = 2;
        sink_->OnFrame(frm);
      }
      codec_->free_frames(out_audios);
    }
  }
  srs_freepa(adts_audio);

  return err;
}

srs_error_t AudioTransform::Transcode(SrsAudioFrame* audio, 
    std::vector<SrsAudioFrame*>& out_audios) {
  srs_error_t err = srs_success;

  if (codec_ == nullptr) {
    SrsAudioTranscoder::AudioFormat from, to;

    // read from sdp ?
    to.codec = SrsAudioCodecIdOpus;
    to.samplerate = 48000; // The output audio bitrate in bps.
    to.channels = 2;      //stero
    to.bitrate = 48000;  // The output audio bitrate in bps.

    from.codec = SrsAudioCodecIdAAC; // The output audio codec.
    from.samplerate = GetAacSampleRate(format_.acodec->aac_sample_rate);
    from.channels = format_.acodec->aac_channels;
    from.bitrate = format_.acodec->audio_data_rate;

    codec_.reset(new SrsAudioTranscoder);
    if ((err = codec_->initialize(from, to)) != srs_success) {
      return srs_error_wrap(err, "codec initialize");
    }
  }
  
  if ((err = codec_->transcode(audio, out_audios)) != srs_success) {
    return srs_error_wrap(err, "recode error");
  }
  return err;
}

////////////////////////////////////////////////////////////////////////////////
//Videotransform
////////////////////////////////////////////////////////////////////////////////
Videotransform::Videotransform() : meta_(new MediaMetaCache) { }

Videotransform::~Videotransform() {
  if (h264_writer_) {
    h264_writer_->close();
    h264_writer_.reset(nullptr);
  }
}

srs_error_t Videotransform::Open(TransformSink* sink,
    bool debug, const std::string& fileName) {
  sink_ = sink;

  srs_error_t err = srs_success;
  if (!debug) {
    return err;
  }
  h264_writer_.reset(new SrsFileWriter);
  if (srs_success != (err = h264_writer_->open(fileName))) {
    return err;
  }
  return err;
}

srs_error_t Videotransform::OnData(std::shared_ptr<MediaMessage> msg) {
  srs_error_t err = srs_success;

  // cache the sequence header if h264
  bool is_sequence_header = 
      SrsFlvVideo::sh(msg->payload_->GetFirstMsgReadPtr(), 
                      msg->payload_->GetFirstMsgLength());
  if (is_sequence_header && (err = meta_->update_vsh(msg)) != srs_success) {
    return srs_error_wrap(err, "meta update video");
  }

  if ((err = format_.on_video(msg)) != srs_success) {
    return srs_error_wrap(err, "format consume video");
  }

  // Ignore if no format->vcodec, it means the codec is not parsed, 
  // or unsupport/unknown codec, such as H.263 codec
  if (!format_.vcodec) {
    return err;
  }

  bool has_idr = false;
  std::deque<SrsSample*> samples;
  int len = 0;
  if ((err = Filter(&format_, has_idr, samples, len)) != srs_success) {
    return srs_error_wrap(err, "filter video");
  }
  int nn_samples = (int)samples.size();

  owt_base::Frame frm;
  if ((err = PackageVideoframe(has_idr, msg.get(), samples, len, frm))
      != srs_success) {
    return srs_error_wrap(err, "package video");
  }

  if (h264_writer_) {
    char* pData = (char*)frm.payload;
    int data_size = (int)frm.length;

    h264_writer_->write(pData, data_size, 0);
  }

  sink_->OnFrame(frm);
  return err;
}

srs_error_t Videotransform::Filter(SrsFormat* format, bool& has_idr,
     std::deque<SrsSample*>& samples, int& data_len) {
  srs_error_t err = srs_success;
  data_len = 0;

  // If IDR, we will insert SPS/PPS before IDR frame.
  if (format->video && format->video->has_idr) {
    has_idr = true;
  }

  // Update samples to shared frame.
  for (int i = 0; i < format->video->nb_samples; ++i) {
    SrsSample* sample = &format->video->samples[i];

    // Because RTC does not support B-frame, so we will drop them.
    // TODO: Drop B-frame in better way, which not cause picture corruption.
    if ((err = sample->parse_bframe()) != srs_success) {
      return srs_error_wrap(err, "parse bframe");
    }
    if (sample->bframe) {
      continue;
    }
    data_len += sample->size;
    samples.push_back(sample);
  }

  return err;
}

srs_error_t Videotransform::PackageVideoframe(bool idr,
    MediaMessage* msg, std::deque<SrsSample*>& samples,
    int len, owt_base::Frame& frame) {
  srs_error_t err = srs_success;

  // Well, for each IDR, we append a SPS/PPS before it
  if (idr) {
    SrsFormat* format = meta_->vsh_format();
    if (!format || !format->vcodec) {
      return err;
    }

    // Note that the sps/pps may change, so we should copy it.
    const std::vector<char>& sps = format->vcodec->sequenceParameterSetNALUnit;
    const std::vector<char>& pps = format->vcodec->pictureParameterSetNALUnit;
    if (sps.empty() || pps.empty()) {
      return srs_error_new(ERROR_RTC_RTP_MUXER, "sps/pps empty");
    }

    { // pps
      SrsSample sample_pps;
      sample_pps.bytes = (char*)&pps[0];
      sample_pps.size = (int)pps.size();
      samples.push_front(&sample_pps);
      len += sample_pps.size;
    }

    { // sps
      SrsSample sample_sps;
      sample_sps.bytes = (char*)&sps[0];
      sample_sps.size = (int)sps.size();
      samples.push_front(&sample_sps);
      len += sample_sps.size;
    }

    frame.additionalInfo.video.isKeyFrame = true;
  } else {
    frame.additionalInfo.video.isKeyFrame = false;
  }

  frame.format = owt_base::FRAME_FORMAT_H264;
  frame.timeStamp = msg->timestamp_ * 90;
  frame.ntpTimeMs = msg->timestamp_;
  frame.additionalInfo.video.height = 0;
  frame.additionalInfo.video.width = 0;
  
  size_t nn_samples = samples.size();

  int buffer_len = nn_samples * 4 + len;
  frame.length = buffer_len;
  frame.payload = new uint8_t[buffer_len];
  uint8_t* p = frame.payload;
  
  for (size_t i = 0; i < nn_samples; ++i) {
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x01;
    SrsSample* sample = samples[i];
    memcpy(p, sample->bytes, sample->size);
    p += sample->size;
  }

  return err;
}

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.live");

////////////////////////////////////////////////////////////////////////////////
//MediaLiveRtcAdaptor
////////////////////////////////////////////////////////////////////////////////
MediaLiveRtcAdaptor::MediaLiveRtcAdaptor(const std::string& streamName) 
    : stream_name_(streamName) {
  MLOG_TRACE(stream_name_);
}

MediaLiveRtcAdaptor::~MediaLiveRtcAdaptor() {
  MLOG_TRACE(stream_name_);

  if (debug_writer_) {
    debug_writer_->close();
  }
}

srs_error_t MediaLiveRtcAdaptor::Open(wa::Worker* worker,
    MediaLiveSource* source, LiveRtcAdapterSink* sink, bool enableDebug) {
  srs_error_t err = srs_success;
  enable_debug_ = enableDebug;
  live_source_ = source;
  auto consumer = live_source_->CreateConsumer();

  if ((err = source->ConsumerDumps(
      consumer.get(), true, true, true)) != srs_success) {
    MLOG_CERROR("dumps consumer, desc:%s", srs_error_desc(err));
    return err;
  }
  consumer_ = std::move(consumer);
  rtc_source_ = sink;
  rtc_source_->OnLocalPublish(stream_name_);

  static constexpr auto TIME_OUT = std::chrono::milliseconds(SRS_PERF_MW_SLEEP);

  //TODO timer push need optimizing
  worker->scheduleEvery([weak_this = weak_from_this()]() {
    if (auto this_ptr = weak_this.lock()) {
      return this_ptr->OnTimer();
    }
    return false;
  }, TIME_OUT);

  std::string streamName = srs_string_replace(source->StreamName(), "/", "_");

  // debug aac
  std::string file_writer_path = "/tmp/rtmpadaptor" + streamName + "_d.aac";
  audio_.reset(new AudioTransform);
  if (srs_success !=(err=audio_->Open(this, enable_debug_, file_writer_path))) {
    return err;
  }

  // debug h264
  file_writer_path = "/tmp/rtmpadaptor" + streamName + "_d.h264";
  video_.reset(new Videotransform);
  if (srs_success !=(err=video_->Open(this, enable_debug_, file_writer_path))) {
    return err;
  }

  if (enable_debug_) {
    file_writer_path = "/tmp/rtmpadaptor" + streamName + "_d.flv";
    debug_writer_.reset(new SrsFileWriter);
    if (srs_success != (err = debug_writer_->open(file_writer_path))) {
      return err;
    }
    
    debug_flv_encoder_.reset(new SrsFlvStreamEncoder);
    if (srs_success != 
        (err = debug_flv_encoder_->initialize(debug_writer_.get(), nullptr))) {
      return err;
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (debug_flv_encoder_->has_cache()) {
      if (srs_success != 
          (err = debug_flv_encoder_->dump_cache(
              consumer.get(), source->jitter()))) {
        return err;
      }
    }
  }
  return err;
}

void MediaLiveRtcAdaptor::Close() {
  audio_.reset(nullptr);
  video_.reset(nullptr);

  live_source_->DestroyConsumer(consumer_.get());
  consumer_ = nullptr;
  rtc_source_->OnLocalUnpublish();
  rtc_source_ = nullptr;
}

srs_error_t MediaLiveRtcAdaptor::OnAudio(std::shared_ptr<MediaMessage> msg) {
  return audio_->OnData(std::move(msg));
}

void MediaLiveRtcAdaptor::OnFrame(owt_base::Frame& frame) {
  auto msg = std::make_shared<owt_base::Frame>(std::move(frame));
  rtc_source_->OnFrame(std::move(msg));
}

srs_error_t MediaLiveRtcAdaptor::OnVideo(std::shared_ptr<MediaMessage> msg) {
  return video_->OnData(std::move(msg));
}

bool MediaLiveRtcAdaptor::OnTimer() {
  srs_error_t err = srs_success;
  int count;
  std::vector<std::shared_ptr<MediaMessage>> cache;
  consumer_->fetch_packets(SRS_PERF_MW_MSGS, cache, count);

  for(int i = 0; i < count; ++i) {
    if (cache[i]->is_audio()) {
      err = OnAudio(cache[i]);
    } else if (cache[i]->is_video()) {
      err = OnVideo(cache[i]);
    }

    if (err != srs_success) {
      MLOG_CERROR("consume, media packet, desc:%s", srs_error_desc(err));
      delete err;
    }
  }

  if (enable_debug_) {
    SrsFlvStreamEncoder* fast = 
        dynamic_cast<SrsFlvStreamEncoder*>(debug_flv_encoder_.get());
    if ((err = fast->write_tags(cache)) != srs_success) {
        MLOG_ERROR("write_tags failed, desc:" << srs_error_desc(err));
      delete err;
    }
  }

  return true;
}

}

