#include "http/http_protocal_impl.h"

#include "rtc_base/thread.h"

#include "common/media_define.h"
#include "common/media_log.h"
#include "http/http_stack.h"
#include "utils/media_msg_chain.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("http_protocal_impl");

//AsyncSokcetWrapper
AsyncSokcetWrapper::AsyncSokcetWrapper(rtc::AsyncPacketSocket* c)
  : conn_{c} {
}

AsyncSokcetWrapper::~AsyncSokcetWrapper() {
  Close();
}

void AsyncSokcetWrapper::Open(bool is_server) {
  conn_->SignalSentPacket.connect(this, &AsyncSokcetWrapper::OnSentEvent);
  conn_->SignalReadyToSend.connect(this, &AsyncSokcetWrapper::OnWriteEvent);
  conn_->SignalReadPacket.connect(this, &AsyncSokcetWrapper::OnReadEvent);
  conn_->SignalClose.connect(this, &AsyncSokcetWrapper::OnCloseEvent);
  
  server_ = is_server;
}

void AsyncSokcetWrapper::Close() {
  if (conn_) {
    conn_->Close();
    conn_.reset(nullptr);
  }
}

srs_error_t AsyncSokcetWrapper::Write(const char* c_data, int c_size, int* sent) {
  srs_error_t err = srs_success;
  if (UNLIKELY(blocked_)) {
    err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  } else {
    if ((err = Write_i(c_data, c_size, sent)) != srs_success) {
      blocked_ = true;
    }
  }

  return err;
}

srs_error_t AsyncSokcetWrapper::Write(MessageChain& msg, int* sent) {
  srs_error_t err = srs_success;
  int isent = 0;
  
  if (UNLIKELY(blocked_)) {
    err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  } else {
    MessageChain* pnext = &msg;
    while (pnext) {
      int msg_sent = 0;
      uint32_t len = pnext->GetFirstMsgLength();
      if (len != 0) {
        const char* c_data = pnext->GetFirstMsgReadPtr();
        if ((err = Write_i(c_data, len, &msg_sent)) != srs_success) {
          //error occur, mybe would block
          isent += msg_sent;
          blocked_ = true;
          break;
        }
        isent += msg_sent;
      }
      pnext = pnext->GetNext();
    }
  }

  if (sent) {
    *sent = isent;
  }
  
  return err;
}

srs_error_t AsyncSokcetWrapper::Write_i(const char* c_data, int c_size, int* sent) {
  srs_error_t err = srs_success;
  
  int left_size = c_size;
  int size;
  const char* data;
  
  do {
    size = left_size;
    data = c_data + c_size - left_size;
    
    rtc::PacketOptions option;
    if (size > kMaxPacketSize) {
      size = kMaxPacketSize;
    }

    if (size <= 0) {
      break;
    }
    
    int ret = conn_->Send(data, size, option);
    if (LIKELY(ret > 0)) {
      // must be total sent
      MA_ASSERT(ret == size);
      left_size -= size;
    } else /*if ret <= 0*/ {      
      if (conn_->GetError() == EWOULDBLOCK) {
        err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, 
            "need on send, sent:%d", c_size - left_size);
      } else {
        err = srs_error_new(ERROR_SOCKET_ERROR, 
            "unexpect low level error code:%d", conn_->GetError());
      }
      break;
    }
  } while(true);

  if (sent) {
    *sent = c_size - left_size;
  }

  return err;
}

void AsyncSokcetWrapper::OnReadEvent(rtc::AsyncPacketSocket*,
                                     const char* c_msg,
                                     size_t c_size,
                                     const rtc::SocketAddress&,
                                     const int64_t&) {
  MA_ASSERT_RETURN(c_size, );
  std::string_view str_req{c_msg, c_size};

  if (server_) {
    auto p = req_reader_.lock();
    if (p) {
      p->OnRequest(str_req);
    }
  }
}

void AsyncSokcetWrapper::OnCloseEvent(rtc::AsyncPacketSocket* socket, int err) {
  MLOG_INFO("" << err);
  if (conn_) {
    conn_ = nullptr;
  }

  if (server_) {
    auto p = req_reader_.lock();
    if (p) {
      p->OnDisconnect();
    }
  }
}

void AsyncSokcetWrapper::OnSentEvent(rtc::AsyncPacketSocket*, const rtc::SentPacket& sp) {
  MLOG_INFO("");
}

