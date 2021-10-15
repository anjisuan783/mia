#include "rtc/media_rtc_live_adaptor.h"

#include "myrtc/rtp_rtcp/rtp_packet.h"
#include "utils/media_kernel_buffer.h"
#include "utils/media_msg_chain.h"
#include "common/media_define.h"
#include "common/media_message.h"

namespace ma {

MDEFINE_LOGGER(MediaRtcLiveAdaptor, "MediaRtcLiveAdaptor");

#define __OPTIMIZE__

// 00 00 00 01
const uint32_t kNaluLongStartSequence = 0x1000000;
// 00 00 01
const uint32_t kNaluShortStartSequence = 0x10000;

// StapPackage STAP-A, for multiple NALUs.
class StapPackage final {
 public:
  StapPackage(bool f, uint32_t ts);
  ~StapPackage() = default;

  SrsSample* get_sps();
  SrsSample* get_pps();

  uint64_t nb_bytes();
  srs_error_t encode(SrsBuffer* buf);
  srs_error_t decode(SrsBuffer* buf);

 public:
  // The NRI in NALU type.
  SrsAvcNaluType nri_{SrsAvcNaluTypeReserved};
  // The NALU samples, we will manage the samples.
  std::vector<SrsSample> nalus_;
  bool key_frame_{false};
  uint32_t time_stamp_{0};
};

StapPackage::StapPackage(bool f, uint32_t ts)
    : key_frame_{f}, time_stamp_(ts) {
}

SrsSample* StapPackage::get_sps() {
  SrsSample* p = nullptr;
#ifdef __OPTIMIZE__
  if (key_frame_) {
    assert(nalus_.size() >= 2);
    p = &nalus_[0];
  }
#else
  size_t n_nalus = nalus_.size();
  for (size_t i = 0; i < n_nalus; ++i) {
    p = &nalus_[i];
    SrsAvcNaluType nalu_type = (SrsAvcNaluType)(p->bytes[0] & kNalTypeMask);
    if (nalu_type == SrsAvcNaluTypeSPS) {
      break;
    }
  }
#endif
  return p;
}

SrsSample* StapPackage::get_pps() {
  SrsSample* p = nullptr;
#ifdef __OPTIMIZE__
  if (key_frame_) {
    assert(nalus_.size() >= 2);
    p = &nalus_[1];
  }
#else  
  size_t n_nalus = nalus_.size();
  for (int i = 0; i < n_nalus; ++i) {
    p = &nalus_[i];

    SrsAvcNaluType nalu_type = (SrsAvcNaluType)(p->bytes[0] & kNalTypeMask);
    if (nalu_type == SrsAvcNaluTypePPS) {
      break;
    }
  }
#endif
  return p;
}

uint64_t StapPackage::nb_bytes() {
  int size = 1;

  size_t n_nalus = nalus_.size();
  for (size_t i = 0; i < n_nalus; i++) {
    SrsSample& p = nalus_[i];
    size += 4 + p.size;
  }

  return size;
}

srs_error_t StapPackage::encode(SrsBuffer* buf) {
  
  for (auto& sample : nalus_) {
    if (sample.size > 0) {  
      buf->write_4bytes(sample.size);
      buf->write_bytes(sample.bytes, sample.size);
    }
  }

  return srs_success;
}

srs_error_t StapPackage::decode(SrsBuffer* buf) {
  if (!buf->require(4)) {
      return srs_error_new(ERROR_RTC_FRAME_MUXER, "requires %d bytes", 4);
  }

  // Nalu Start Sequence
  int start_seq_size = 3;
  uint32_t nalu_start_seq = buf->read_3bytes();

  if (nalu_start_seq != kNaluShortStartSequence) {
    nalu_start_seq = kNaluLongStartSequence;
    buf->read_1bytes();
    start_seq_size = 4;
  }

  char* nalu_start = buf->head();
  uint8_t v = buf->read_1bytes();
  
  // forbidden_zero_bit shoul be zero.
  // @see https://tools.ietf.org/html/rfc6184#section-5.3
  uint8_t f = (v & 0x80);
  if (f == 0x80) {
    return srs_error_new(ERROR_RTC_RTP_MUXER, "forbidden_zero_bit should be zero");
  }

  nri_ = SrsAvcNaluType(v & (~kNalTypeMask));

  // NALUs.
  while (!buf->empty()) {
    uint32_t* first4 = (uint32_t*)buf->head();
    if (*first4 == nalu_start_seq) {
      if (nalu_start < buf->head()) {
        nalus_.emplace_back(nalu_start, buf->head() - nalu_start);
      }
      nalu_start = buf->head() + start_seq_size;
    }
    buf->skip(1);
  }
  nalus_.emplace_back(nalu_start, buf->head() - nalu_start + 1);

#if 0
  for(auto& x : nalus_) {
    SrsAvcNaluType nalu_type = (SrsAvcNaluType)(*(x.bytes) & kNalTypeMask);
    printf("ts:%u, size:%d, t:%s, h264 %s_frame:%d\n", time_stamp_, (int)nalus_.size(),
        srs_avc_nalu2str(nalu_type).c_str(), key_frame_?"k":"p", x.size);
  }
#endif
  return srs_success;
}

//MediaRtcLiveAdaptor
MediaRtcLiveAdaptor::MediaRtcLiveAdaptor(const std::string& stream_id) 
  : stream_id_{stream_id} {
}

void MediaRtcLiveAdaptor::onFrame(const owt_base::Frame& frm) {
  srs_error_t err = srs_success;
  if (frm.format == owt_base::FRAME_FORMAT_OPUS) {
    if (nullptr == codec_) {
      codec_ = std::move(std::make_unique<SrsAudioTranscoder>());
      
      SrsAudioTranscoder::AudioFormat from, to;
      from.codec = SrsAudioCodecIdOpus;
      to.codec = SrsAudioCodecIdAAC;

      from.samplerate = frm.additionalInfo.audio.sampleRate;  
      from.bitpersample = 16;
      from.channels = frm.additionalInfo.audio.channels;
      from.bitrate = from.samplerate * from.bitpersample * from.channels;
      
      to.samplerate = 44100; // The output audio sample rate in HZ.
      to.bitpersample = 16;
      to.channels = 2;       //stero
      to.bitrate = 48000; // The output audio bitrate in bps.

      if ((err = codec_->initialize(from, to)) != srs_success) {
        assert(false);
        MLOG_CERROR("transcoder initialize failed, code:%d, desc:%s",
            srs_error_code(err), srs_error_desc(err));
        delete err;
      }
    }

    if ((err = Trancode_audio(frm)) != srs_success) {
      MLOG_CERROR("transcode audio failed, code:%d, desc:%s",
          srs_error_code(err), srs_error_desc(err));
      delete err;
    }
  } else if (frm.format == owt_base::FRAME_FORMAT_H264 ) {
    if ((err = PacketVideo(frm)) != srs_success) {
      MLOG_CERROR("packet video failed, code:%d, desc:%s",
          srs_error_code(err), srs_error_desc(err));
      delete err;
    }
    dump_video(frm.payload, frm.length);
  } else {
    MLOG_CFATAL("unknown media format:%d", frm.format);
  }
}

void MediaRtcLiveAdaptor::dump_video(uint8_t* buf, uint32_t count) {
  if (!debug_) {
    return;
  }
  srs_error_t err = srs_success;
  if (video_writer_ && (srs_success != (err = video_writer_->write(buf, count, nullptr)))) {
     MLOG_CFATAL("to_file failed, code:%d, desc:%s", 
                 srs_error_code(err), srs_error_desc(err).c_str());
     delete err;
  }
}

void MediaRtcLiveAdaptor::open_dump() {
  if (!debug_) {
    return;
  }
  
  video_writer_ = std::move(std::make_unique<SrsFileWriter>());
  
   std::string file_writer_path = "/tmp/" + stream_id_ + "_rtc.h264";
   srs_error_t err = srs_success;
   if (srs_success != (err = video_writer_->open(file_writer_path))) {
     MLOG_CFATAL("open rtc file writer failed, code:%d, desc:%s", 
                 srs_error_code(err), srs_error_desc(err).c_str());
     delete err;
     video_writer_.reset(nullptr);
     return;
   }

}

srs_error_t MediaRtcLiveAdaptor::PacketVideoKeyFrame(StapPackage& pkg) {
  srs_error_t err = srs_success;

  //packet record decode sequence header
  SrsSample* sps = pkg.get_sps();
  SrsSample* pps = pkg.get_pps();
  if (NULL == sps || NULL == pps) {
    return srs_error_new(ERROR_RTC_FRAME_MUXER, 
          "no sps or pps in stap-a sps: %p, pps:%p", sps, pps);
  } else {
    //type_codec1 + avc_type + composition time + 
    //fix header + count of sps + len of sps + sps + 
    //count of pps + len of pps + pps
    int nb_payload = 1 + 1 + 3 + 5 + 1 + 2 + sps->size + 1 + 2 + pps->size;
    
    MessageChain mc(nb_payload);

    SrsBuffer payload(mc);
    //TODO: call api
    payload.write_1bytes(0x17);// type(4 bits): key frame; code(4bits): avc
    payload.write_1bytes(0x0); // avc_type: sequence header
    payload.write_1bytes(0x0); // composition time
    payload.write_1bytes(0x0);
    payload.write_1bytes(0x0);
    payload.write_1bytes(0x01); // version
    payload.write_1bytes(sps->bytes[1]);
    payload.write_1bytes(sps->bytes[2]);
    payload.write_1bytes(sps->bytes[3]);
    payload.write_1bytes(0xff);
    payload.write_1bytes(0xe1);
    payload.write_2bytes(sps->size);
    payload.write_bytes(sps->bytes, sps->size);
    payload.write_1bytes(0x01);
    payload.write_2bytes(pps->size);
    payload.write_bytes(pps->bytes, pps->size);

    MessageHeader header;
    header.initialize_video(nb_payload, pkg.time_stamp_, 1);
    auto rtmp = std::make_shared<MediaMessage>();
    rtmp->create(&header, &mc);
    if (sink_ && (err = sink_->OnVideo(rtmp)) != srs_success) {
      return err;
    }
  }

  return PacketVideoRtmp(pkg);
}

srs_error_t MediaRtcLiveAdaptor::PacketVideoRtmp(StapPackage& pkg) {
  srs_error_t err = srs_success;
  //type_codec1 + avc_type + composition time + nalu size + nalu
  int nb_payload = 1 + 1 + 3 + pkg.nb_bytes();
  MessageChain mc(nb_payload);
  SrsBuffer payload(mc);
  if (pkg.key_frame_) {
    payload.write_1bytes(0x17); // type(4 bits): key frame; code(4bits): avc
  } else {
    payload.write_1bytes(0x27); // type(4 bits): inter frame; code(4bits): avc
  }
  payload.write_1bytes(0x01); // avc_type: nalu
  payload.write_1bytes(0x0);  // composition time
  payload.write_1bytes(0x0);
  payload.write_1bytes(0x0);

  pkg.encode(&payload);
  
  MessageHeader header;
  header.initialize_video(nb_payload, pkg.time_stamp_, 1);
  auto rtmp = std::make_shared<MediaMessage>();
  rtmp->create(&header, &mc);
  if (sink_ && (err = sink_->OnVideo(rtmp)) != srs_success) {
     MLOG_WARN("rtc on video")
  }
  return err;
}

srs_error_t MediaRtcLiveAdaptor::PacketVideo(const owt_base::Frame& frm) {
  static constexpr int kVideoSamplerate  = 90000/SRS_UTIME_MILLISECONDS;
  srs_error_t err = srs_success;

  uint32_t ts = frm.timeStamp/kVideoSamplerate;
  if (video_begin_ts_ == (uint32_t)-1) {
    video_begin_ts_ = ts;
  }
  SrsBuffer buffer((char*)frm.payload, frm.length);
  StapPackage pkg{frm.additionalInfo.video.isKeyFrame, ts - video_begin_ts_};
  if ((err = pkg.decode(&buffer)) != srs_success) {
    return err;
  }
  
  if (pkg.key_frame_) {
    return PacketVideoKeyFrame(pkg);
  } 

  return PacketVideoRtmp(pkg);
}

std::shared_ptr<MediaMessage> MediaRtcLiveAdaptor::PacketAudio( 
    char* data, int len, uint32_t pts, bool is_header) {
  if (audio_begin_ts_ == (uint32_t)-1) {
    audio_begin_ts_ = pts;
  }
  
  int rtmp_len = len + 2;
  MessageChain mc(rtmp_len);
  SrsBuffer stream(mc);
  uint8_t aac_flag = (SrsAudioCodecIdAAC << 4) | 
                     (SrsAudioSampleRate44100 << 2) | 
                     (SrsAudioSampleBits16bit << 1) | 
                     SrsAudioChannelsStereo;
  stream.write_1bytes(aac_flag);
  if (is_header) {
    stream.write_1bytes(0);
  } else {
    stream.write_1bytes(1);
  }
  stream.write_bytes(data, len);

  MessageHeader header;
  header.initialize_audio(rtmp_len, pts - audio_begin_ts_, 1);
  auto audio = std::make_shared<MediaMessage>();
  audio->create(&header, &mc);

  return std::move(audio);
}

srs_error_t MediaRtcLiveAdaptor::Trancode_audio(const owt_base::Frame& frm) {
  MA_ASSERT(frm.additionalInfo.audio.isRtpPacket);
  srs_error_t err = srs_success;

  uint32_t ts = frm.timeStamp/(48000/1000);
  
  if (is_first_audio_) {
    //audio sequence header
    int header_len = 0;
    uint8_t* header = nullptr;
    codec_->aac_codec_header(&header, &header_len);

    auto out_rtmp = PacketAudio((char*)header, header_len, ts, is_first_audio_);

    if (sink_ && (err = sink_->OnAudio(out_rtmp)) != srs_success) {
      return srs_error_wrap(err, "source on audio");
    }

    is_first_audio_ = false;
  }

  std::vector<SrsAudioFrame*> out_pkts;

  webrtc::RtpPacket rtp;
  bool ret = rtp.Parse(frm.payload, frm.length);
  MA_ASSERT_RETURN(ret, srs_error_new(ERROR_RTC_FRAME_MUXER, "rtp parse failed"));
  auto payload = rtp.payload();
  SrsAudioFrame frame;
  frame.add_sample((char*)payload.data(), payload.size());
  frame.dts = ts;
  frame.cts = 0;

  err = codec_->transcode(&frame, out_pkts);
  if (err != srs_success) {
    return err;
  }

  for (auto it = out_pkts.begin(); it != out_pkts.end(); ++it) {
    auto out_rtmp = PacketAudio((*it)->samples[0].bytes, 
                                (*it)->samples[0].size, 
                                ts, 
                                is_first_audio_);

    if (sink_ && (err = sink_->OnAudio(out_rtmp)) != srs_success) {
      err = srs_error_wrap(err, "source on audio");
      break;
    }
  }
  codec_->free_frames(out_pkts);

  return err;
}

} //namespace ma

