# MIA

stream server library written with c++

## Build:

git clone https://github.com/anjisuan783/mia.git

cd mia

./script/buildAll.sh

Edit conf/mia.cfg, correct candidates.

nohup ./mia -l conf/log4cxx.properties -c conf/mia.cfg &

CentOS 7 supported only now!

## Features
- [x] Supported protocal
  - [x] 1. WebRTC
  - [x] 2. Http(s) flv
  - [ ] 3. RTMP
  - [ ] 4. HLS
- [x] Supported codec
  - [x] H264 
  - [x] OPUS 
  - [x] AAC
- [x] Supported browser
  - [x] Google Chrome
  - [x] Edge
  - [x] Firefox
- [x] Publishing & playing mode
  - [x] 1.WebRTC publishing, WebRTC playing
  - [x] 2.WebRTC publishing, rtmp playing
  - [x] 3.Rtmp publishing, rtmp playing
  - [x] 4.Rtmp publishing, WebRTC playing

- [ ] H264 
  - [ ] AVC simulcast
  - [ ] SVC
- [ ] Single udp port for deployment.
- [ ] WebRTC pulling

## Rtc API

### 1. WebRTC api is compating with SRS.
   You can publish and playing with SRS players.

### 2. C++ api

class MediaRtcPublisherApi {
 public:
  virtual ~MediaRtcPublisherApi() = default;
  virtual void OnPublish(const std::string& tcUrl, const std::string& stream) = 0;
  virtual void OnUnpublish() = 0;
  virtual void OnFrame(owt_base::Frame&) = 0;
};

class MediaRtmpPublisherApi {
 public:
  virtual ~MediaRtmpPublisherApi() = default;
  virtual void OnPublish(const std::string& tcUrl, const std::string& stream) = 0;
  virtual void OnUnpublish() = 0;
  virtual void OnVideo(const uint8_t*, uint32_t len, uint32_t timestamp) = 0;
  virtual void OnAudio(const uint8_t*, uint32_t len, uint32_t timestamp) = 0;
};

## AUTHORS

anjisuan783@sina.com

## Releases

developing