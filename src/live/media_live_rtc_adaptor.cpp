#include "live/media_live_rtc_adaptor.h"

#include "common/media_log.h"
#include "common/media_performance.h"
#include "rtmp/media_req.h"
#include "common/media_message.h"
#include "live/media_live_rtc_adaptor_sink.h"
#include "live/media_live_source.h"
#include "live/media_consumer.h"
#include "rtc/media_rtc_source.h"

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.live");

MediaLiveRtcAdaptor::MediaLiveRtcAdaptor(const std::string& streamName) 
    : stream_name_(streamName) {
  MLOG_TRACE(stream_name_);
}
MediaLiveRtcAdaptor::~MediaLiveRtcAdaptor() {
  MLOG_TRACE(stream_name_);
}

void MediaLiveRtcAdaptor::Open(MediaLiveSource* source, 
                               LiveRtcAdapterSink* sink) {
  live_source_ = source;
  auto consumer = live_source_->CreateConsumer();

  srs_error_t err = nullptr;
  if ((err = source->consumer_dumps(
      consumer.get(), true, true, false)) != srs_success) {
    MLOG_CERROR("dumps consumer, desc:%s", srs_error_desc(err));
    delete err;
    return;
  }
  consumer_ = std::move(consumer);
  rtc_source_ = sink;
  rtc_source_->OnLocalPublish(stream_name_);
}

void MediaLiveRtcAdaptor::Close() {
  live_source_->DestroyConsumer(consumer_.get());
  consumer_ = nullptr;
  rtc_source_->OnLocalUnpublish();
  rtc_source_ = nullptr;
}

srs_error_t MediaLiveRtcAdaptor::OnAudio(
    std::shared_ptr<MediaMessage>, bool from_adaptor) {
  return 0;
}

srs_error_t MediaLiveRtcAdaptor::OnVideo(
    std::shared_ptr<MediaMessage>, bool from_adaptor) {
  return 0;
}

void MediaLiveRtcAdaptor::OnTimer() {
  int count;
  std::vector<std::shared_ptr<MediaMessage>> cache;
  consumer_->fetch_packets(SRS_PERF_MW_MSGS, cache, count);

  //TODO transoding audo from aac to opus
}

}

