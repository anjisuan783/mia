#ifndef __MEDIA_LIVE_RTC_ADAPTOR_H__
#define __MEDIA_LIVE_RTC_ADAPTOR_H__

#include <memory>
#include <deque>

#include "common/media_kernel_error.h"
#include "common/media_message.h"
#include "h/rtc_media_frame.h"
#include "live/media_live_source_sink.h"
#include "rtmp/media_rtmp_format.h"
#include "utils/Worker.h"

namespace ma {

class LiveRtcAdapterSink;
class MediaLiveSource;
class MediaRequest;
class MediaConsumer;
class MediaMetaCache;
class SrsFileWriter;
class ISrsBufferEncoder;
class SrsAudioTranscoder;

class TransformSink {
 public:
  virtual ~TransformSink() = default;
  virtual void OnFrame(owt_base::Frame&) = 0;
};

class AudioTransform {
 public:
  AudioTransform();
  ~AudioTransform();

  srs_error_t Open(TransformSink*, bool debug, const std::string& out_path);
  srs_error_t OnData(std::shared_ptr<MediaMessage> msg);
 private:
  srs_error_t Transcode(SrsAudioFrame* audio, 
      std::vector<SrsAudioFrame*>& out_audios);

 private:
  TransformSink* sink_{nullptr};
  SrsRtmpFormat format_;
  std::unique_ptr<SrsAudioTranscoder> codec_;

  std::unique_ptr<SrsFileWriter> adts_writer_;
};

class Videotransform {
 public:
  Videotransform();
  ~Videotransform();
  srs_error_t Open(TransformSink*, bool debug, const std::string& fileName);
  srs_error_t OnData(std::shared_ptr<MediaMessage> msg);
 private:
  srs_error_t Filter(SrsFormat* format, 
      bool& has_idr, std::deque<SrsSample*>& samples, int& data_len);
  srs_error_t PackageVideoframe(bool idr, MediaMessage* msg, 
      std::deque<SrsSample*>& samples, int len, owt_base::Frame& frame);

 private:
  TransformSink* sink_{nullptr};
  SrsRtmpFormat format_;
  // The metadata cache.
  std::unique_ptr<MediaMetaCache> meta_;

  std::unique_ptr<SrsFileWriter> h264_writer_;
};

class MediaLiveRtcAdaptor final : 
    public std::enable_shared_from_this<MediaLiveRtcAdaptor>,
    public TransformSink {
 public:
  MediaLiveRtcAdaptor(const std::string& streamName);
  ~MediaLiveRtcAdaptor();

  srs_error_t Open(wa::Worker* worker, MediaLiveSource*, 
      LiveRtcAdapterSink* sink, bool enableDebug);
  void Close();

 private:
  srs_error_t OnAudio(std::shared_ptr<MediaMessage>);
  srs_error_t OnVideo(std::shared_ptr<MediaMessage>);
  void OnFrame(owt_base::Frame&) override;
  
  bool OnTimer();

 private:
  std::string stream_name_;
  std::shared_ptr<MediaConsumer> consumer_;
  LiveRtcAdapterSink* rtc_source_{nullptr};
  MediaLiveSource* live_source_{nullptr};

  std::unique_ptr<AudioTransform> audio_;
  std::unique_ptr<Videotransform> video_;
  
  bool enable_debug_ = false;
  std::unique_ptr<SrsFileWriter> debug_writer_;
  std::unique_ptr<ISrsBufferEncoder> debug_flv_encoder_;
};

}

#endif //!__MEDIA_LIVE_RTC_ADAPTOR_H__
