#include "http/http_protocal_impl.h"

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
  
  worker_ = rtc::ThreadManager::Instance()->WrapCurrentThread();
}

void AsyncSokcetWrapper::Close() {
  if (conn_) {
    conn_->Close();
    conn_.reset(nullptr);
  }
}

srs_error_t AsyncSokcetWrapper::Write(const char* c_data, int c_size, int* sent) {
  
  MessageChain msg(c_size, c_data, MessageChain::DONT_DELETE, c_size);
  return Write(msg, sent);
}

srs_error_t AsyncSokcetWrapper::Write(MessageChain& data, int* sent) {
  srs_error_t err = srs_success;

  if (UNLIKELY(buffer_full_)) {
    err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send");
  }

  auto msg = data.DuplicateChained();
  auto f = [msg, this] () {
    srs_error_t err = srs_success;
    //Write_i will destroy msg
    if ((err=Write_i(msg)) != srs_success) {
      if (srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK) {
        buffer_full_ = true;
      } else {
        MLOG_CERROR("code:%d, desc:%s", srs_error_code(err), srs_error_desc(err));
      }
      delete err;
    }
  };
  
  worker_->PostTask(RTC_FROM_HERE, [weak_this=weak_from_this(), f] {
    if (auto this_ptr = weak_this.lock()) {
      f();
    }
  });

  // We claim to have sent the whole thing, even if we only sent partial
  if (sent) {
    *sent = data.GetChainedLength();
  }

  return err;
}

void AsyncSokcetWrapper::OnReadEvent(rtc::AsyncPacketSocket*,
                                     const char* c_msg,
                                     size_t c_size,
                                     const rtc::SocketAddress&,
                                     const int64_t&) {
  MA_ASSERT_RETURN(c_size, );
  MLOG_DEBUG(std::string(c_msg, c_size));
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
  MLOG_INFO("");

  if (cur_) {
    srs_error_t err = srs_success;

    MessageChain* tmp = cur_;
    cur_ = nullptr;
    
    //Write_i will destroy tmp
    if ((err = Write_i(tmp)) != srs_success) {
      if (srs_error_code(err) == ERROR_SOCKET_WOULD_BLOCK) {
        buffer_full_ = true;
      } else {
        MLOG_CERROR("code:%d, desc:%s", srs_error_code(err), srs_error_desc(err));
      }
      delete err;

      return;
    }
  }

  buffer_full_ = false;
}

srs_error_t AsyncSokcetWrapper::Write_i(const char* c_data, int c_size, int* sent) {
  srs_error_t err = srs_success;
  
  int size = c_size;
  const char* data = c_data;
  size_t sent_size = 0;
  do {
    rtc::PacketOptions option;
    int ret = conn_->Send(data, size, option);
    if (LIKELY(ret > 0)) {
      sent_size += ret;
      data += ret;
      
      if (ret < size) {
        // partal sent          
        int left = c_size - sent_size;
        MessageChain msg(left, c_data+sent_size, MessageChain::DONT_DELETE, left);

        MA_ASSERT(cur_ == nullptr);
        cur_ = msg.DuplicateChained();

        err =  srs_error_new(ERROR_SOCKET_WOULD_BLOCK, 
            "need on send, sent:%d", sent_size);
        if (sent) {
          *sent = sent_size;
        }
        break;
      }

      // total sent continue
      size = c_size - sent_size;
      MA_ASSERT(size >= 0);
    } else /*if ret <= 0*/ {
      //split packet no more than kMaxPacketSize
      if (conn_->GetError() == EMSGSIZE) {
        size = kMaxPacketSize;
        data += sent_size;
        continue;
      } else {
        int left = c_size - sent_size;
        MessageChain msg(left, c_data+sent_size, MessageChain::DONT_DELETE, left);
        
        MA_ASSERT(cur_ == nullptr);
        cur_ = msg.DuplicateChained();
        
        if (conn_->GetError() == EWOULDBLOCK) {
          err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, 
              "need on send, sent:%d", sent_size);
        } else {
          err = srs_error_new(ERROR_SOCKET_ERROR, 
              "unexpect low level error code:%d", conn_->GetError());
        }
        break;
      }
    }
  } while(size > 0);

  return err;
}

