#include "connection/media_rtmp_conn.h"
#include "rtmp/media_rtmp_stack.h"
#include "connection/h/media_io.h"
#include "rtmp/media_req.h"
#include "common/media_message.h"
#include "utils/media_protocol_utility.h"
#include "media_source.h"
#include "media_source_mgr.h"
#include "media_server.h"

namespace ma {

MDEFINE_LOGGER(MediaRtmpConn, "ma.Rtmp");

MediaRtmpConn::MediaRtmpConn(std::unique_ptr<IMediaIOFactory> fac, 
                             IMediaHttpHandler* handle)
    : io_(fac->CreateIO()), handler_(handle) { }

MediaRtmpConn::~MediaRtmpConn() = default;

srs_error_t MediaRtmpConn::Start() {
  MLOG_TRACE("");
  rtmp_ = std::make_shared<RtmpServerSide>(this);
  return rtmp_->Handshake(io_);
}

void MediaRtmpConn::Disconnect() {
  MLOG_TRACE("");
}

std::string MediaRtmpConn::Ip() {
  if (rtmp_) {
    //return rtmp_->ProxyRealIp()
  }
  return "";
}

srs_error_t MediaRtmpConn::OnConnect(std::shared_ptr<MediaRequest> req) {
  srs_error_t err = srs_success;
  cli_info_.req_ = req;

  // set client ip to request.
  req->ip = io_->GetRemoteAddress();

  MLOG_CTRACE("connect app, tcUrl=%s, pageUrl=%s, swfUrl=%s, schema=%s, vhost=%s, port=%d, app=%s, args=%s",
    req->tcUrl.c_str(), req->pageUrl.c_str(), req->swfUrl.c_str(),
    req->schema.c_str(), req->vhost.c_str(), req->port,
    req->app.c_str(), (req->args? "(obj)":"null"));
  
  int out_ack_size = g_server_.config_.out_ack_size_;
  if (out_ack_size && (err = rtmp_->SetWinAckSize(out_ack_size)) != srs_success) {
    return srs_error_wrap(err, "rtmp: set out window ack size");
  }
  
  int in_ack_size = g_server_.config_.in_ack_size_;
  if (in_ack_size && (err = rtmp_->SetInWinAckSize(in_ack_size)) != srs_success) {
    return srs_error_wrap(err, "rtmp: set in window ack size");
  }
  
  if ((err = rtmp_->SetPeerBandwidth((int)(2.5 * 1000 * 1000), 2)) != srs_success) {
    return srs_error_wrap(err, "rtmp: set peer bandwidth");
  }
  
  // set chunk size to larger.
  // set the chunk size before any larger response greater than 128,
  // to make OBS happy, @see https://github.com/ossrs/srs/issues/454
  int chunk_size = g_server_.config_.chunk_size_;
  if ((err = rtmp_->SetChunkSize(chunk_size)) != srs_success) {
    return srs_error_wrap(err, "rtmp: set chunk size %d", chunk_size);
  }
  
  // get the ip which client connected.
    std::string local_ip = io_->GetLocalAddress();

  // response the client connect ok.
  if ((err = rtmp_->ResponseConnect(req.get(), local_ip.c_str())) != srs_success) {
    return srs_error_wrap(err, "rtmp: response connect app");
  }
  
  if ((err = rtmp_->OnBwDone()) != srs_success) {
      return srs_error_wrap(err, "rtmp: on bw down");
  }
  return err;
}

srs_error_t MediaRtmpConn::OnClientInfo(RtmpConnType type, 
    std::string stream_name, srs_utime_t ts) {
  
  if (stream_name.empty())
    return srs_error_new(ERROR_RTMP_STREAM_NAME_EMPTY, "rtmp: empty stream");

  cli_info_.type = type;
  auto req = cli_info_.req_;
  req->stream = stream_name;
  req->duration = ts;
  srs_discovery_tc_url(req->tcUrl, req->schema, req->host, req->vhost, req->app, req->stream, req->port, req->param);
  req->strip();

  MLOG_TRACE("client identified, type=" << RtmpClientTypeString(cli_info_.type) 
      << ", vhost=" << req->vhost << ", app=" << req->app << ", stream=" << req->stream
      << ", param=" << req->param << ", duration=" << srsu2msi(req->duration) << "ms");

  if (req->schema.empty() || req->vhost.empty() || req->port == 0 || req->app.empty()) {
    return srs_error_new(ERROR_RTMP_REQ_TCURL, "discovery tcUrl failed, tcUrl=%s, schema=%s, vhost=%s, port=%d, app=%s",
        req->tcUrl.c_str(), req->schema.c_str(), req->vhost.c_str(), req->port, req->app.c_str());
  }

  // find a source to serve.
  MediaSource::Config cfg;
  source_ = g_source_mgr_.FetchOrCreateSource(cfg, req);
  MA_ASSERT(source_);
  srs_error_t err = srs_success;

  switch (type) {
    case RtmpConnPlay: {
      // response connection start play
      if ((err = rtmp_->StartPlay(cli_info_.stream_id_)) != srs_success) {
        return srs_error_wrap(err, "rtmp: start play");
      }

      //if ((err = http_hooks_on_play()) != srs_success) {
      //  return srs_error_wrap(err, "rtmp: callback on play");
      //}
      
      //err = playing(source);
      //http_hooks_on_stop();
      return err;
    }
    case RtmpConnFMLEPublish: {
      if ((err = rtmp_->StartFmlePublish(cli_info_.stream_id_)) != srs_success) {
        return srs_error_wrap(err, "rtmp: start FMLE publish");
      }
      break;
    }
    case RtmpConnHaivisionPublish: {
      if ((err = rtmp_->StartHaivisionPublish(cli_info_.stream_id_)) != srs_success) {
        return srs_error_wrap(err, "rtmp: start HAIVISION publish");
      }
      
      break;
    }
    case RtmpConnFlashPublish: {
      if ((err = rtmp_->StartFlashPublish(cli_info_.stream_id_)) != srs_success) {
        return srs_error_wrap(err, "rtmp: start FLASH publish");
      }
      
      break;
    }
    default: {
      return srs_error_new(ERROR_SYSTEM_CLIENT_INVALID, "rtmp: unknown client type=%d", type);
    }
  }

  return err;
}

srs_error_t MediaRtmpConn::OnMessage(std::shared_ptr<MediaMessage> msg) {
  srs_error_t err = srs_success;
  MLOG_TRACE(msg->size_);
  return err;
}

srs_error_t MediaRtmpConn::OnRedirect(bool accepted) {
  srs_error_t err = srs_success;
  MLOG_TRACE(accepted? "accept" : "denied");
  return err;
}

void MediaRtmpConn::OnDisc(srs_error_t err) {
  MLOG_TRACE(srs_error_desc(err));
  delete err;
}

} //namespace ma
