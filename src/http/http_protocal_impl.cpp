#include "http/http_protocal_impl.h"

#include "rtc_base/thread.h"
#include "common/media_define.h"
#include "common/media_log.h"
#include "http/http_stack.h"
#include "utils/media_msg_chain.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.http");

#define CHECK_MSG_DUPLICATED(m) \
  do{ \
    MessageChain* check = m; \
    while(check) { \
      MA_ASSERT(MA_BIT_ENABLED(check->GetFlag(), MessageChain::DUPLICATED)); \
      check = check->GetNext(); \
    } \
  } while(0)
  
//AsyncSokcetWrapper
AsyncSokcetWrapper::AsyncSokcetWrapper(rtc::AsyncPacketSocket* c)
  : conn_{c} {
}

AsyncSokcetWrapper::~AsyncSokcetWrapper() {
  RTC_DCHECK_RUN_ON(&thread_check_);
}

void AsyncSokcetWrapper::Open(bool is_server, rtc::Thread* thread) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  conn_->SignalSentPacket.connect(this, &AsyncSokcetWrapper::OnSentEvent);
  conn_->SignalReadyToSend.connect(this, &AsyncSokcetWrapper::OnWriteEvent);
  conn_->SignalReadPacket.connect(this, &AsyncSokcetWrapper::OnReadEvent);
  conn_->SignalClose.connect(this, &AsyncSokcetWrapper::OnCloseEvent);
  
  server_ = is_server;
  thread_ = thread;
}

void AsyncSokcetWrapper::Close() {
  if (close_) {
    return ;
  }

  close_ = true;
  conn_->Close();
}

srs_error_t AsyncSokcetWrapper::Write(MessageChain* msg, int* sent) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (close_) {
    return srs_error_new(ERROR_SOCKET_CLOSED, "socket closed");
  }

  srs_error_t err = srs_success;
  int isent = 0;
  
  if (UNLIKELY(blocked_)) {
    err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  } else {
    MessageChain* pnext = msg;
    while (pnext) {
      int msg_sent = 0;
      uint32_t len = pnext->GetFirstMsgLength();
      if (len != 0) {
        const char* c_data = pnext->GetFirstMsgReadPtr();
        MA_ASSERT(c_data + len == pnext->GetFirstMsgWritePtr());
        err = Write_i(c_data, len, &msg_sent);
        isent += msg_sent;
        if (err != srs_success) {
          //error occur, mybe would block
          MA_ASSERT(srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK);
          if (srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK) {
            blocked_ = true;
          }
          break;
        }
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
  RTC_DCHECK_RUN_ON(&thread_check_);

  srs_error_t err = srs_success;
  
  int left_size = c_size;
  int size;
  const char* data;
  
  do {
    data = c_data + c_size - left_size;
    size = std::min(left_size, kMaxPacketSize);
    
    if (size <= 0) {
      break;
    }
    
    rtc::PacketOptions option;
    int ret = conn_->Send(data, size, option);
    if (UNLIKELY(ret <= 0)) {
      MA_ASSERT(EMSGSIZE != conn_->GetError());
      if (conn_->GetError() == EWOULDBLOCK) {
        err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, 
            "need on send, sent:%d", c_size - left_size);
      } else {
        err = srs_error_new(ERROR_SOCKET_ERROR, 
            "unexpect low level error code:%d", conn_->GetError());
      }
      break;
    }

    // must be total sent
    MA_ASSERT(ret == size);
    left_size -= ret;
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
  RTC_DCHECK_RUN_ON(&thread_check_);  
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
  RTC_DCHECK_RUN_ON(&thread_check_);
  MLOG_TRACE("code:" << err << (server_?", server":""));

  if (!close_) {
    conn_->Close();
  }
  close_ = true;

  // avoid dtor in this function, will cause dead lock
  rtc::Thread* thread = rtc::ThreadManager::Instance()->CurrentThread();
  thread->PostTask(RTC_FROM_HERE, [share_this = shared_from_this()] {
    // call dtor
  });
  
  if (server_) {
    auto p = req_reader_.lock();
    if (p) {
      p->OnDisconnect();
    }
  }
}

void AsyncSokcetWrapper::OnSentEvent(rtc::AsyncPacketSocket*, 
                                     const rtc::SentPacket& sp) {
  RTC_DCHECK_RUN_ON(&thread_check_);
}

void AsyncSokcetWrapper::OnWriteEvent(rtc::AsyncPacketSocket*) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (close_) {
    return ;
  }
  
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
      
    MLOG_CDEBUG("size=%d, nparsed=%d", buffer_.length(), (int)consumed);

    // Only consume the header bytes.
    buffer_view_.remove_prefix(consumed);

    // Done when header completed, never wait for body completed, 
    // because it maybe chunked.
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
  
  MLOG_CDEBUG("***MESSAGE BEGIN***");
  
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

  MLOG_CDEBUG("***HEADERS COMPLETE***");
  
  return 0;
}

int HttpMessageParser::on_message_complete(http_parser* parser) {
  HttpMessageParser* obj = (HttpMessageParser*)parser->data;
  srs_assert(obj);
  
  // save the parser when body parse completed.
  obj->state_ = SrsHttpParseStateMessageComplete;
  
  MLOG_CDEBUG("***MESSAGE COMPLETE***\n");
  
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
  
  MLOG_CDEBUG("Method: %d, Url: %.*s", parser->method, (int)length, at);
  
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
  
  MLOG_CDEBUG("Header field(%d bytes): %.*s", (int)length, (int)length, at);
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
      MLOG_ERROR("desc:" << srs_error_desc(err));
      delete err;
    }
  }
}

