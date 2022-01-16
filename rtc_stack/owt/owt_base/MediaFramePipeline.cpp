// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "owt_base/MediaFramePipeline.h"
#include <assert.h>

namespace owt_base {

FrameSource::~FrameSource() {
/*  
  for (auto& it : m_audio_dests) {
    auto p = it.second->lock();
    if (p) {
      p->unsetAudioSource();
    }
  }

  for (auto& it : m_video_dests) {
    auto p = it.second->lock();
    if (p) {
      p->unsetVideoSource();
    }
  }
*/
}

void FrameSource::addAudioDestination(std::shared_ptr<FrameDestination> dest) {
  dest->setAudioSource(std::move(weak_from_this()));
  m_audio_dests.emplace(dest.get(), std::move(dest));
}

void FrameSource::addVideoDestination(std::shared_ptr<FrameDestination> dest) {
  dest->setVideoSource(std::move(weak_from_this()));
  m_video_dests.emplace(dest.get(), std::move(dest));
}

void FrameSource::addDataDestination(std::shared_ptr<FrameDestination> dest) {
  dest->setDataSource(std::move(weak_from_this()));
  m_data_dests.emplace(dest.get(), std::move(dest));
}

void FrameSource::removeAudioDestination(FrameDestination* dest) {
  auto p = m_audio_dests.find(dest);
  if (p != m_audio_dests.end()) {
    p->second.lock()->unsetAudioSource();
    m_audio_dests.erase(p);
  }
}

void FrameSource::removeVideoDestination(FrameDestination* dest) {
  auto p = m_video_dests.find(dest);
  if (p != m_video_dests.end()) {
    p->second.lock()->unsetVideoSource();
    m_video_dests.erase(p);
  }
}

void FrameSource::removeDataDestination(FrameDestination* dest) {
  auto p = m_data_dests.find(dest);
  if (p != m_data_dests.end()) {
    p->second.lock()->unsetDataSource();
    m_data_dests.erase(p);
  }
}

void FrameSource::deliverFrame(std::shared_ptr<Frame> frame) {
  std::unordered_map<FrameDestination*, std::weak_ptr<FrameDestination>> *plist;
  if (isAudioFrame(*frame.get())) {
    plist = &m_audio_dests;
  } else if (isVideoFrame(*frame.get())) {
    plist = &m_video_dests;
  } else if (isDataFrame(*frame.get())){
    plist = &m_data_dests;
  } else {
    assert(false);
    plist = nullptr;
  }

  if (plist) {
    for (auto it = plist->begin(); it != plist->end();) {
      auto p = it->second.lock();
      if (p) {
        p->onFrame(frame);
        ++it;
      } else {
        plist->erase(it++); 
      }
    }
  }
}

void FrameSource::deliverMetaData(const MetaData& metadata) {
    std::unordered_map<FrameDestination*, 
        std::weak_ptr<FrameDestination>> *plist = &m_audio_dests;
  int i = 2;
  do {
    for (auto it = plist->begin(); it != plist->end();) {
      auto p = it->second.lock();
      if (p) {
        p->onMetaData(metadata);
        ++it;
      } else {
        plist->erase(it++); 
      }
    }
  } while(plist = &m_video_dests, --i);
}

/*============================================================================*/
void FrameDestination::setAudioSource(std::weak_ptr<FrameSource> src) {
  m_audio_src = std::move(src);
}

void FrameDestination::setVideoSource(std::weak_ptr<FrameSource> src) {
  m_video_src = std::move(src);
  onVideoSourceChanged();
}

void FrameDestination::setDataSource(std::weak_ptr<FrameSource> src) {
  m_data_src = std::move(src);
}

void FrameDestination::unsetAudioSource() {
  m_audio_src.reset();
}

void FrameDestination::unsetVideoSource() {
  m_video_src.reset();
}

void FrameDestination::unsetDataSource() {
  m_data_src.reset();
}

void FrameDestination::deliverFeedbackMsg(const FeedbackMsg& msg) {
  if (msg.type == AUDIO_FEEDBACK) {
    auto p = m_audio_src.lock();
    if (p) {
      p->onFeedback(msg);
    }
  } else if (msg.type == VIDEO_FEEDBACK) {
    auto p = m_video_src.lock();
    if (p) {
      p->onFeedback(msg);
    }
  } else {
    assert(false);
  }
}

}

