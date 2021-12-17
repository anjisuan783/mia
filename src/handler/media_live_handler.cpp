#include "media_live_handler.h"

#include <chrono>

#include "rtc_base/sequence_checker.h"
#include "http/http_consts.h"
#include "utils/protocol_utility.h"
#include "connection/h/conn_interface.h"
#include "live/media_consumer.h"
#include "common/media_message.h"
#include "http/http_stack.h"
#include "http/h/http_message.h"
#include "media_source_mgr.h"
#include "handler/media_error_handler.h"
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

    int sent_{0};
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
    req_{std::move(r)}, 
    worker_{source_->get_worker()} {
  thread_check_.Detach();
  MLOG_TRACE("service created:" << req_->get_stream_url());
}

StreamEntry::~StreamEntry() {
  MLOG_TRACE("service destoryed:" << req_->get_stream_url());
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
      MLOG_FATAL("open file writer failed, desc:" << srs_error_desc(result));
      delete result;
      return;
    }
    
    auto encoder = std::make_unique<SrsFlvStreamEncoder>();

    if (srs_success != 
        (result = encoder->initialize(file_writer.get(), nullptr))) {
      MLOG_FATAL("init encoder, desc:" << srs_error_desc(result));
      delete result;
      return;
    }

    if (srs_success != (result = source_->consumer_dumps(
            consumer.get(), true, true, !encoder->has_cache()))) {
      MLOG_ERROR("dumps consumer, desc:" << srs_error_desc(result));
      delete result;
      return;
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (encoder->has_cache()) {
      if ((result = encoder->dump_cache(consumer.get(), source_->jitter())) 
          != srs_success) {
        MLOG_ERROR("encoder dump cache, desc:" << srs_error_desc(result));
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

void StreamEntry::consumer_push(
    customer& c, std::vector<std::shared_ptr<MediaMessage>>& cache) {
  srs_error_t err = srs_success;
  SrsFlvStreamEncoder* fast = 
      dynamic_cast<SrsFlvStreamEncoder*>(c.encoder_.get());
  
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
      cache.swap(c.cache_);
      break;
    } else {
      c.sent_ += count;
    }
  } while(true);
}

bool StreamEntry::on_timer() {
  RTC_DCHECK_RUN_ON(&thread_check_);

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

  async_task([this, key = msg->connection().get(), 
              shared_writer = std::move(writer)]() {
    RTC_DCHECK_RUN_ON(&thread_check_);
    srs_error_t err = srs_success;

    std::unique_ptr<ISrsBufferEncoder> encoder = 
        std::make_unique<SrsFlvStreamEncoder>();

    // the memory writer.
    std::unique_ptr<SrsFileWriter> bw = 
        std::make_unique<SrsBufferWriter>(shared_writer);

    if ((err = encoder->initialize(bw.get(), nullptr)) != srs_success) {
      MLOG_CERROR("flv encoder initialize failed, desc:%s", 
                  srs_error_desc(err));
      delete err;
      if ((err = shared_writer->final_request()) != srs_success) {
        MLOG_CERROR("flv encoder initialize failed final request, desc:%s", 
                    srs_error_desc(err));
        delete err;
      }
      return;
    }
    
    auto consumer = source_->create_consumer();

    if ((err = source_->consumer_dumps(
        consumer.get(), true, true, !encoder->has_cache())) != srs_success) {
      MLOG_CERROR("dumps consumer, desc:%s", srs_error_desc(err));
      delete err;
      if ((err = shared_writer->final_request()) != srs_success) {
        MLOG_CERROR("consumer_dumps final request, desc:%s", 
                    srs_error_desc(err));
        delete err;
      }
      return;
    }
    
    // if gop cache enabled for encoder, dump to consumer.
    if (encoder->has_cache()) {
      if ((err = encoder->dump_cache(consumer.get(), source_->jitter())) 
          != srs_success) {
        MLOG_CERROR("encoder dump cache, desc:%s", srs_error_desc(err));
        delete err;
        if ((err = shared_writer->final_request()) != srs_success) {
          MLOG_CERROR("encoder dump cache final request, desc:%s", 
                      srs_error_desc(err));
          delete err;
        }
        return;
      }
    }

    add_customer(key, std::move(consumer), std::move(bw), std::move(encoder));
  });

  return srs_success;
}

void StreamEntry::conn_destroy(std::shared_ptr<IMediaConnection> conn) {
  async_task([this, raw_conn = conn.get()]() {
    delete_customer(raw_conn);
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

//MediaFlvPlayHandler
MDEFINE_LOGGER(MediaFlvPlayHandler, "MediaFlvPlayHandler");

MediaFlvPlayHandler::MediaFlvPlayHandler() = default;

MediaFlvPlayHandler::~MediaFlvPlayHandler() = default;

srs_error_t MediaFlvPlayHandler::mount_service(
    std::shared_ptr<MediaSource> s, std::shared_ptr<MediaRequest> r) {

  srs_error_t result = srs_success;
  std::map<std::string, std::shared_ptr<StreamEntry>>::iterator found;
  
  {
    std::string stream_id = srs_generate_stream_url("", r->app, r->stream);
    std::lock_guard<std::mutex> guard(stream_lock_);
    found = steams_.find(stream_id);

    if (found == steams_.end()) {
      auto stream = std::make_shared<StreamEntry>(s, r);
      stream->initialize();
      steams_.emplace(stream_id, stream);
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
    std::shared_ptr<IHttpResponseWriter> writer, 
    std::shared_ptr<ISrsHttpMessage> msg) {

  std::shared_ptr<StreamEntry> handler;
  std::string path = msg->path();

  if (msg->ext() != ".flv") {
    return srs_go_http_error(writer.get(), SRS_CONSTS_HTTP_NotFound);
  }

  auto req = msg->to_request(g_server_.config_.vhost);

  MLOG_INFO("flv player desc schema:" << req->schema << 
            ", host:" << req->host <<
            ", vhost:" << req->vhost << 
            ", app:" << req->app << 
            ", stream:" << req->stream << 
            ", port:" << req->port << 
            ", param:" << req->param);

  {
    std::lock_guard<std::mutex> guard(stream_lock_);
    auto found = steams_.find(req->get_stream_url());
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

} //namespace ma