void AsyncSokcetWrapper::OnWriteEvent(rtc::AsyncPacketSocket*) {
  blocked_ = false;
  auto ptr = writer_.lock();
  if (ptr) {
    ptr->OnWriteEvent();
  }
}

//HttpMessageParser
void HttpMessageParser::initialize(enum http_parser_type type) {
  jsonp_ = false;
  type_ = type;
  
  // Initialize the parser, however it's not necessary.
  http_parser_init(&parser_, type_);
  parser_.data = (void*)this;
  
  memset(&settings_, 0, sizeof(http_parser_settings));
  settings_.on_message_begin = on_message_begin;
  settings_.on_url = on_url;
  settings_.on_header_field = on_header_field;
  settings_.on_header_value = on_header_value;
  settings_.on_headers_complete = on_headers_complete;
  settings_.on_body = on_body;
  settings_.on_message_complete = on_message_complete;
}

void HttpMessageParser::set_jsonp(bool allow_jsonp) {
  jsonp_ = allow_jsonp;
}

srs_error_t HttpMessageParser::parse_message(
    std::string_view str_msg, std::shared_ptr<ISrsHttpMessage>& out) {
  srs_error_t err = srs_success;

  if (state_ == SrsHttpParseStateStart ||
      state_ == SrsHttpParseStateHeaderComplete ||
      state_ == SrsHttpParseStateBody) {
    // continue parse message
    if ((err = parse_message_imp(str_msg)) != srs_success) {
      // Reset request data.
      state_ = SrsHttpParseStateInit;
      return srs_error_wrap(err, "parse message body");
    }

    switch(state_) {
    case SrsHttpParseStateInit:
      MA_ASSERT_RETURN(false, err);
    case SrsHttpParseStateStart:
      return err;
    case SrsHttpParseStateHeaderComplete:
    case SrsHttpParseStateBody:
    case SrsHttpParseStateMessageComplete:
      break;
    default:
      MA_ASSERT_RETURN(false, err);
    }
    
    if (!msg_out_) {
      //coming in on SrsHttpParseStateStart
      auto msg = std::make_shared<HttpMessage>(buffer_view_);
      // Initialize the basic information.
      msg->set_basic(hp_header_.type, 
                     hp_header_.method, 
                     hp_header_.status_code, 
                     hp_header_.content_length);
      msg->set_header(*(header_.get()), http_should_keep_alive(&hp_header_));
      if ((err = msg->set_url(url_, jsonp_)) != srs_success) {
        // Reset request data.
        state_ = SrsHttpParseStateInit;         
        return srs_error_wrap(err, "set url=%s, jsonp=%d", url_.c_str(), jsonp_);
      }

      msg_out_ = std::move(msg);
    }

    if (state_ == SrsHttpParseStateMessageComplete) {
      // Reset request data. for the next message parsing
      state_ = SrsHttpParseStateInit;

      // parse message ok
      msg_out_->set_body_eof();
      out = std::move(msg_out_);
      return err;
    }

    // chunked
    if (hp_header_.content_length == (uint64_t)-1) {
      //chunked message header has returned already.
      return err;
    } 

    //not chunked, don't return util body parsed ok
    return err;
  }
  
  // Reset request data.
  state_ = SrsHttpParseStateInit;
  memset(&hp_header_, 0, sizeof(http_parser));
  // The body that we have read from cache.
  p_body_start_ = p_header_tail_ = NULL;
  // We must reset the field name and value, because we may get a partial value in on_header_value.
  field_name_ = field_value_ = "";
  // The header of the request.

  header_ = std::move(std::make_unique<SrsHttpHeader>());

  // Reset parser for each message.
  // If the request is large, such as the fifth message at 
  // @utest ProtocolHTTPTest.ParsingLargeMessages,
  // we got header and part of body, so the parser will stay at SrsHttpParseStateBody:
  //      ***MESSAGE BEGIN***
  //      ***HEADERS COMPLETE***
  //      Body: xxx
  // when got next message, the whole next message is parsed as the body of previous one,
  // and the message fail.
  // @note You can comment the bellow line, the utest will fail.
  http_parser_init(&parser_, type_);
  // callback object ptr.
  parser_.data = (void*)this;

  buffer_.clear();
  
  // do parse
  if ((err = parse_message_imp(str_msg)) != srs_success) {
    // Reset request data.
    state_ = SrsHttpParseStateInit;
    return srs_error_wrap(err, "parse message");
  }

  switch(state_) {
  case SrsHttpParseStateInit:
    MA_ASSERT_RETURN(false, err);
  case SrsHttpParseStateStart:
    return err;
  case SrsHttpParseStateHeaderComplete:
  case SrsHttpParseStateBody:
  case SrsHttpParseStateMessageComplete:
    break;
  default:
    MA_ASSERT_RETURN(false, err);
  }

  //header completed
  auto msg = std::make_shared<HttpMessage>(buffer_view_);

  // Initialize the basic information.
  msg->set_basic(hp_header_.type, 
                 hp_header_.method, 
                 hp_header_.status_code, 
                 hp_header_.content_length);
  msg->set_header(*(header_.get()), http_should_keep_alive(&hp_header_));
  if ((err = msg->set_url(url_, jsonp_)) != srs_success) {
    // Reset request data.
    state_ = SrsHttpParseStateInit;
    return srs_error_wrap(err, "set url=%s, jsonp=%d", url_.c_str(), jsonp_);
  }

  if (state_ == SrsHttpParseStateMessageComplete) {
    // Reset request data. for the next message parsing
    state_ = SrsHttpParseStateInit;
    msg->set_body_eof();
    out = std::move(msg);
    return err;
  }

  msg_out_ = msg;
  
  // chunked
  if (hp_header_.content_length == (uint64_t)-1) {
    //save message pointer for body parsing, and return message
    out = std::move(msg);
    return err;
  } 

  //not chunked, don't return util body parsed ok
  return err;
}

