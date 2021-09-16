#ifdef __GS__

#include "http/http_protocal_gs_impl.h"

#include "netaddress.h"
#include "http/http_consts.h"
#include "http/http_stack.h"
#include "networkbase.h"
#include "common/media_io.h"

namespace ma {

GsHttpMessageParser::GsHttpMessageParser(IHttpServer* p) 
    : conn_(p) {
}

srs_error_t GsHttpMessageParser::parse_message(
    std::string_view body, std::shared_ptr<ISrsHttpMessage>& out)
{
  std::string method;
  conn_->GetRequestMethod(method);

  // fill header
  SrsHttpHeader header;
  auto& headers = conn_->GetRequestHeaders();

  for(auto& item : headers){
    header.set(item.first.get(), item.second);
  }

  //product message
  std::shared_ptr<HttpMessage> msg(std::make_shared<HttpMessage>(body));

  // Initialize the basic information.
  msg->set_basic(HTTP_REQUEST, 
                 method, 
                 0, 
                 header.content_length());
                 
  msg->set_header(header, true);

  std::string url;
  conn_->GetRequestPath(url);
  srs_error_t err = srs_success;
  if ((err = msg->set_url(url, false)) != srs_success) {
    return srs_error_wrap(err, "set url %s", url.c_str());
  }
  
  // parse ok, return the msg.
  out = std::move(msg);
  return err;
}

GsHttpRequestReader::GsHttpRequestReader(
    IHttpServer* conn, CDataPackage* msg, CallBack* callback)
    : conn_{conn}, req_{msg}, callback_{callback} {

  conn_->OpenWithSink(this);
}

GsHttpRequestReader::~GsHttpRequestReader() {
  if (conn_) {
    conn_->Disconnect(0);
    conn_ = nullptr;
  }
}

void GsHttpRequestReader::open() {
  std::string str_req = req_.Get()->FlattenPackage();
  callback_->process_request(str_req);
}

void GsHttpRequestReader::disconnect() {
  if (conn_) {
    conn_->Disconnect(RV_OK);
    conn_ = nullptr;
  }
  callback_ = nullptr;
}

//ITransportSink implement
void GsHttpRequestReader::OnReceive(CDataPackage& inData, ITransport* inTransport) {
  std::string req = inData.FlattenPackage();
  srs_error_t err = callback_->process_request(req);
  if (err != srs_success) {
    MLOG_ERROR("code:" << srs_error_code(err) << ", " << srs_error_desc(err));
    delete err;
  }
}

void GsHttpRequestReader::OnSend(ITransport* inTransport) {
  conn_->Response();
}

void GsHttpRequestReader::OnDisconnect(OsResult, ITransport*) {
  MLOG_INFO("");
  if (conn_) {
    conn_->Disconnect(RV_OK);
    conn_ = nullptr;
  }
  if (callback_) {
    callback_->on_disconnect();
    callback_ = nullptr;
  }
}

GsHttpResponseWriter::GsHttpResponseWriter(IHttpServer* p, bool flag_stream)
    : conn_{p}, status_{SRS_CONSTS_HTTP_OK} {
  if (flag_stream) {
    BOOL stream = TRUE;
    conn_->SetOption(TP_OPT_HTTP_STREAM, &stream);
  }

#ifdef __DUMP_PEER_STREAM__
  CNetAddress peer_addr;  
  int ret = conn_->GetOption(TP_OPT_TRANSPORT_PEER_ADDR, &peer_addr);
  MA_ASSERT(ret == 0);

  std::string peer_name = peer_addr.GetWholeAddress();  
  file_dump_ = std::make_unique<SrsFileWriter>();

  srs_error_t result = file_dump_->open("/tmp/" + peer_name + ".flv");
  if (result != srs_success) {
    MLOG_ERROR("open file dump failed.");
    delete result;  
  }
#endif
}

//IHttpResponseWriter implement
srs_error_t GsHttpResponseWriter::final_request() {
  srs_error_t err = srs_success;

  // write the header data in memory.
  if (!header_wrote_) {
      write_header(SRS_CONSTS_HTTP_OK);
  }

  // whatever header is wrote, we should try to send header.
  if ((err = send_header(NULL)) != srs_success) {
    return srs_error_wrap(err, "send header");
  }

    // complete the chunked encoding.
  if (content_length_ == -1) {
    std::stringstream ss;
    ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
    std::string ch = ss.str();

    CDataPackage data_end(ch.length(), ch.data(), CDataPackage::DONT_DELETE, ch.length());
    conn_->PrepareResponse(data_end);
  }
  
  // flush when send with content length
  int ret = conn_->Response();

  //header_.clear();
  //header_wrote_ = false;
  //header_sent_ = false;
  //written_ = 0;
  
  return RV_SUCCEEDED(ret) ? err : srs_error_new(ret, "failed");
}

SrsHttpHeader* GsHttpResponseWriter::header() {
  return &header_;
}

srs_error_t GsHttpResponseWriter::write(const char* data, int size) {
  // write the header data in memory.
  if (!header_wrote_) {
    if (header_.content_type().empty()) {
      if (json_){
        header_.set_content_type("application/json");
      } else {
        header_.set_content_type("text/plain; charset=utf-8");
      }
    }
    if (header_.content_length() == -1) {
      header_.set_content_length(size);
    }
    write_header(SRS_CONSTS_HTTP_OK);
  }

  srs_error_t err = srs_success;

  // whatever header is wrote, we should try to send header.
  if ((err = send_header(data)) != srs_success) {
      return srs_error_wrap(err, "send header");
  }
  
  // check the bytes send and content length.
  written_ += size;
  if (content_length_ != -1 && written_ > content_length_) {
    return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, 
      "overflow writen=%d, max=%d", (int)written_, (int)content_length_);
  }

