#include "handler/media_rtc_source.h"

#include "h/rtc_return_value.h"
#include "utils/json.h"
#include "utils/protocol_utility.h"
#include "http/h/http_message.h"
#include "http/h/http_protocal.h"
#include "media_source.h"
#include "media_source_mgr.h"
#include "media_server.h"
#include "rtmp/media_req.h"
#include "common/media_message.h"
#include "common/media_io.h"
#include "utils/media_kernel_buffer.h"
#include "utils/media_msg_chain.h"

namespace ma {

#define __OPTIMIZE__

MDEFINE_LOGGER(MediaRtcSource, "MediaRtcSource");

const uint32_t kNaluLongStartSequence = 0x1000000;
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

//MediaRtcSource
MediaRtcSource::MediaRtcSource(const std::string& id) 
  : stream_id_{id} {
  MLOG_TRACE(stream_id_);
}

MediaRtcSource::~MediaRtcSource() {
  MLOG_TRACE(stream_id_);
}

srs_error_t MediaRtcSource::Init(wa::rtc_api* rtc, 
                                 std::shared_ptr<IHttpResponseWriter> w, 
                                 const std::string& sdp, 
                                 const std::string& url) {
  writer_ = std::move(w);
  stream_url_ = url;
  task_queue_ = this;
  rtc_ = rtc;
  return Init_i(sdp);
}

srs_error_t MediaRtcSource::Init(wa::rtc_api* rtc, 
                                std::shared_ptr<IHttpResponseWriter> w, 
                                std::shared_ptr<ISrsHttpMessage> msg, 
                                const std::string& url) {                             
  writer_ = std::move(w);
  stream_url_ = url;
  task_queue_ = this; 
  rtc_ = rtc;
  msg->SignalOnBody_.connect(this, &MediaRtcSource::OnBody);

  return srs_success;
}

srs_error_t MediaRtcSource::Init_i(const std::string& isdp) {
  std::string sdp = srs_string_replace(isdp, "\\r\\n", "\r\n");

  MLOG_DEBUG("sdp:" << sdp);
  
  worker_ = g_source_mgr_.GetWorker();
  
  wa::TOption  t;
  t.connectId_ = "1";

  size_t found = 0;
  do {
    found = sdp.find("m=audio", found);
    if (found != std::string::npos) {
      found = sdp.find("a=mid:", found);
      if (found != std::string::npos) {
        wa::TTrackInfo track;
        found += 6;
        size_t found_end = sdp.find("\r\n", found);
        track.mid_ = sdp.substr(found, found_end-found);
        track.type_ = wa::media_audio;
        track.preference_.format_ = wa::EFormatPreference::p_opus;
        t.tracks_.emplace_back(track);
      }
    }

    found = sdp.find("m=video", found);
    if (found != std::string::npos) {
      found = sdp.find("a=mid:", found);
      if (found != std::string::npos) {
        found += 6;
        size_t found_end = sdp.find("\r\n", found);
        wa::TTrackInfo track;
        track.mid_ = sdp.substr(found, found_end-found);
        track.type_ = wa::media_video;
        track.preference_.format_ = wa::EFormatPreference::p_h264;
        track.preference_.profile_ = "42001f";
        t.tracks_.emplace_back(track);
      }
    }
  } while(found!=std::string::npos);

  t.call_back_ = shared_from_this();

  int ret = rtc_->publish(t, sdp);

  srs_error_t err = srs_success;
  if (ret != wa::wa_ok) {
    err = srs_error_wrap(err, "rtc publish failed, code:%d", ret);
  }

  return err;
}

void MediaRtcSource::OnBody(const std::string& body) {
  json::Object jobj = json::Deserialize(body);
  std::string sdp = std::move((std::string)jobj["sdp"]);
  srs_error_t err = this->Init_i(sdp);

  if (err != srs_success) {
    MLOG_CERROR("rtc source internal init failed, code:%d, desc:%s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;
  }
}

void MediaRtcSource::onFailed(const std::string&) {
  MLOG_DEBUG("");
}

void MediaRtcSource::onCandidate(const std::string&) {
  MLOG_DEBUG("");
}

void MediaRtcSource::onReady() {
  MLOG_DEBUG("");
}

void MediaRtcSource::onAnswer(const std::string& sdp) {
  MLOG_DEBUG(sdp);
  writer_->header()->set("Connection", "Close");

  json::Object jroot;
  jroot["code"] = 200;
  jroot["server"] = "ly rtc";
  jroot["sdp"] = std::move(srs_string_replace(sdp, "\r\n", "\\r\\n"));
  //jroot["sessionid"] = msid;
  
  std::string jsonStr = std::move(json::Serialize(jroot));

  srs_error_t err = writer_->write(jsonStr.c_str(), jsonStr.length());

  if(err != srs_success){
    MLOG_CERROR("send rtc answer failed, code=%d, %s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;

    return;
  }

  err = writer_->final_request();

  if(err != srs_success){
    MLOG_CERROR("final_request failed, code=%d, %s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;

    return;
  }

  OnPublish();

  open_dump();
}

srs_error_t MediaRtcSource::PacketVideoKeyFrame(StapPackage& pkg) {
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
    if ((err = source_->on_video(rtmp)) != srs_success) {
      return err;
    }
  }

  return PacketVideoRtmp(pkg);
}

srs_error_t MediaRtcSource::PacketVideoRtmp(StapPackage& pkg) {
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
  if ((err = source_->on_video(rtmp)) != srs_success) {
     MLOG_WARN("rtc on video")
  }
  return err;
}

srs_error_t MediaRtcSource::PacketVideo(const owt_base::Frame& frm) {
  static constexpr int kVideoSamplerate  = 90000/SRS_UTIME_MILLISECONDS;
  srs_error_t err = srs_success;
  SrsBuffer buffer((char*)frm.payload, frm.length);
  StapPackage pkg{frm.additionalInfo.video.isKeyFrame, frm.timeStamp/kVideoSamplerate};
  if ((err = pkg.decode(&buffer)) != srs_success) {
    return err;
  }
  
  if (pkg.key_frame_) {
    return PacketVideoKeyFrame(pkg);
  } 

  return PacketVideoRtmp(pkg);
}

srs_error_t MediaRtcSource::PacketAudio(const owt_base::Frame& f) {
  srs_error_t err = srs_success;
  return err;
}

void MediaRtcSource::onFrame(const owt_base::Frame& frm) {
  srs_error_t err = srs_success;
  if (frm.format == owt_base::FRAME_FORMAT_OPUS) {
    if ((err = PacketAudio(frm)) != srs_success) {
      delete err;
    }
  } else if (frm.format == owt_base::FRAME_FORMAT_H264 ) {
    if ((err = PacketVideo(frm)) != srs_success) {
      delete err;
    }
    dump_video(frm.payload, frm.length);
    //MLOG_CINFO("ts:%d, len:%d", frm.timeStamp, frm.length);
  } else {
    MLOG_CFATAL("unknown media format:%d", frm.format);
  }
}

void MediaRtcSource::onStat() {
  MLOG_DEBUG("");
}

void MediaRtcSource::OnPublish() {
  auto req = std::make_shared<MediaRequest>();
  req->tcUrl = stream_url_;
  req->stream = stream_id_;

  srs_discovery_tc_url(req->tcUrl, 
                       req->schema,
                       req->host, 
                       req->vhost, 
                       req->app, 
                       req->stream, 
                       req->port, 
                       req->param);

  MLOG_INFO("schema:" << req->schema << 
            ", host:" << req->host <<
            ", vhost:" << req->vhost << 
            ", app:" << req->app << 
            ", stream:" << req->stream << 
            ", port:" << req->port << 
            ", param:" << req->param);

  auto result = g_source_mgr_.FetchOrCreateSource(req->stream);

  if (!*result) {
    MLOG_ERROR("create source failed url:" << req->stream);
    return;
  }
  source_ = *result;

  g_server_.on_publish(result.value(), req);
  
  source_->on_publish();

  req_ = req;
}

void MediaRtcSource::post(Task t) {
  worker_->task(t);
}

void MediaRtcSource::dump_video(uint8_t* buf, uint32_t count) {
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

void MediaRtcSource::open_dump() {
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


} //namespace ma