void HttpRequestReader::OnDisconnect() {
  MLOG_TRACE("");

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
  MLOG_TRACE("");
  RTC_DCHECK_RUN_ON(&thread_check_);

  if (socket_) {
    socket_ = nullptr;
  }
  callback_ = nullptr;
}

//HttpResponseWriter
HttpResponseWriter::HttpResponseWriter()
  : header_{std::make_unique<SrsHttpHeader>()} {
}

void HttpResponseWriter::open() {
  RTC_DCHECK_RUN_ON(&thread_check_);
}

srs_error_t HttpResponseWriter::final_request(MessageChain*& result) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  // write the header data in memory.
  if (!header_wrote_) {
    write_header(SRS_CONSTS_HTTP_OK);
  }

  // whatever header is wrote, we should try to send header.
  result = send_header(NULL, 0);

  srs_error_t err = srs_success;
  
  // complete the chunked encoding.
  if (content_length_ == -1) {
    std::stringstream ss;
    ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
    std::string ch = std::move(ss.str());

    MessageChain mc(ch.length(), ch.data(), MessageChain::DONT_DELETE, ch.length());

    if (result) {
      result->Append(mc.DuplicateChained());
    } else {
      result = mc.DuplicateChained();
    }

    return err;
  }

  // flush when send with content length
  MessageChain* ret = nullptr;
  return write(nullptr, ret);
}

SrsHttpHeader* HttpResponseWriter::header() {
  return header_.get();
}

srs_error_t HttpResponseWriter::write(
    MessageChain* send_msg, MessageChain*& result_msg) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  MA_ASSERT(result_msg == nullptr);
  
  int size = send_msg ? send_msg->GetChainedLength() : 0;
    
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
  
  // get header
  result_msg = send_header((const char*)send_msg, size);

  // check the bytes send and content length.
  written_ += size;
  if (content_length_ != -1 && written_ > content_length_) {
    return srs_error_new(ERROR_HTTP_CONTENT_LENGTH, 
        "overflow writen=%d, max=%d", (int)written_, (int)content_length_);
  }

  srs_error_t err = srs_success;
  // ignore NULL content.
  if (!send_msg) {
    return err;
  }
  
  // directly send with content length
  if (content_length_ != -1) {

    if (result_msg) {
      result_msg->Append(send_msg->DuplicateChained());
    } else {
      result_msg = send_msg->DuplicateChained();
    }
    return err;
  }
  
  // send in chunked encoding.
  int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, 
      "%x" SRS_HTTP_CRLF, size);

  MA_ASSERT(nb_size <= SRS_HTTP_HEADER_CACHE_SIZE);

  MessageChain pStart{(uint32_t)nb_size, header_cache_, MessageChain::DONT_DELETE, (uint32_t)nb_size};
  MessageChain pEnd{2, (char*)SRS_HTTP_CRLF, MessageChain::DONT_DELETE, 2};

  MessageChain* http_msg = pStart.DuplicateChained();
  http_msg->Append(send_msg->DuplicateChained());
  http_msg->Append(pEnd.DuplicateChained());
 
  if (result_msg) {
    result_msg->Append(http_msg);
  } else {
    result_msg = http_msg;
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

MessageChain* HttpResponseWriter::send_header(const char* data, int) {
  
  if (header_sent_) {
    return nullptr;
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

  MessageChain mb(buf.length(), buf.c_str(), MessageChain::DONT_DELETE, buf.length());

  return mb.DuplicateChained();
}

//helper define
#define IS_CURRENT_THREAD(x) \
    x==rtc::ThreadManager::Instance()->CurrentThread()

//HttpResponseWriterProxy
HttpResponseWriterProxy::HttpResponseWriterProxy(
    std::shared_ptr<AsyncSokcetWrapper> s, bool /*TODO stream=true set nodelay*/)
  : writer_{std::move(std::make_unique<HttpResponseWriter>())},
    socket_{std::move(s)} {
  thread_ = socket_->GetThread();
}

HttpResponseWriterProxy::~HttpResponseWriterProxy() {
  if (buffer_) {
    buffer_->DestroyChained();
  }
}

void HttpResponseWriterProxy::open() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  writer_->open();
  socket_->SetWriter(weak_from_this());
}

srs_error_t HttpResponseWriterProxy::final_request_i() {
  RTC_DCHECK_RUN_ON(&thread_check_);

  srs_error_t err = srs_success;

  MessageChain* result = nullptr;
  if ((err = writer_->final_request(result)) == srs_success) {
    // final ok
    if (buffer_ && result) {
      buffer_->Append(result);
      return err;
    }
    
    if (result && ((err = write2sock(result)) != srs_success)) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_ERROR("proxy write2sock failed, desc:" << srs_error_desc(err));
      }

      //Cached by buffer, so we thought it has been sent successfully
      delete err;
      err = srs_success;
      result = nullptr;
    }
  }
  
  if (result) {
    result->DestroyChained();
  }
  return err;
}

