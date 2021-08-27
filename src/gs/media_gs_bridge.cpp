#ifdef __GS__

#include "media_gs_bridge.h"

#include "tsvideoflv.h"
#include "common/media_message.h"
#include "media_source.h"
#include "common/media_log.h"
#include "media_source_mgr.h"
#include "rtmp/media_req.h"
#include "media_server.h"
#include "utils/protocol_utility.h"
#include "common/media_io.h"
#include "encoder/media_flv_encoder.h"
#include "rtmp/media_rtmp_const.h"

namespace ma
{

MediaBridge::MediaBridge()
  : flv_adaptor_{std::make_unique<MediaCache>()} {
}

MediaBridge::~MediaBridge() = default;

void MediaBridge::OnPublish(const std::string& tcUrl, const std::string& stream) {
  MLOG_INFO(tcUrl << ";" << stream);

  if (pushed_) {
    MLOG_FATAL("alread pushed");
    return;
  }

  pushed_ = true;

  auto req = std::make_shared<MediaRequest>();
  req->tcUrl = tcUrl;
  req->stream = stream;

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

  int ret = flv_adaptor_->init(IMediaAdapter::T_AAC, nullptr);
  if (0 != ret) {
    MLOG_FATAL("MediaCache init faild code" << ret);
    return;
  }
  
  flv_adaptor_->open(this, this);

  req_ = req;

  g_server_.on_publish(result.value(), req_);
  
  source_->on_publish();

  to_file();
}

void MediaBridge::OnUnpublish() {

  if (!pushed_) {
    MLOG_FATAL("not pushed, " << req_->tcUrl << ", " << req_->stream);
    return;
  }

  pushed_ = false;

  source_->on_unpublish();
  g_server_.on_unpublish(source_, req_);
  g_source_mgr_.removeSource(req_->get_stream_url());
}

void MediaBridge::OnVideo(CDataPackage& msg, 
                          uint32_t timestamp, 
                          IMediaPublisher::Type t) {

  AVCPackageType avc_type = AVC_P_FRAME;
  if (t==IMediaPublisher::t_video_avg) {
    avc_type = AVC_CONFIG;
  } else if (t==IMediaPublisher::t_video_k) {
    avc_type = AVC_K_FRAME;
  }

  flv_adaptor_->on_video(msg, timestamp, avc_type);
}

void MediaBridge::OnAudio(CDataPackage& msg, uint32_t timestamp) {

  flv_adaptor_->on_audio(msg, timestamp);
}

void MediaBridge::on_audio(CDataPackage& data, DWORD dwTimeStamp) {

  MessageHeader header;

  header.payload_length = data.GetPackageLength();

  header.message_type = RTMP_MSG_AudioMessage;

  header.timestamp = dwTimeStamp;

  auto audio_block = DataBlock::Create(header.payload_length, nullptr);

  data.Write(audio_block->GetBasePtr(), audio_block->GetCapacity());
  
  auto a = MediaMessage::create(&header, std::move(audio_block));

  if (debug_) {
    std::vector<std::shared_ptr<MediaMessage>> msgs{a};
    
    srs_error_t ret = flv_encoder_->write_tags(msgs);
    if (ret != srs_success) {
      MLOG_ERROR("write audio tags faild code:" << srs_error_code(ret) << 
                 ", desc:" << srs_error_desc(ret));
      delete ret;
    }
  }
  source_->on_audio(std::move(a));
}

void MediaBridge::on_video(CDataPackage& data, DWORD dwTimeStamp, AVCPackageType) {

  MessageHeader header;

  header.payload_length = data.GetPackageLength();

  header.message_type = RTMP_MSG_VideoMessage;

  header.timestamp = dwTimeStamp;

  auto video_block = DataBlock::Create(header.payload_length, nullptr);

  data.Write(video_block->GetBasePtr(), video_block->GetCapacity());

  auto a = MediaMessage::create(&header, std::move(video_block));

  if (debug_) {
    std::vector<std::shared_ptr<MediaMessage>> msgs{a};
  
    srs_error_t ret = flv_encoder_->write_tags(msgs);

    if (ret != srs_success) {
      MLOG_ERROR("write audio tags faild code:" << srs_error_code(ret) << 
                 ", desc:" << srs_error_desc(ret));
      delete ret;
    }
  }
  
  source_->on_video(std::move(a));
}

void MediaBridge::to_file() {
  if (!debug_) {
    return;
  }
  
  file_writer_ = std::move(std::make_unique<SrsFileWriter>());
  
   std::string file_writer_path = "/tmp/" + req_->stream + "_d.flv";
   srs_error_t result = srs_success;
   if (srs_success != (result = file_writer_->open(file_writer_path))) {
     MLOG_CFATAL("open file writer failed, code:%d, desc:%s", 
                 srs_error_code(result), srs_error_desc(result).c_str());
     delete result;
     return;
   }
   
   flv_encoder_ = std::move(std::make_unique<SrsFlvStreamEncoder>());

   if (srs_success != (result = flv_encoder_->initialize(file_writer_.get(), nullptr))) {
     MLOG_CFATAL("init encoder, code:%d, desc:%s", 
                 srs_error_code(result), srs_error_desc(result).c_str());
     delete result;
   }
}

std::shared_ptr<IMediaPublisher> MediaPublisherFactory::Create() {
  return std::make_shared<MediaBridge>();
}

} //namespace ma

#endif //__GS__

