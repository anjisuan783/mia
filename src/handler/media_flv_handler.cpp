#include "media_flv_handler.h"

#include <chrono>

#include "rtc_base/sequence_checker.h"
#include "http/http_consts.h"
#include "utils/protocol_utility.h"
#include "connection/h/conn_interface.h"
#include "media_consumer.h"
#include "common/media_message.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"
#include "media_source_mgr.h"
#include "handler/media_404_handler.h"
#include "media_source.h"
#include "common/media_io.h"
#include "http/h/http_protocal.h"
#include "rtmp/media_req.h"
#include "common/media_performance.h"
#include "media_server.h"

namespace ma {

class StreamEntry final 
    : public std::enable_shared_from_this<StreamEntry> {

  MDECLARE_LOGGER();

  struct customer {
    customer(std::shared_ptr<MediaConsumer> consumer,
             std::unique_ptr<SrsFileWriter> buffer,
             std::unique_ptr<ISrsBufferEncoder> encoder)
             : consumer_{std::move(consumer)},
               buffer_writer_{std::move(buffer)},
               encoder_{std::move(encoder)} {
    }

    customer(customer&& r)
      : consumer_{std::move(r.consumer_)},
        buffer_writer_{std::move(r.buffer_writer_)},
        encoder_{std::move(r.encoder_)} {
    }

    customer(const customer&) = delete;
    void operator=(const customer&) = delete;
    void operator=(customer&&) = delete;

    bool is_cached() { return !cache_.empty(); }
    
    std::shared_ptr<MediaConsumer> consumer_;
    std::unique_ptr<SrsFileWriter>  buffer_writer_;
    std::unique_ptr<ISrsBufferEncoder> encoder_;

    std::vector<std::shared_ptr<MediaMessage>> cache_;
  };
 public:
  StreamEntry(std::shared_ptr<MediaSource> s,   
              std::shared_ptr<MediaRequest> r);
  ~StreamEntry();

  void initialize();

  void update() { }
  
  srs_error_t serve_http(std::shared_ptr<IHttpResponseWriter> writer, 
                         std::shared_ptr<ISrsHttpMessage> msg);
  void conn_destroy(std::shared_ptr<IMediaConnection>);
 private:
  void add_customer(IMediaConnection* conn, 
                    std::shared_ptr<MediaConsumer> consumer,
                    std::unique_ptr<SrsFileWriter> buffer,
                    std::unique_ptr<ISrsBufferEncoder> encoder);
  void delete_customer(IMediaConnection* conn);
  bool on_timer();
  void consumer_push(customer&, std::vector<std::shared_ptr<MediaMessage>>&);
  void async_task(std::function<void()> f);
 private:
  std::map<IMediaConnection*, customer> conns_customers_; // in worker thread

  std::shared_ptr<MediaSource> source_;
  std::shared_ptr<MediaRequest> req_;
  std::unique_ptr<SrsBufferCache> cache_;

  std::shared_ptr<wa::Worker> worker_;

  webrtc::SequenceChecker thread_check_;
};


MDEFINE_LOGGER(StreamEntry, "StreamEntry");

StreamEntry::StreamEntry(std::shared_ptr<MediaSource> s, 
                         std::shared_ptr<MediaRequest> r) 
  : source_{s},  
    req_{r}, 
    worker_{source_->get_worker()} {
  std::string service_id = srs_generate_stream_url("", req_->app, req_->stream);
  MLOG_TRACE("service created:" << service_id );
  thread_check_.Detach();
}

StreamEntry::~StreamEntry() {
  MLOG_TRACE("service destoryed:" << req_->stream);
}

void StreamEntry::initialize() {
  if (!g_server_.config_.flv_record_) {
    return ;
  }
  
  async_task([this]() {
    auto consumer = source_->create_consumer();
    auto file_writer = std::make_unique<SrsFileWriter>();

    std::string file_writer_path = "/tmp/" + req_->stream + ".flv";
    srs_error_t result = srs_success;
    if (srs_success != (result = file_writer->open(file_writer_path))) {
      MLOG_CFATAL("open file writer failed, code:%d, desc:%s", 
                  srs_error_code(result), srs_error_desc(result).c_str());
      delete result;
      return;
    }
    
    auto encoder = std::make_unique<SrsFlvStreamEncoder>();

    if (srs_success != (result = encoder->initialize(file_writer.get(), nullptr))) {
      MLOG_CFATAL("init encoder, code:%d, desc:%s", 
                  srs_error_code(result), srs_error_desc(result).c_str());
      delete result;
      return;
    }

    if (srs_success != (result = source_->consumer_dumps(
            consumer.get(), true, true, !encoder->has_cache()))) {
      MLOG_CERROR("dumps consumer, code:%d, desc:%s", 
                  srs_error_code(result), srs_error_desc(result).c_str());
      delete result;
      return;
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (encoder->has_cache()) {
      if ((result = encoder->dump_cache(consumer.get(), source_->jitter())) != srs_success) {
        MLOG_CERROR("encoder dump cache, code:%d, desc:%s", 
                    srs_error_code(result), srs_error_desc(result).c_str());
        delete result;
        return;
      }
    }
    add_customer(nullptr, consumer, std::move(file_writer), std::move(encoder));
  });
}

void StreamEntry::add_customer(IMediaConnection* conn, 
                               std::shared_ptr<MediaConsumer> consumer,
                               std::unique_ptr<SrsFileWriter> buffer,
                               std::unique_ptr<ISrsBufferEncoder> encoder) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  bool need_timer = false;
  if (conns_customers_.empty()) {
    need_timer = true;
  }
  
  customer c{consumer, std::move(buffer), std::move(encoder)};
  conns_customers_.emplace(conn, std::move(c));

  if (!need_timer) {
    return;
  }

  static constexpr auto TIME_OUT = std::chrono::milliseconds(SRS_PERF_MW_SLEEP);

  //TODO timer push need optimizing
  std::weak_ptr<StreamEntry> weak_this = shared_from_this();
  worker_->scheduleEvery([weak_this]() {
    if (auto stream = weak_this.lock()) {
      return stream->on_timer();
    }
    
    return false;
  }, TIME_OUT);
}

void StreamEntry::delete_customer(IMediaConnection* conn) {
  RTC_DCHECK_RUN_ON(&thread_check_);
  
  auto found = conns_customers_.find(conn);
  if (found != conns_customers_.end()) {
    conns_customers_.erase(found);
  }  
}

void StreamEntry::consumer_push(customer& c, std::vector<std::shared_ptr<MediaMessage>>& cache) {
  srs_error_t err = srs_success;
  SrsFlvStreamEncoder* fast = dynamic_cast<SrsFlvStreamEncoder*>(c.encoder_.get());
  int count;
  do {
    cache.clear();
    count = 0;

    if (c.is_cached()) {
      c.cache_.swap(cache);
      count = cache.size();
    } else {
      c.consumer_->fetch_packets(SRS_PERF_MW_MSGS, cache, count);
    }
    
    if (count == 0) {
      break;
    }
    
    // check send error code.
    if ((err = fast->write_tags(cache)) != srs_success) {
      if (srs_error_code(err) != ERROR_SOCKET_WOULD_BLOCK) {
        MLOG_ERROR("write_tags failed, code:" << srs_error_code(err) << 
                   " desc:" << srs_error_desc(err));
      }
      delete err;
#ifdef __GS__      
      cache.swap(c.cache_);
#endif
      break;
    }
  } while(true);
}

bool StreamEntry::on_timer() {
  RTC_DCHECK_RUN_ON(&thread_check_);

  MLOG_TRACE("on_timer");
  
  bool result = true;
  if (conns_customers_.empty()) {
    return result = false;
  }
 
  std::vector<std::shared_ptr<MediaMessage>> msgs;
  msgs.reserve(SRS_PERF_MW_MSGS);
  for (auto& i : conns_customers_) {
    consumer_push(i.second, msgs);
  }
  
  return result;
}

srs_error_t StreamEntry::serve_http(std::shared_ptr<IHttpResponseWriter> writer, 
                                    std::shared_ptr<ISrsHttpMessage> msg) {
  MLOG_TRACE(req_->tcUrl);
  writer->header()->set_content_type("video/x-flv");
  writer->write_header(SRS_CONSTS_HTTP_OK);
  
  auto conn = msg->connection();
  
  async_task([this, conn, writer]() {
    RTC_DCHECK_RUN_ON(&thread_check_);
  
    srs_error_t err = srs_success;
    std::unique_ptr<ISrsBufferEncoder> encoder = std::make_unique<SrsFlvStreamEncoder>();
  
    // the memory writer.
    std::unique_ptr<SrsFileWriter> bw = std::make_unique<SrsBufferWriter>(writer.get());
    if ((err = encoder->initialize(bw.get(), nullptr)) != srs_success) {
      MLOG_CERROR("init encoder, code:%d, desc:%s", 
                  srs_error_code(err), srs_error_desc(err).c_str());
      delete err;

      writer->final_request();
      return;
    }
    auto consumer = source_->create_consumer();

    if ((err = source_->consumer_dumps(
        consumer.get(), true, true, !encoder->has_cache())) != srs_success) {
      MLOG_CERROR("dumps consumer, code:%d, desc:%s", 
                  srs_error_code(err), srs_error_desc(err).c_str());
      delete err;
      writer->final_request();
      return;
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (encoder->has_cache()) {
      if ((err = encoder->dump_cache(consumer.get(), source_->jitter())) != srs_success) {
        MLOG_CERROR("encoder dump cache, code:%d, desc:%s", 
                    srs_error_code(err), srs_error_desc(err).c_str());
        delete err;
        writer->final_request();
        return;
      }
    }

    add_customer(conn.get(), consumer, std::move(bw), std::move(encoder));
  });

  return srs_success;
}

void StreamEntry::conn_destroy(std::shared_ptr<IMediaConnection> conn) {
  async_task([this, conn]() {
    delete_customer(conn.get());
  });
}

void StreamEntry::async_task(std::function<void()> f) {
  std::weak_ptr<StreamEntry> weak_this{weak_from_this()};
  worker_->task([weak_this, f] {
    if (auto this_ptr = weak_this.lock()) {
      f();
    }
  });
}

MDEFINE_LOGGER(MediaFlvPlayHandler, "MediaFlvPlayHandler");

MediaFlvPlayHandler::MediaFlvPlayHandler() = default;

MediaFlvPlayHandler::~MediaFlvPlayHandler() = default;

srs_error_t MediaFlvPlayHandler::mount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {

  srs_error_t result = srs_success;
  std::map<std::string, std::shared_ptr<StreamEntry>>::iterator found;
  
  {
    std::string service_id = srs_generate_stream_url("", r->app, r->stream);
    std::lock_guard<std::mutex> guard(stream_lock_);
    found = steams_.find(service_id);

    if (found == steams_.end()) {
      auto stream = std::make_shared<StreamEntry>(s, r);
      stream->initialize();
      steams_.emplace(service_id, stream);
      return result;
    } 
  }
  
  //reuse it and update
  found->second->update();

  return result;
}

void MediaFlvPlayHandler::unmount_service(std::shared_ptr<MediaSource>, 
                                       std::shared_ptr<MediaRequest> r) {

  std::string service_id = srs_generate_stream_url("", r->app, r->stream);
  std::lock_guard<std::mutex> guard(stream_lock_);
  steams_.erase(service_id);
}

void MediaFlvPlayHandler::conn_destroy(std::shared_ptr<IMediaConnection> conn) {

  std::shared_ptr<StreamEntry> handler;
  {
    std::lock_guard<std::mutex> guard(index_lock_);
    auto found = index_.find(conn.get());
    if (found != index_.end()) {
      handler = found->second;
      index_.erase(found);
    }
  }
  
  if (handler) {
    handler->conn_destroy(conn);
  }
}

srs_error_t MediaFlvPlayHandler::serve_http(
    std::shared_ptr<IHttpResponseWriter> writer, std::shared_ptr<ISrsHttpMessage> msg) {

  MLOG_TRACE("");

  assert(msg->is_http_get());

  std::shared_ptr<StreamEntry> handler;
  std::string path = msg->path();

  if (msg->ext() != ".flv") {
    return srs_go_http_error(writer.get(), SRS_CONSTS_HTTP_NotFound);
  }

  size_t found = path.rfind(".");
  if (found != path.npos) {
    path.erase(found, 4);
  }
  MLOG_TRACE(path);
  {
    std::lock_guard<std::mutex> guard(stream_lock_);
    auto found = steams_.find(path);
    if (found == steams_.end()) {
      return srs_go_http_error(writer.get(), SRS_CONSTS_HTTP_NotFound);
    }

    handler = found->second;
  }

  {
    std::lock_guard<std::mutex> guard(index_lock_);
    index_.emplace(msg->connection().get(), handler);
  }
  
  return handler->serve_http(writer, msg);
}

}

