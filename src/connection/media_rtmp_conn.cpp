#include "connection/media_rtmp_conn.h"
#include "rtmp/media_rtmp_stack.h"
#include "connection/h/media_io.h"

namespace ma {

MediaRtmpConn::MediaRtmpConn(std::unique_ptr<IMediaIOFactory> fac, 
                             IMediaHttpHandler* handle)
    : io_(fac->CreateIO()), handler_(handle) { }

MediaRtmpConn::~MediaRtmpConn() { }

srs_error_t MediaRtmpConn::Start() {
  rtmp_ = std::make_shared<RtmpServerSide>(this);
  return rtmp_->Handshake(io_);
}

void MediaRtmpConn::Disconnect() {
  
}

std::string MediaRtmpConn::Ip() {
  if (rtmp_) {
    //return rtmp_->ProxyRealIp()
  }
  return "";
}

void MediaRtmpConn::OnConnect(std::shared_ptr<MediaRequest>, srs_error_t err) {

}

void MediaRtmpConn::OnClientInfo(RtmpConnType type, 
    std::string stream_name, srs_utime_t) {
      
}

void MediaRtmpConn::OnMessage(std::shared_ptr<MediaMessage>) {

}

void MediaRtmpConn::OnRedirect(bool accepted) {

}

} //namespace ma