//msg must be duplicated
srs_error_t AsyncSokcetWrapper::Write_i(MessageChain* msg) {
  srs_error_t err = srs_success;
  if (UNLIKELY(cur_)) {
    cur_->Append(msg);
    err = srs_error_new(ERROR_SOCKET_WOULD_BLOCK, "need on send, sent");
  } else {
    MessageChain* pnext = msg;
    while (pnext) {
      if (pnext->GetFirstMsgLength()) {
        const char* c_data = pnext->GetFirstMsgReadPtr();
        if ((err=Write_i(c_data, pnext->GetFirstMsgLength(), nullptr)) != srs_success) {
          MessageChain* pPre = pnext;
          pnext = pnext->GetNext();
          if (pnext) {
            MA_ASSERT(cur_);
            pPre->NulNext();
            cur_->Append(pnext);
          }
          break;
        }
      }
      pnext = pnext->GetNext();
    }

    msg->DestroyChained();
  }
  return err;
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

std::optional<std::shared_ptr<ISrsHttpMessage>>
HttpMessageParser::parse_message(std::string_view str_msg) {
  srs_error_t err = srs_success;

  if (state_ == SrsHttpParseStateStart ||
      state_ == SrsHttpParseStateHeaderComplete ||
      state_ == SrsHttpParseStateBody) {
    // continue parse message
    if ((err = parse_message_imp(str_msg)) != srs_success) {
      MLOG_CERROR("parse message body, code:%d, desc:%s", 
          srs_error_code(err), srs_error_desc(err).c_str());
      delete err;
      // Reset request data.
      state_ = SrsHttpParseStateInit;
      return std::nullopt;
    }

    switch(state_) {
    case SrsHttpParseStateInit:
      MA_ASSERT_RETURN(false, std::nullopt);
    case SrsHttpParseStateStart:
      return std::nullopt;
    case SrsHttpParseStateHeaderComplete:
    case SrsHttpParseStateBody:
    case SrsHttpParseStateMessageComplete:
      break;
    default:
      MA_ASSERT_RETURN(false, std::nullopt);
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
        MLOG_CERROR("set url=%s, jsonp=%d, code:%d, desc:%s", 
            url_.c_str(), jsonp_, srs_error_code(err), srs_error_desc(err).c_str());
        delete err;
        // Reset request data.
        state_ = SrsHttpParseStateInit;
        return std::nullopt;
      }

      msg_out_ = std::move(msg);
    }

    if (state_ == SrsHttpParseStateMessageComplete) {
      // Reset request data. for the next message parsing
      state_ = SrsHttpParseStateInit;

      // parse message ok
      msg_out_->set_body_eof();
      return std::move(msg_out_);
    }
   
    // for body parsing, only parsing header ok.
    return msg_out_;
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
    MLOG_CERROR("parse message, code:%d, desc:%s", 
        srs_error_code(err), srs_error_desc(err).c_str());
    delete err;

    // Reset request data.
    state_ = SrsHttpParseStateInit;
    return std::nullopt;
  }

  switch(state_) {
  case SrsHttpParseStateInit:
    MA_ASSERT_RETURN(false, std::nullopt);
  case SrsHttpParseStateStart:
    return std::nullopt;
  case SrsHttpParseStateHeaderComplete:
  case SrsHttpParseStateBody:
  case SrsHttpParseStateMessageComplete:
    break;
  default:
    MA_ASSERT_RETURN(false, std::nullopt);
  }

  auto msg = std::make_shared<HttpMessage>(buffer_view_);

  // Initialize the basic information.
  msg->set_basic(hp_header_.type, 
                 hp_header_.method, 
                 hp_header_.status_code, 
                 hp_header_.content_length);
  msg->set_header(*(header_.get()), http_should_keep_alive(&hp_header_));
  if ((err = msg->set_url(url_, jsonp_)) != srs_success) {
    MLOG_CERROR("set url=%s, jsonp=%d, code:%d, desc:%s", 
        url_.c_str(), jsonp_, srs_error_code(err), srs_error_desc(err).c_str());
    delete err;
    // Reset request data.
    state_ = SrsHttpParseStateInit;
    return std::nullopt;
  }

  if (state_ != SrsHttpParseStateMessageComplete) {
    //save message pointer for body parsing
    msg_out_ = msg;
  } else {
    // Reset request data. for the next message parsing
    state_ = SrsHttpParseStateInit;
    msg->set_body_eof();
  }
  
  // parse message ok or parse header ok.
  return std::move(msg);
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
  
  MLOG_CINFO("Header value(%d bytes): %.*s", (int)length, (int)length, at);
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

  MLOG_CINFO("Body: %.*s", (int)length, at);

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
HttpResponseWriter::HttpResponseWriter(
    std::shared_ptr<AsyncSokcetWrapper> s, bool flag_stream)
  : socket_{s},
    header_{std::make_unique<SrsHttpHeader>()} {

  //only one thread can write
  thread_check_.Detach();
}

void HttpResponseWriter::open() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  socket_->SetWriter(weak_from_this());
}

