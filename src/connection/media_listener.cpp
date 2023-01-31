#include "connection/media_listener.h"

#include "h/media_return_code.h"
#include "utils/sigslot.h"
#include "utils/media_protocol_utility.h"
#include "common/media_log.h"
#include "http/h/http_protocal.h"
#include "connection/h/conn_interface.h"
#include "connection/h/media_conn_mgr.h"
#include "media_server.h"
#include "utils/media_acceptor.h"
#include "utils/media_thread.h"
#include "utils/media_msg_queue.h"

namespace ma {
static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.connection");

//MediaListenerBase
class MediaListenerBase: public MediaListenerMgr::IMediaListener,
    public AcceptorSink {
 public:
  MediaListenerBase() = default;
  ~MediaListenerBase() override = default;
  srs_error_t Listen(const MediaAddress&) override;
  srs_error_t Stop() override;
 private:
  std::shared_ptr<Acceptor> acceptor_;
};

srs_error_t MediaListenerBase::Listen(const MediaAddress& address) {
  srs_error_t err = srs_success;
  if (acceptor_) {
    return err;
  }

  acceptor_ = AcceptorFactory::CreateAcceptor(true);

  if (srs_success != (err = acceptor_->Listen(this, address))) {
    return srs_error_wrap(err, "rtmp listen failed");
  }

  return err;
}

srs_error_t MediaListenerBase::Stop() {
  srs_error_t err = srs_success;
  if (!acceptor_) {
    return err;
  }
  if (srs_success != (err = acceptor_->Stop())) {
    return srs_error_wrap(err, "rtmp stop listen failed");
  }
  return err;
}

//MediaRtmpListener
class MediaRtmpListener final : public MediaListenerBase  {
 public:
  // AcceptorSink implement
  void OnAccept(std::shared_ptr<Transport>) override;
 private:
  std::shared_ptr<Acceptor> acceptor_;
};

void MediaRtmpListener::OnAccept(std::shared_ptr<Transport> t) {
  MLOG_TRACE("new peer:" << t->GetLocalAddr().ToString() << 
             ", from:" << t->GetLocalAddr().ToString());
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_rtmp, std::move(CreateTcpIOFactory(std::move(t))));

  srs_error_t err = conn->Start();
  if (srs_success != err) {
    MLOG_ERROR("rtmp connection start failed, desc:" << srs_error_desc(err));
    delete err;
  }
}

//MediaHttpListener
class MediaHttpListener final: public MediaListenerBase  {
 public:
  MediaHttpListener() = default;
  // AcceptorSink implement
  void OnAccept(std::shared_ptr<Transport>) override;

 private:
  std::shared_ptr<Acceptor> acceptor_;
};

void MediaHttpListener::OnAccept(std::shared_ptr<Transport> t) {
  MLOG_TRACE("new peer:" << t->GetLocalAddr().ToString() << 
             ", from:" << t->GetLocalAddr().ToString());
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_http, std::move(CreateHttpProtocalFactory(std::move(t), false)));

  srs_error_t err = conn->Start();
  if (srs_success != err) {
    MLOG_ERROR("http connection start failed, desc:" << srs_error_desc(err));
    delete err;
  }
}

///////////////////////////////////////////////////////////////////////////////
//MediaHttpsListener
///////////////////////////////////////////////////////////////////////////////
class MediaHttpsListener : public MediaListenerBase {
 public:
  MediaHttpsListener();
  ~MediaHttpsListener() override;
  
  // AcceptorSink implement
  void OnAccept(std::shared_ptr<Transport>) override;
 private:
  void CheckInit();
  void CheckClean();
 private:
  static bool inited_;
};

bool MediaHttpsListener::inited_{false};

MediaHttpsListener::MediaHttpsListener() {
  CheckInit();
}

MediaHttpsListener::~MediaHttpsListener() {
  CheckClean();
}

void MediaHttpsListener::OnAccept(std::shared_ptr<Transport> t) {
  MLOG_TRACE("new peer:" << t->GetLocalAddr().ToString() << 
             ", from:" << t->GetLocalAddr().ToString());
  auto conn = g_conn_mgr_.CreateConnection(
      MediaConnMgr::e_http, std::move(CreateHttpProtocalFactory(std::move(t), true)));

  srs_error_t err = conn->Start();
  if (srs_success != err) {
    MLOG_ERROR("https connection start failed, desc:" << srs_error_desc(err));
    delete err;
  }
}

void MediaHttpsListener::CheckInit() {
  if (!inited_) {
    //rtc::InitializeSSL();
    inited_ = true;
  }
}

void MediaHttpsListener::CheckClean() {
  if (inited_) {
    //rtc::CleanupSSL();
    inited_ = false;
  }
}

///////////////////////////////////////////////////////////////////////////////
//MediaHttpsListener
///////////////////////////////////////////////////////////////////////////////
MediaListenerMgr::MediaListenerMgr() = default;

srs_error_t MediaListenerMgr::Init(const std::vector<std::string>& addr) {
  worker_ = MediaThreadManager::Instance()->GetDefaultNetThread();
  if (!worker_) {
    MediaThreadManager::Instance()->CreateNetThread("reactor");
    worker_->Run();
  }

  srs_error_t err = srs_success;
  for (auto& i : addr) {
    std::string_view schema, host;
    int port;
    split_schema_host_port(i, schema, host, port);
    MediaAddress host_port((std::string)host, port);
    MLOG_TRACE(i << "[schema:" << schema << ", host:" << host
        << ", port:" << port << "]");

    err = worker_->MsgQueue()->Send([this, schema, host_port]() {
      std::unique_ptr<IMediaListener> listener(CreateListener(schema));
      srs_error_t ret = srs_success;
      if ((ret = listener->Listen(host_port)) != srs_success) {
        return srs_error_wrap(ret, "Listen failed, %s", host_port.ToString().c_str());
      }
      listeners_.emplace_back(std::move(listener));
      return ret;
    });
    if (err) {
      return srs_error_wrap(err, "send listen msg failed");
    }
  }

  return err;
}

void MediaListenerMgr::Close() {
  for(auto& i : listeners_) {
    i->Stop();
  }
  listeners_.clear();

  if (worker_) {
    worker_->Stop();
    worker_->Join();
    worker_->Destroy();
  }
}

MediaListenerMgr::IMediaListener*
MediaListenerMgr::CreateListener(std::string_view schema) {
  IMediaListener* p;
  if (schema == "rtmp") {
    p = new MediaRtmpListener;
  }

  if (schema == "http") {
    p = new MediaHttpListener;
  }

  if (schema == "https") {
    p = new MediaHttpsListener;
  }

  return p;
}

} //namespace ma

