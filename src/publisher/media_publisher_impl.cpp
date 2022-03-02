//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "publisher/media_publisher_impl.h"

#include "common/media_log.h"
#include "media_source.h"
#include "rtmp/media_req.h"
#include "common/media_io.h"
#include "encoder/media_flv_encoder.h"
#include "utils/protocol_utility.h"
#include "media_server.h"
#include "media_source_mgr.h"
#include "rtmp/media_rtmp_const.h"
#include "utils/media_msg_chain.h"
#include "common/media_message.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.push");


///////////////////////////////////////////////////////////////////////////////
// MediaRtcPublisherImp
///////////////////////////////////////////////////////////////////////////////
void MediaRtcPublisherImp::OnPublish(const std::string& tcUrl, 
                                     const std::string& stream) {
  MLOG_INFO(tcUrl << ";" << stream);

  if (active_) {
    MLOG_ERROR("alread pushed");
    return;
  }

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
  req->vhost = g_server_.config_.vhost;
  
  MLOG_TRACE("schema:" << req->schema << 
            ", host:" << req->host <<
            ", vhost:" << req->vhost << 
            ", app:" << req->app << 
            ", stream:" << req->stream << 
            ", port:" << req->port << 
            ", param:" << req->param);

  MediaSource::Config cfg = {
      .worker = nullptr,
      .gop = g_server_.config_.enable_gop_,
      .jitter_algorithm = JitterAlgorithmZERO,
      .rtc_api = nullptr,
      .enable_rtc2rtmp_ = g_server_.config_.enable_rtc2rtmp_,
      .enable_rtmp2rtc_ = g_server_.config_.enable_rtmp2rtc_,
      .consumer_queue_size_ = g_server_.config_.enable_rtmp2rtc_,
      .mix_correct_ = g_server_.config_.mix_correct_};

  source_ = g_source_mgr_.FetchOrCreateSource(cfg, req);

  // local rtc publisher
  source_->OnPublish(eLocalRtc);
  active_ = true;
  req_ = req;
}

void MediaRtcPublisherImp::OnUnpublish() {
  if (!active_) {
    MLOG_ERROR("not pushed, " << req_->get_stream_url());
    return;
  }
  
  // local rtc publisher
  source_->OnUnpublish();
  source_ = nullptr;

  g_source_mgr_.RemoveSource(req_);
  active_ = false;
}

void MediaRtcPublisherImp::OnFrame(owt_base::Frame& frm) {
  if (active_) {
    source_->OnFrame(std::make_shared<owt_base::Frame>(frm));
  }
}

///////////////////////////////////////////////////////////////////////////////
// MediaRtmpPublisherImp
///////////////////////////////////////////////////////////////////////////////
void MediaRtmpPublisherImp::OnPublish(
    const std::string& tcUrl, const std::string& stream) {
  MLOG_INFO(tcUrl << ";" << stream);

  if (active_) {
    MLOG_INFO("alread pushed");
    return;
  }

  active_ = true;

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
  req->vhost = g_server_.config_.vhost;
  MLOG_INFO("tcurl:" << req->tcUrl <<
            ", schema:" << req->schema << 
            ", host:" << req->host <<
            ", vhost:" << req->vhost << 
            ", app:" << req->app << 
            ", stream:" << req->stream << 
            ", port:" << req->port << 
            ", param:" << req->param);

  MediaSource::Config cfg = {
        .worker = nullptr,
        .gop = g_server_.config_.enable_gop_,
        .jitter_algorithm = JitterAlgorithmZERO,
        .rtc_api = nullptr,
        .enable_rtc2rtmp_ = g_server_.config_.enable_rtc2rtmp_,
        .enable_rtmp2rtc_ = g_server_.config_.enable_rtmp2rtc_,
        .consumer_queue_size_ = g_server_.config_.enable_rtmp2rtc_,
        .mix_correct_ = g_server_.config_.mix_correct_};

  source_ = g_source_mgr_.FetchOrCreateSource(cfg, req);

  req_ = req;

  // local rtmp publisher
  source_->OnPublish(eLocalRtmp);
  ToFile();
}

