#include "connection/media_rtmp_conn.h"
#include "rtmp/media_rtmp_stack.h"
#include "connection/h/media_io.h"
#include "rtmp/media_req.h"
#include "common/media_message.h"
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

void MediaRtmpConn::OnClientInfo(RtmpConnType type, 
    std::string stream_name, srs_utime_t) {
  MLOG_TRACE(stream_name);
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

} //namespace ma