srs_error_t HttpMessageParser::parse_message_imp(std::string_view str_msg) {
  srs_error_t err = srs_success;

  buffer_.erase(0, consumed_);
  buffer_.append(str_msg.data(), str_msg.length());
  buffer_view_ = buffer_;
  
  if (!buffer_view_.empty()) {
    consumed_ = http_parser_execute(
        &parser_, &settings_, buffer_view_.data(), buffer_view_.length());

    // The error is set in http_errno.
    enum http_errno code;
    if ((code = HTTP_PARSER_ERRNO(&parser_)) != HPE_OK) {
      return srs_error_new(ERROR_HTTP_PARSE_HEADER, 
          "parse %dB, nparsed=%d, err=%d/%s %s", buffer_view_.length(), 
                                                 (int)consumed_, 
                                                 code, 
                                                 http_errno_name(code), 
                                                 http_errno_description(code));
    }

    ssize_t consumed = consumed_;
    // When buffer consumed these bytes, it's dropped so the new ptr is 
    // actually the HTTP body. But http-parser
    // doesn't indicate the specific sizeof header, so we must finger it out.
    // @remark We shouldn't use on_body, because it only works for normal case, 
    // and losts the chunk header and length.
    // @see https://github.com/ossrs/srs/issues/1508
    if (!msg_out_ && p_header_tail_ && buffer_view_.data() < p_body_start_) {
      for (const char* p = p_header_tail_; p <= p_body_start_ - 4; p++) {
        if (p[0] == SRS_CONSTS_CR && 
            p[1] == SRS_CONSTS_LF && 
            p[2] == SRS_CONSTS_CR && 
            p[3] == SRS_CONSTS_LF) {
          consumed = p + 4 - buffer_view_.data();
          break;
        }
      }
    }
      
    MLOG_CINFO("size=%d, nparsed=%d", buffer_.length(), (int)consumed);

    // Only consume the header bytes.
    buffer_view_.remove_prefix(consumed);

    // Done when header completed, never wait for body completed, because it maybe chunked.
    if (state_ >= SrsHttpParseStateHeaderComplete) {
      HttpMessageParser* obj = this;
      if (!obj->field_value_.empty()) {
        obj->header_->set(obj->field_name_, obj->field_value_);
      }
    }
  }
  
  return err;
}

int HttpMessageParser::on_message_begin(http_parser* parser) {
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);
  
  obj->state_ = SrsHttpParseStateStart;
  
  MLOG_INFO("***MESSAGE BEGIN***");
  
  return 0;
}