void MediaRtmpPublisherImp::OnUnpublish() {
  if (!active_) {
    MLOG_ERROR("not pushed, " << req_->get_stream_url());
    return;
  }

  // local rtmp publisher
  source_->OnUnpublish();
  active_ = false;
}

void MediaRtmpPublisherImp::OnVideo(
    const uint8_t* data, uint32_t len, uint32_t timestamp) {

  MessageHeader header;

  header.payload_length = len;

  header.message_type = RTMP_MSG_VideoMessage;

  header.timestamp = timestamp;

  MessageChain msg_pkt(len, (const char*)data, MessageChain::DONT_DELETE, len);
  auto msg = std::make_shared<MediaMessage>(&header, &msg_pkt);

  if (debug_) {
    std::vector< std::shared_ptr<MediaMessage> > msgs;
    msgs.push_back(msg);
  
    srs_error_t ret = flv_encoder_->write_tags(msgs);

    if (ret != srs_success) {
      MLOG_ERROR("write audio tags faild code:" << srs_error_code(ret) << 
                 ", desc:" << srs_error_desc(ret));
      delete ret;
    }
  }
  
  source_->OnMessage(std::move(msg));
}

void MediaRtmpPublisherImp::OnAudio(
    const uint8_t* data, uint32_t len, uint32_t timestamp) {
  MessageHeader header;

  header.payload_length = len;

  header.message_type = RTMP_MSG_AudioMessage;

  header.timestamp = timestamp;

  MessageChain msg_pkt(len, (const char*)data, MessageChain::DONT_DELETE, len); 
  auto msg = std::make_shared<MediaMessage>(&header, &msg_pkt);

  if (debug_) {
    std::vector< std::shared_ptr<MediaMessage> > msgs;
    msgs.push_back(msg);
    
    srs_error_t ret = flv_encoder_->write_tags(msgs);
    if (ret != srs_success) {
      MLOG_ERROR("write audio tags faild code:" << srs_error_code(ret) << 
                 ", desc:" << srs_error_desc(ret));
      delete ret;
    }
  }
  source_->OnMessage(std::move(msg));
}

void MediaRtmpPublisherImp::ToFile() {
  if (!debug_) {
    return;
  }
  
  file_writer_.reset(new SrsFileWriter);
  
   std::string file_writer_path = "/tmp/rtmppush" + 
        srs_string_replace(req_->get_stream_url(), "/", "_") + "_d.flv";
   srs_error_t result = srs_success;
   if (srs_success != (result = file_writer_->open(file_writer_path))) {
     MLOG_CFATAL("open file writer failed, code:%d, desc:%s", 
                 srs_error_code(result), srs_error_desc(result).c_str());
     delete result;
     return;
   }
   
   flv_encoder_.reset(new SrsFlvStreamEncoder);

   if (srs_success != (result = flv_encoder_->initialize(file_writer_.get(), NULL))) {
     MLOG_CFATAL("init encoder, code:%d, desc:%s", 
                 srs_error_code(result), srs_error_desc(result).c_str());
     delete result;
   }
}

///////////////////////////////////////////////////////////////////////////////
// factory
///////////////////////////////////////////////////////////////////////////////
std::shared_ptr<MediaRtcPublisherApi> MediaRtcPublisherFactory::Create() {
  return std::dynamic_pointer_cast<MediaRtcPublisherApi>(
      std::make_shared<MediaRtcPublisherImp>());
}

std::shared_ptr<MediaRtmpPublisherApi> MediaRtmpPublisherFactory::Create() {
  return std::dynamic_pointer_cast<MediaRtmpPublisherApi>(
      std::make_shared<MediaRtmpPublisherImp>());
}


}