srs_error_t HttpResponseWriterProxy::final_request() {
  if (IS_CURRENT_THREAD(thread_)) {
    RTC_DCHECK_RUN_ON(&thread_check_);
    return final_request_i();
  }

  std::weak_ptr<HttpResponseWriterProxy> weak_ptr = weak_from_this();
  asyncTask([weak_ptr, this](auto) {
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr) {
      return;
    }

    srs_error_t err = final_request_i();

    if (err != srs_success) {
      MLOG_ERROR("proxy final request failed, desc:" << srs_error_desc(err));
      delete err;
    }
  });

  return srs_success;
}

SrsHttpHeader* HttpResponseWriterProxy::header() {
  return writer_->header();
}

MessageChain* HttpResponseWriterProxy::internal_write(MessageChain* input) {
  RTC_DCHECK_RUN_ON(&thread_check_);

  MessageChain* result = nullptr;
  srs_error_t err = srs_success;

  if ((err = writer_->write(input, result)) != srs_success) {
    //got header by result
    MLOG_ERROR("proxy internal_write failed, desc:" << srs_error_desc(err));
    delete err;
  }
  return result;
}

srs_error_t HttpResponseWriterProxy::write(const char* data, int size) {
  if (buffer_full_) {
    return srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  }

  if (!data || size <= 0) {
    // send header only
    return write(nullptr, nullptr);
  }
  
  MessageChain mb(size, data, MessageChain::DONT_DELETE, size);
  return write(&mb, nullptr);
}

srs_error_t HttpResponseWriterProxy::write(MessageChain* data, ssize_t* pnwrite) {
  if (pnwrite) {
    *pnwrite = 0;
  }

  if (data && data->GetChainedLength() == 0) {
    return srs_error_new(ERROR_HTTP_PATTERN_EMPTY, "data length is 0");
  }
  
  if (buffer_full_) {
    return srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  }

  return write_i(data, pnwrite);
}

srs_error_t HttpResponseWriterProxy::writev(
    const iovec* iov, int iovcnt, ssize_t* pnwrite) {
  if (pnwrite) {
    *pnwrite = 0;
  }
  
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

  srs_error_t err = write_i(data, pnwrite);
  data->DestroyChained();
  return err;
}