srs_error_t HttpResponseWriter::final_request() {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  srs_error_t err = srs_success;

  // write the header data in memory.
  if (!header_wrote_) {
    write_header(SRS_CONSTS_HTTP_OK);
  }

  // whatever header is wrote, we should try to send header.
  if ((err = send_header(NULL, 0)) != srs_success) {
    return srs_error_wrap(err, "send header");
  }

  // complete the chunked encoding.
  if (content_length_ == -1) {
    std::stringstream ss;
    ss << 0 << SRS_HTTP_CRLF << SRS_HTTP_CRLF;
    std::string ch = ss.str();
    return socket_->Write(ch.data(), (int)ch.length(), NULL);
  }

  // flush when send with content length
  return write(NULL, 0);
}

SrsHttpHeader* HttpResponseWriter::header() {
  return header_.get();
}

srs_error_t HttpResponseWriter::write(const char* data, int size) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  srs_error_t err = srs_success;
    
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
  if ((err = send_header(data, size)) != srs_success) {
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
    return socket_->Write(data, size, NULL);
  }
  
  // send in chunked encoding.
  int nb_size = snprintf(header_cache_, SRS_HTTP_HEADER_CACHE_SIZE, "%x", size);

  MessageChain p0{(uint32_t)nb_size, header_cache_, MessageChain::DONT_DELETE, (uint32_t)nb_size};
  MessageChain p1{2, (char*)SRS_HTTP_CRLF, MessageChain::DONT_DELETE, 2};
  MessageChain p2{(uint32_t)size, (char*)data, MessageChain::DONT_DELETE, (uint32_t)size};
  MessageChain p3{2, (char*)SRS_HTTP_CRLF, MessageChain::DONT_DELETE, 2};

  p0.Append(&p1);
  p1.Append(&p2);
  p2.Append(&p3);
  
  if ((err = socket_->Write(p0, nullptr)) != srs_success) {
    return srs_error_wrap(err, "write chunk");
  }
  
  return err;
}

srs_error_t HttpResponseWriter::writev(const iovec* iov, int iovcnt, ssize_t* pnwrite) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  srs_error_t err = srs_success;
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

  if (data) {
    std::string msg{std::move(data->FlattenChained())};
    err = write(msg.c_str(), msg.length());

    data->DestroyChained();
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

srs_error_t HttpResponseWriter::send_header(const char* data, int size) {
  
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
  
  std::string buf = ss.str();
  return socket_->Write(buf.c_str(), buf.length(), nullptr);
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
    return std::make_shared<HttpResponseWriter>(socket_, flag_stream);
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