int HttpMessageParser::on_headers_complete(http_parser* parser) {
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);
  
  obj->hp_header_ = *parser;
  // save the parser when header parse completed.
  obj->state_ = SrsHttpParseStateHeaderComplete;

  // We must update the body start when header complete, because sometimes we only got header.
  // When we got the body start event, we will update it to much precious position.
  obj->p_body_start_ = obj->buffer_view_.data() + obj->buffer_view_.length();

  MLOG_INFO("***HEADERS COMPLETE***");
  
  // see http_parser.c:1570, return 1 to skip body.
  return 0;
}

int HttpMessageParser::on_message_complete(http_parser* parser) {
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);
  
  // save the parser when body parse completed.
  obj->state_ = SrsHttpParseStateMessageComplete;
  
  MLOG_INFO("***MESSAGE COMPLETE***\n");
  
  return 0;
}

int HttpMessageParser::on_url(http_parser* parser, const char* at, size_t length) {
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);
  
  if (length > 0) {
      obj->url_ = std::string(at, (int)length);
  }

  // When header parsed, we must save the position of start for body,
  // because we have to consume the header in buffer.
  // @see https://github.com/ossrs/srs/issues/1508
  obj->p_header_tail_ = at;
  
  MLOG_CINFO("Method: %d, Url: %.*s", parser->method, (int)length, at);
  
  return 0;
}

int HttpMessageParser::on_header_field(http_parser* parser, const char* at, size_t length) {
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);

  if (!obj->field_value_.empty()) {
      obj->header_->set(obj->field_name_, obj->field_value_);
      obj->field_name_ = obj->field_value_ = "";
  }
  
  if (length > 0) {
      obj->field_name_.append(at, (int)length);
  }

  // When header parsed, we must save the position of start for body,
  // because we have to consume the header in buffer.
  // @see https://github.com/ossrs/srs/issues/1508
  obj->p_header_tail_ = at;
  
  MLOG_CINFO("Header field(%d bytes): %.*s", (int)length, (int)length, at);
  return 0;
}

int HttpMessageParser::on_header_value(http_parser* parser, const char* at, size_t length){
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);
  
  if (length > 0) {
      obj->field_value_.append(at, (int)length);
  }

  // When header parsed, we must save the position of start for body,
  // because we have to consume the header in buffer.
  // @see https://github.com/ossrs/srs/issues/1508
  obj->p_header_tail_ = at;
  
  MLOG_CDEBUG("Header value(%d bytes): %.*s", (int)length, (int)length, at);
  return 0;
}

int HttpMessageParser::on_body(http_parser* parser, const char* at, size_t length){
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);

  // save the parser when body parsed.
  obj->state_ = SrsHttpParseStateBody;

  // Used to discover the header length.
  // @see https://github.com/ossrs/srs/issues/1508
  obj->p_body_start_ = at;

  MLOG_CDEBUG("Body: %.*s", (int)length, at);

  std::string_view body(at, length);

  if (obj->msg_out_) {
    obj->msg_out_->on_body(body);
  }
  
  return 0;
}

int HttpMessageParser::on_chunk_header(http_parser* parser) {
  //not implement
  return 0;
}

int HttpMessageParser::on_chunk_complete(http_parser* parser) {
  //not implement
  return 0;
}

//HttpRequestReader
HttpRequestReader::HttpRequestReader(
    std::shared_ptr<AsyncSokcetWrapper> s, CallBack* callback) 
  : socket_{s}, callback_{callback} {
}

void HttpRequestReader::open() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  socket_->SetReqReader(weak_from_this());
}

void HttpRequestReader::OnRequest(std::string_view str_mgs) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  if (callback_) {
    srs_error_t err = callback_->process_request(str_mgs);
    if (err != srs_success) {
      MLOG_CERROR("code:%d, desc:%s", 
          srs_error_code(err), srs_error_desc(err).c_str());
      delete err;
    }
  }
}

void HttpRequestReader::OnDisconnect() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  if (socket_) {
    socket_ = nullptr;
  }

  if (callback_) {
    callback_->on_disconnect();
    callback_ = nullptr;
  }
}

void HttpRequestReader::disconnect() {
  MLOG_INFO("");
  if (socket_) {
    socket_ = nullptr;
  }
  callback_ = nullptr;
}

//HttpResponseWriter
HttpResponseWriter::HttpResponseWriter(AsyncSokcetWrapper* s)
  : socket_{s},
    header_{std::make_unique<SrsHttpHeader>()} {
}