  // ignore NULL content.
  if (!data || size <= 0) {
    return err;
  }

#ifdef __DUMP_PEER_STREAM__
  if ((err = file_dump_->write((void*)data, size, nullptr)) != srs_success) {
    MLOG_CERROR("write file dump error, %d:%s", srs_error_code(err), srs_error_desc(err).c_str());
    delete err;
  }
#endif

  // directly send with content length
  if (content_length_ != -1) {
    CDataPackage packet(size, data, CDataPackage::DONT_DELETE, size);
    conn_->PrepareResponse(packet);
    int ret = conn_->Response();
    return RV_SUCCEEDED(ret) ? err: 
        srs_error_new(ret==RV_ERROR_PARTIAL_DATA?ERROR_SOCKET_WOULD_BLOCK:ret, "send data");
  }
  
  // send in chunked encoding.
  int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);

  CDataPackage packet0(nb_size, header_cache_, CDataPackage::DONT_DELETE, nb_size);
  CDataPackage packet1(2, (const char*)SRS_HTTP_CRLF, CDataPackage::DONT_DELETE, 2);
  CDataPackage packet2(size, (const char*)data, CDataPackage::DONT_DELETE, size);
  CDataPackage packet3(2, (const char*)SRS_HTTP_CRLF, CDataPackage::DONT_DELETE, 2);

  packet0.Append(&packet1);
  packet1.Append(&packet2);
  packet2.Append(&packet3);
  
  conn_->PrepareResponse(packet0);
  int ret = conn_->Response();
  return RV_SUCCEEDED(ret) ? err: 
      srs_error_new(ret==RV_ERROR_PARTIAL_DATA?ERROR_SOCKET_WOULD_BLOCK:ret, "send chunk");
}

srs_error_t GsHttpResponseWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) {
  srs_error_t err = srs_success;
  //TODO need optimizing
  CDataPackage* data = nullptr;  
  for (int i = 0; i < iovcnt; ++i) {
    CDataPackage part(iov[i].iov_len, 
                      (LPCSTR)iov[i].iov_base, 
                      CDataPackage::DONT_DELETE, 
                      iov[i].iov_len);

    if (data) {
      data->Append(part.DuplicatePackage());
    } else {
      data = part.DuplicatePackage();
    }

    if (pnwrite) {
      *pnwrite += iov[i].iov_len;
    }
  }

  if (data) {
    std::string msg{std::move(data->FlattenPackage())};
    err = write(msg.c_str(), msg.length());

    data->DestroyPackage();
  }

  return err;
}

void GsHttpResponseWriter::write_header(int code) {
  if (header_wrote_) {
    MLOG_WARN("http: multiple write_header calls, code=" << code);
    return;
  }
  
  header_wrote_ = true;
  status_ = code;
  
  // parse the content length from header.
  content_length_ = header_.content_length();
}

srs_error_t GsHttpResponseWriter::send_header(const char* data) {
  srs_error_t err = srs_success;
  
  if (header_sent_) {
      return err;
  }
  header_sent_ = true;

  // detect content type
  if(srs_go_http_body_allowd(status_)) {
    if(data && header_.content_type().empty()) {
      header_.set_content_type(srs_go_http_detect());
    }
  }
  
  // set server if not set.
  if (header_.get("Server").empty()) {
      header_.set("Server", "ly demo");
  }
  
  // chunked encoding
  if (content_length_ == -1) {
      header_.set("Transfer-Encoding", "chunked");
  }
  
  // keep alive to make vlc happy.
  if (header_.get("Connection").empty()) {
      header_.set("Connection", "Keep-Alive");
  }

  auto status_txt = generate_http_status_text(status_);
  conn_->SetResponseStatus(status_, std::string{status_txt.data(), status_txt.length()});

  BOOL value = TRUE;
  conn_->SetOption(TP_OPT_HTTP_CUSTOM_CONTENT_LENGTH, &value);  
  conn_->SetResponseHeaders(header_.header());

  //CDataPackage _data(0);
  int ret = RV_OK; //conn_->SendData(_data);

  if(RV_SUCCEEDED(ret))
    return err;
    
  return srs_error_new(ret, "send header faileid");
}

class GsHttpProtocalImplFactory : public IHttpProtocalFactory {
 public:
  GsHttpProtocalImplFactory(IHttpServer* p1, CDataPackage* p2)
    : conn_{p1}, req_{p2} {
  }

  std::shared_ptr<IHttpRequestReader> 
  CreateRequestReader(IHttpRequestReader::CallBack* callback) override {
    return std::make_shared<GsHttpRequestReader>(conn_.Get(), req_.Get(), callback);
  }
  
  std::shared_ptr<IHttpResponseWriter> 
  CreateResponseWriter(bool flag_stream) override {
    return std::make_shared<GsHttpResponseWriter>(conn_.Get(), flag_stream);
  }
  
  std::shared_ptr<IHttpMessageParser> 
  CreateMessageParser() override {
    return std::make_shared<GsHttpMessageParser>(conn_.Get());
  }

  std::shared_ptr<IHttpResponseReader> 
  CreateResponseReader() override {
    return std::make_shared<GsHttpResponseDummyReader>();
  }
 private:
   CSmartPointer<T1> conn_;
   CDataPackageSmartPoint req_;
};

std::unique_ptr<IHttpProtocalFactory>
CreateDefaultHttpProtocalFactory(void* p1, void* p2) {
  return std::make_unique<GsHttpProtocalImplFactory>(
      reinterpret_cast<IHttpServer*>(p1), 
      reinterpret_cast<CDataPackage*>(p2));
}

}

#endif //__GS__

