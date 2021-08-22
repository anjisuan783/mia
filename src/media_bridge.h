#ifndef __MEDIA_Bridge_H__
#define __MEDIA_Bridge_H__

#include "h/media_gs_adapter.h"
#include "gs/media_cache.h"

namespace ma
{

class MediaSource;
class MediaRequest;
class SrsFileWriter;
class SrsFlvStreamEncoder;

class MediaBridge : public IMediaPublisher,
                    public IVideoSender,
                    public IAudioSender {
 public:
  MediaBridge();
  ~MediaBridge();

 private:
  void OnPublish(const std::string& tcUrl, const std::string& stream) override;
  void OnUnpublish() override;
 
  void OnVideo(CDataPackage&, uint32_t timestamp, IMediaPublisher::Type) override;
  void OnAudio(CDataPackage&, uint32_t timestamp) override;

  void on_audio(CDataPackage& data, DWORD dwTimeStamp) override;
  void on_video(CDataPackage& data, DWORD dwTimeStamp, AVCPackageType packageType) override;

  void to_file();

 private:
  bool pushed_{false};
  std::shared_ptr<MediaSource> source_;

  std::shared_ptr<MediaRequest> req_;

  std::unique_ptr<MediaCache> flv_adaptor_;

  std::unique_ptr<SrsFileWriter> file_writer_;
  
  std::unique_ptr<SrsFlvStreamEncoder> flv_encoder_;

  bool debug_{false};
};

}
#endif //!__MEDIA_Bridge_H__