void HttpResponseWriter::open() {
  RTC_DCHECK_RUN_ON(&thread_check_);
}

srs_error_t HttpResponseWriter::final_request(MessageChain*& left_msg) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  srs_error_t err = srs_success;

  // write the header data in memory.
  if (!header_wrote_) {
    write_header(SRS_CONSTS_HTTP_OK);
  }

  // whatever header is wrote, we should try to send header.
  if ((err = send_header(NULL, 0, left_msg)) != srs_success) {
    return srs_error_wrap(err, "send header");
  }

  // complete the chunked encoding.
  if (content_length_ == -1) {
    std::stringstream ss;
    ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
    std::string ch = ss.str();
    int sent = 0;
    if ((err = socket_->Write(ch.data(), (int)ch.length(), &sent)) != srs_success) {
      int left_size = (int)ch.length() - sent;
      MessageChain mb(left_size, ch.data()+left_size, MessageChain::DONT_DELETE, left_size);
      left_msg = mb.DuplicateChained();
    }
    return err;
  }

  // flush when send with content length
  return write(nullptr, left_msg);
}

SrsHttpHeader* HttpResponseWriter::header() {
  return header_.get();
}

srs_error_t HttpResponseWriter::write(
    MessageChain* data, MessageChain*& left_msg) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  srs_error_t err = srs_success;
  int size = data ? data->GetChainedLength() : 0;
    
  // write the header data in memory.
  if (!header_wrote_) {
    if (header_->content_type().empty()) {
      header_->set_content_type("text/plain; charset=utf-8");
    }
    if (header_->content_length() == -1) {
      header_->set_content_length(size);
    }
    write_header(SRS_CONSTS_HTTP_OK);
  }
  
  // whatever header is wrote, we should try to send header.
  if ((err = send_header((const char*)data, size, left_msg)) != srs_success) {
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
  
  // directly send with content length
  if (content_length_ != -1) {
    int isent = 0;
    
    if ((err = socket_->Write(*data, &isent)) != srs_success) {
      left_msg = data->DuplicateChained();
      left_msg->AdvanceChainedReadPtr(isent);
    }
    return err;
  }
  
  // send in chunked encoding.
  int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);

  MessageChain p0{(uint32_t)nb_size, header_cache_, MessageChain::DONT_DELETE, (uint32_t)nb_size};
  MessageChain p1{2, (char*)SRS_HTTP_CRLF, MessageChain::DONT_DELETE, 2};
  MessageChain& p2 = *data;
  MessageChain p3{2, (char*)SRS_HTTP_CRLF, MessageChain::DONT_DELETE, 2};

  p0.Append(&p1);
  p1.Append(&p2);
  p2.Append(&p3);

  int isent = 0;
  if ((err = socket_->Write(p0, &isent)) != srs_success) {
    left_msg = p0.DuplicateChained();
    left_msg->AdvanceChainedReadPtr(isent);
    err = srs_error_wrap(err, "write chunk");
  }
  
  return err;
}

void HttpResponseWriter::write_header(int code) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (header_wrote_) {
    MLOG_CWARN("http: multiple write_header calls, code=%d", code);
    return;
  }
  
  header_wrote_ = true;
  status_ = code;
  
  // parse the content length from header.
  content_length_ = header_->content_length();
}

srs_error_t HttpResponseWriter::send_header(
    const char* data, int, MessageChain*& left_msg) {
  
  srs_error_t err = srs_success;
  
  if (header_sent_) {
    return err;
  }

  header_sent_ = true;
  
  std::stringstream ss;
  
  // status_line
  ss << "HTTP/1.1 " << status_ << " "
      << generate_http_status_text(status_) << SRS_HTTP_CRLF;
  
  // detect content type
  if (srs_go_http_body_allowd(status_)) {
    if (data && header_->content_type().empty()) {
      header_->set_content_type(srs_go_http_detect());
    }
  }
  
  // set server if not set.
  if (header_->get("Server").empty()) {
    header_->set("Server", RTMP_SIG_SRS_SERVER);
  }
  
  // chunked encoding
  if (content_length_ == -1) {
    header_->set("Transfer-Encoding", "chunked");
  }
  
  // keep alive to make vlc happy.
  if (header_->get("Connection").empty()) {
    header_->set("Connection", "Keep-Alive");
  }
  
  // write header
  header_->write(ss);
  
  // header_eof
  ss << SRS_HTTP_CRLF;
  
  std::string buf = std::move(ss.str());

  int sent = 0;
  MessageChain mb(buf.length(), buf.c_str(), MessageChain::DONT_DELETE, buf.length());
  if ((err = socket_->Write(mb, &sent)) != srs_success) {
    mb.AdvanceChainedReadPtr(sent);
    left_msg = mb.DuplicateChained();
  }

  return err;
}

