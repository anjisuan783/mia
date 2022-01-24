// Copyright (C) <2020> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "RtcAdapter.h"

#include <memory>
#include <mutex>

#include "rtc_base/clock.h"
#include "rtc_adapter/thread/ProcessThreadMock.h"
#include "rtc_adapter/thread/StaticTaskQueueFactory.h"
#include "rtc_adapter/AdapterInternalDefinitions.h"
#include "rtc_adapter/AudioSendAdapter.h"
#include "rtc_adapter/VideoSendAdapter.h"
#include "rtc_adapter/AudioReceiveAdapter.h"
#include "rtc_adapter/VideoReceiveAdapter.h"

namespace rtc_adapter {

/////////////////////////
//RtcAdapterImpl
class RtcAdapterImpl : public RtcAdapter,
                       public CallOwner {
public:
  RtcAdapterImpl(webrtc::TaskQueueBase*);
  virtual ~RtcAdapterImpl();

  // Implement RtcAdapter
  VideoReceiveAdapter* createVideoReceiver(const Config&) override;
  void destoryVideoReceiver(VideoReceiveAdapter*) override;
  
  VideoSendAdapter* createVideoSender(const Config&) override;
  void destoryVideoSender(VideoSendAdapter*) override;
  
  AudioReceiveAdapter* createAudioReceiver(const Config&) override;
  void destoryAudioReceiver(AudioReceiveAdapter*) override;
  
  AudioSendAdapter* createAudioSender(const Config&) override;
  void destoryAudioSender(AudioSendAdapter*) override;

  // Implement CallOwner
  std::shared_ptr<webrtc::Call> call() override { return call_; }
  std::shared_ptr<rtc::TaskQueue> taskQueue() override { return taskQueue_; }
  webrtc::RtcEventLog* eventLog() override { return eventLog_; }

private:
  void initCall();

  std::unique_ptr<webrtc::TaskQueueFactory> m_taskQueueFactory;
  std::shared_ptr<rtc::TaskQueue> taskQueue_;
  webrtc::RtcEventLog* eventLog_;
  std::shared_ptr<webrtc::Call> call_;
};

RtcAdapterImpl::RtcAdapterImpl(webrtc::TaskQueueBase* p)
  : m_taskQueueFactory(createDummyTaskQueueFactory(p)),
    taskQueue_(std::make_shared<rtc::TaskQueue>(m_taskQueueFactory->CreateTaskQueue(
                "CallTaskQueue",
                webrtc::TaskQueueFactory::Priority::NORMAL))),
    eventLog_(nullptr) {
}

RtcAdapterImpl::~RtcAdapterImpl() {
}

void RtcAdapterImpl::initCall() {
  if (call_) {
    return;
  }
  
  webrtc::Call::Config call_config(eventLog_);
  call_config.task_queue_factory = m_taskQueueFactory.get();

  std::unique_ptr<webrtc::ProcessThread> moduleThread =
    std::make_unique<ProcessThreadMock>(taskQueue_.get());
    
  call_.reset(
    webrtc::Call::Create(call_config, 
                         webrtc::Clock::GetRealTimeClock(), 
                         std::move(moduleThread)));
}

VideoReceiveAdapter* RtcAdapterImpl::createVideoReceiver(const Config& config) {
  initCall();
  return new VideoReceiveAdapterImpl(this, config);
}

void RtcAdapterImpl::destoryVideoReceiver(VideoReceiveAdapter* video_recv_adapter) {
  VideoReceiveAdapterImpl* impl = static_cast<VideoReceiveAdapterImpl*>(video_recv_adapter);
  delete impl;
}

VideoSendAdapter* RtcAdapterImpl::createVideoSender(const Config& config) {
  return new VideoSendAdapterImpl(this, config);
}

void RtcAdapterImpl::destoryVideoSender(VideoSendAdapter* video_send_adapter) {
  VideoSendAdapterImpl* impl = static_cast<VideoSendAdapterImpl*>(video_send_adapter);
  delete impl;
}

AudioReceiveAdapter* RtcAdapterImpl::createAudioReceiver(const Config& config) {
  initCall();
  return new AudioReceiveAdapterImpl(this, config);
}

void RtcAdapterImpl::destoryAudioReceiver(AudioReceiveAdapter* audio_recv_adapter) {
  AudioReceiveAdapterImpl* impl = static_cast<AudioReceiveAdapterImpl*>(audio_recv_adapter);
  delete impl;
}

AudioSendAdapter* RtcAdapterImpl::createAudioSender(const Config& config) {
  return new AudioSendAdapterImpl(this, config);
}

void RtcAdapterImpl::destoryAudioSender(AudioSendAdapter* audio_send_adapter) {
  AudioSendAdapterImpl* impl = static_cast<AudioSendAdapterImpl*>(audio_send_adapter);
  delete impl;
}

/////////////////////////
//RtcAdapterFactory
RtcAdapterFactory::RtcAdapterFactory(webrtc::TaskQueueBase* task_queue) 
  : task_queue_(task_queue) {
}

std::shared_ptr<RtcAdapter> RtcAdapterFactory::CreateRtcAdapter() {
  if (!adapter_) {
    adapter_ = std::dynamic_pointer_cast<RtcAdapter>(
        std::make_shared<RtcAdapterImpl>(task_queue_));
  }
  return adapter_;
}

} // namespace rtc_adapter