srs_error_t HttpResponseWriterProxy::write_i(
    MessageChain* data, ssize_t* pnwrite) {
  srs_error_t err = srs_success;
  uint32_t data_len = data ? data->GetChainedLength() : 0;

  if (IS_CURRENT_THREAD(thread_)) {
    RTC_DCHECK_RUN_ON(&thread_check_);
    MA_ASSERT(!buffer_);

    MessageChain* result = internal_write(data);

    if (result && (err = write2sock(result)) != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_ERROR("proxy write2sock failed, desc:" << srs_error_desc(err));
      }
      delete err;
      err = srs_success;

      //cached by buffer
      result = nullptr;
    }

    if (result) {
      result->DestroyChained();
    }

    if (pnwrite) {
      *pnwrite = data_len;
    }
    return err;
  }

  MessageChain* pDuplcated = nullptr;
  if (!data || data_len <= 0) {
    // send header only
  } else {
    pDuplcated = data->DuplicateChained();
  }

  asyncTask([weak_ptr = weak_from_this(), pDuplcated, this] (auto) {
    CHECK_MSG_DUPLICATED(pDuplcated);
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr) {
      if (pDuplcated)
        pDuplcated->DestroyChained();
      return;
    }

    MessageChain* result = internal_write(pDuplcated);
    srs_error_t err = srs_success;

    if (result) {
      // push it into buffer, wait for on send
      if (buffer_) {
        buffer_->Append(result);
        return;
      }

      //buffer empty, send immediatly
      if ((err = write2sock(result)) != srs_success) {
        if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
          MLOG_ERROR("proxy write failed, desc:" << srs_error_desc(err)); 
        }
        delete err;
        
        //cached by buffer
        result = nullptr;
      }

      if (result) {
        result->DestroyChained();
      }
    }

    if (pDuplcated) {
      pDuplcated->DestroyChained();
    }
  });

  if (pnwrite) {
    *pnwrite = data_len;
  }

  return err;
}

srs_error_t HttpResponseWriterProxy::write2sock(MessageChain* data) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  CHECK_MSG_DUPLICATED(data);
  MA_ASSERT(!buffer_);
  
  int sent = 0;
  srs_error_t err = socket_->Write(data, &sent);
  if (err != srs_success) {
    if (srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK) {
      //need on write
    } else {
      // unexpect low level error, save data?
    }
    buffer_ = data;
    buffer_full_ = true;
  }

  if (sent > 0)
    data->AdvanceChainedReadPtr(sent);
  return err;
}

void HttpResponseWriterProxy::write_header(int code) {
  if (IS_CURRENT_THREAD(thread_)) {
    writer_->write_header(code);
    return;
  }

  std::weak_ptr<HttpResponseWriterProxy> weak_ptr = weak_from_this();
  asyncTask([weak_ptr, code, this] (auto){
    auto this_ptr = weak_ptr.lock();
    if (!this_ptr) {
      return;
    }
    
    writer_->write_header(code);
  });
}

void HttpResponseWriterProxy::OnWriteEvent() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  if (buffer_) {
    MessageChain* send = buffer_;
    buffer_ = nullptr;
    srs_error_t err = write2sock(send);
    if (err != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_ERROR("OnWriteEvent write error, desc:" << srs_error_desc(err));
      }

      //send cached by buffer
      delete err;
      return;
    }

    // sent ok
    send->DestroyChained();
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

//AsyncSokcetWrapperDeleter
class AsyncSokcetWrapperDeleter {
 public:
  void operator()(AsyncSokcetWrapper* socket) const {
    if (IS_CURRENT_THREAD(socket->GetThread())) {
      delete socket;
      return;
    }

    socket->GetThread()->PostTask(RTC_FROM_HERE, [socket] {
        delete socket;
      });
  }
};

//HttpProtocalImplFactory
class HttpProtocalImplFactory : public IHttpProtocalFactory {
 public:
  HttpProtocalImplFactory(bool is_server, 
                          rtc::AsyncPacketSocket*, 
                          rtc::AsyncPacketSocket* p2)
    : socket_{new AsyncSokcetWrapper(p2), 
              AsyncSokcetWrapperDeleter()} {
    socket_->Open(true, rtc::ThreadManager::Instance()->CurrentThread());
  }

  std::shared_ptr<IHttpRequestReader> 
  CreateRequestReader(IHttpRequestReader::CallBack* callback) override {
    return std::dynamic_pointer_cast<IHttpRequestReader>(
        std::make_shared<HttpRequestReader>(socket_, callback));
  }
  
  std::shared_ptr<IHttpResponseWriter> 
  CreateResponseWriter(bool flag_stream) override {
    return std::dynamic_pointer_cast<IHttpResponseWriter>(
        std::make_shared<HttpResponseWriterProxy>(socket_, flag_stream));
  }
  
  std::unique_ptr<IHttpMessageParser> 
  CreateMessageParser() override {
    return std::make_unique<HttpMessageParser>();
  }

  std::shared_ptr<IHttpResponseReader> 
  CreateResponseReader() override {
    return std::dynamic_pointer_cast<IHttpResponseReader>(
        std::make_shared<HttpResponseReader>(socket_));
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