//helper define
#define IS_CURRENT_THREAD(x) thread_==rtc::ThreadManager::Instance()->CurrentThread()

//HttpResponseWriterProxy
HttpResponseWriterProxy::HttpResponseWriterProxy(
    std::shared_ptr<AsyncSokcetWrapper> s, bool)
  : writer_{std::move(std::make_unique<HttpResponseWriter>(s.get()))},
    socket_{std::move(s)} {
  thread_ = rtc::ThreadManager::Instance()->CurrentThread();
}

HttpResponseWriterProxy::~HttpResponseWriterProxy() {
  if (buffer_) {
    buffer_->DestroyChained();
  }
}

void HttpResponseWriterProxy::open() {
  writer_->open();
  socket_->SetWriter(weak_from_this());
}

srs_error_t HttpResponseWriterProxy::final_request() {
  if (need_final_request_) {
    return srs_error_new(ERROR_HTTP_PATTERN_DUPLICATED, "final request duplicated");
  }

  srs_error_t err = srs_success;
  if (IS_CURRENT_THREAD()) {
    if (buffer_) {
      need_final_request_ = true; 
    } else {
      MessageChain* left = nullptr;
      if ((err = writer_->final_request(left)) != srs_success) {
        buffer_ = left;
      }
    }
    return err;
  }

  std::weak_ptr<HttpResponseWriterProxy> weak_ptr = weak_from_this();
  asyncTask([weak_ptr, this](auto) {
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr){
      return;
    }

    if (buffer_) {
      need_final_request_ = true; 
    } else {
      MessageChain* left = nullptr;
      srs_error_t err = writer_->final_request(left);
      if (err != srs_success) {
        buffer_ = left;
        MLOG_CWARN("proxy final request failed on send later, code:%d, desc:%s", 
            srs_error_code(err), srs_error_desc(err).c_str());
        delete err;
      }
    }
  });

  return err;
}

SrsHttpHeader* HttpResponseWriterProxy::header() {
  return writer_->header();
}

srs_error_t HttpResponseWriterProxy::write(const char* data, int size) {
  if (buffer_full_) {
    return srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  }

  MessageChain mb(size, data, MessageChain::DONT_DELETE, size);
  
  srs_error_t err = srs_success;
  if (IS_CURRENT_THREAD()) {
    if (buffer_) {
      err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
      MA_ASSERT_RETURN(false, err);
    } else {
      MessageChain* left = nullptr;
      if ((err = writer_->write(&mb, left)) != srs_success) {
        buffer_ = left;
        buffer_full_ = true;
      }
    }
    return err;
  }

  std::weak_ptr<HttpResponseWriterProxy> weak_ptr = weak_from_this();
  asyncTask([weak_ptr, pmb=mb.DuplicateChained(), this] (auto) {
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr){
      pmb->DestroyChained();
      return;
    }

    write_i(pmb);
  });

  return err;
}

srs_error_t HttpResponseWriterProxy::writev(
    const iovec* iov, int iovcnt, ssize_t* pnwrite) {
  if (buffer_full_) {
    return srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  }

  //TODO need optimizing
  MessageChain* data = nullptr;  
  for (int i = 0; i < iovcnt; ++i) {
    MessageChain part(iov[i].iov_len, 
                      (const char*)iov[i].iov_base, 
                      MessageChain::DONT_DELETE, 
                      iov[i].iov_len);

    if (data) {
      data->Append(part.DuplicateChained());
    } else {
      data = part.DuplicateChained();
    }

    if (pnwrite) {
      *pnwrite += iov[i].iov_len;
    }
  }
   
  srs_error_t err = srs_success;

  if (IS_CURRENT_THREAD()) {
    if (buffer_) {
      err =  srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
      data->DestroyChained();
      MA_ASSERT_RETURN(false, err);
    } else {
      MessageChain* left = nullptr;
      if ((err = writer_->write(data, left)) != srs_success) {
        buffer_ = left;
        buffer_full_ = true;
      }
    }
    data->DestroyChained();
    return err;
  }

  std::weak_ptr<HttpResponseWriterProxy> weak_ptr = weak_from_this();
  asyncTask([weak_ptr, data, this] (auto) {
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr){
      data->DestroyChained();
      return;
    }

    write_i(data);
  });

  return err;
}

void HttpResponseWriterProxy::write_i(MessageChain* data) {
  if (buffer_) {
    MA_ASSERT(buffer_full_);
    buffer_->Append(data);
  } else {
    MessageChain* left = nullptr;
    srs_error_t err = writer_->write(data, left);
    if (err != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_CERROR("http async write error, code:%d, desc:%s", 
          srs_error_code(err), srs_error_desc(err).c_str());
      }
      buffer_full_ = true;
      buffer_ = left;
      delete err;
    }
    data->DestroyChained();
  }
}

void HttpResponseWriterProxy::write_header(int code) {
  if (IS_CURRENT_THREAD()) {
    writer_->write_header(code);
    return;
  }

  std::weak_ptr<HttpResponseWriterProxy> weak_ptr = weak_from_this();
  asyncTask([weak_ptr, code, this] (auto){
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr){
      return;
    }
    
    writer_->write_header(code);
  });
}

void HttpResponseWriterProxy::OnWriteEvent() {
  srs_error_t err = srs_success;
  if (buffer_) {
    MessageChain* left = nullptr;
    err = writer_->write(buffer_, left);
    if (err != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_CERROR("OnWriteEvent write error, code:%d, desc:%s", 
            srs_error_code(err), srs_error_desc(err).c_str());
      }
      buffer_full_ = true;
      buffer_->DestroyChained();
      buffer_ = left;
      delete err;
      return;
    }

    buffer_->DestroyChained();
    buffer_ = nullptr;
  }

  if (need_final_request_) {
    MessageChain* left = nullptr;
    if ((err = writer_->final_request(left)) != srs_success) {
      buffer_ = left;
      buffer_full_ = true;
      return;
    }
    need_final_request_ = false;
  }

  buffer_full_ = false;

  SignalOnWrite_(this);
}

void HttpResponseWriterProxy::asyncTask(
    std::function<void(std::shared_ptr<HttpResponseWriterProxy>)> f) {
  std::weak_ptr<HttpResponseWriterProxy> weak_this = weak_from_this();
  thread_->PostTask(RTC_FROM_HERE, [weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f(this_ptr);
    }
  });
}

//HttpResponseReader
void HttpResponseReader::open(IHttpResponseReaderSink* s) {
  socket_->SetResReader(weak_from_this());
  sink_ = s;
}

//HttpProtocalImplFactory
class HttpProtocalImplFactory : public IHttpProtocalFactory {
 public:
  HttpProtocalImplFactory(bool is_server, 
                          rtc::AsyncPacketSocket*, 
                          rtc::AsyncPacketSocket* p2)
    : socket_{std::make_shared<AsyncSokcetWrapper>(p2)} {
    socket_->Open(is_server);
  }

  std::shared_ptr<IHttpRequestReader> 
  CreateRequestReader(IHttpRequestReader::CallBack* callback) override {
    return std::make_shared<HttpRequestReader>(socket_, callback);
  }
  
  std::shared_ptr<IHttpResponseWriter> 
  CreateResponseWriter(bool flag_stream) override {
    return std::make_shared<HttpResponseWriterProxy>(socket_, flag_stream);
  }
  
  std::unique_ptr<IHttpMessageParser> 
  CreateMessageParser() override {
    return std::make_unique<HttpMessageParser>();
  }

  std::shared_ptr<IHttpResponseReader> 
  CreateResponseReader() override {
    return std::make_shared<HttpResponseReader>(socket_);
  }
 private:
   std::shared_ptr<AsyncSokcetWrapper> socket_;
};

std::unique_ptr<IHttpProtocalFactory>
CreateDefaultHttpProtocalFactory(void* p1, void* p2) {
  return std::make_unique<HttpProtocalImplFactory>(true,
      reinterpret_cast<rtc::AsyncPacketSocket*>(p1), 
      reinterpret_cast<rtc::AsyncPacketSocket*>(p2));
}

} //namespace ma

