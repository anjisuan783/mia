/*
 * RtpExtensionProcessor.cpp
 */
#include "erizo/rtp/RtpExtensionProcessor.h"
#include <map>
#include <string>
#include <vector>

#include "utils/Clock.h"

namespace erizo {

DEFINE_LOGGER(RtpExtensionProcessor, "rtp.RtpExtensionProcessor");

RtpExtensionProcessor::RtpExtensionProcessor(
      const std::vector<erizo::ExtMap>& ext_mappings) 
  : ext_mappings_{ext_mappings}, 
    video_orientation_{kVideoRotation_0} 
{
  translationMap_["urn:ietf:params:rtp-hdrext:ssrc-audio-level"] = SSRC_AUDIO_LEVEL;
  translationMap_["http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time"] = ABS_SEND_TIME;
  translationMap_["urn:ietf:params:rtp-hdrext:toffset"] = TOFFSET;
  translationMap_["urn:3gpp:video-orientation"] = VIDEO_ORIENTATION;
  translationMap_["http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"] = TRANSPORT_CC;
  translationMap_["http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"]= PLAYBACK_TIME;
  translationMap_["urn:ietf:params:rtp-hdrext:sdes:mid"]= MEDIA_ID;
  translationMap_["urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"]= RTP_ID;
  translationMap_["urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"]= REPARIED_RTP_ID;
  ext_map_video_.fill(UNKNOWN);
  ext_map_audio_.fill(UNKNOWN);
}

void RtpExtensionProcessor::setSdpInfo(std::shared_ptr<SdpInfo> theInfo) 
{
  // We build the Extension Map
  for (unsigned int i = 0; i < theInfo->extMapVector.size(); i++) {
    const ExtMap& theMap = theInfo->extMapVector[i];
    switch (theMap.mediaType) {
      case VIDEO_TYPE:
        if (isValidExtension(theMap.uri) && theMap.value < ext_map_video_.size()) {
          ELOG_DEBUG("Adding RTP Extension for video %s, value %u", theMap.uri.c_str(), theMap.value);
          ext_map_video_[theMap.value] = RTPExtensions((*translationMap_.find(theMap.uri)).second);
        } else {
          ELOG_WARN("Unsupported extension %s", theMap.uri.c_str());
        }
        break;
      case AUDIO_TYPE:
        if (isValidExtension(theMap.uri) && theMap.value < ext_map_audio_.size()) {
          ELOG_DEBUG("Adding RTP Extension for Audio %s, value %u", theMap.uri.c_str(), theMap.value);
          ext_map_audio_[theMap.value] = RTPExtensions((*translationMap_.find(theMap.uri)).second);
        } else {
          ELOG_WARN("Unsupported extension %s", theMap.uri.c_str());
        }
        break;
      default:
        ELOG_WARN("Unknown type of extMap, ignoring");
        break;
    }
  }
}

bool RtpExtensionProcessor::isValidExtension(const std::string& uri) 
{
  auto value = std::find_if(
      ext_mappings_.begin(), ext_mappings_.end(), [uri](const ExtMap &extension) {
    return uri == extension.uri;
  });
  
  return value != ext_mappings_.end() && translationMap_.find(uri) != translationMap_.end();
}

std::pair<std::string, uint32_t> RtpExtensionProcessor::checkNewRid(std::shared_ptr<DataPacket> p) 
{
  const RtpHeader* head = reinterpret_cast<const RtpHeader*>(p->data);
  std::array<RTPExtensions, kRtpExtSize> extMap;
  std::pair<std::string, uint32_t> ret;

  if (head->getExtension()) {
    switch (p->type) {
      case VIDEO_PACKET:
        extMap = ext_map_video_;
        break;
      case AUDIO_PACKET:
        extMap = ext_map_audio_;
        break;
      default:
        ELOG_WARN("Won't check RID for unknown type packets");
        return ret;
        break;
    }

    uint16_t totalExtLength = head->getExtLength();
    if (head->getExtId() == 0xBEDE) {
      char* extBuffer = (char*)&head->extensions;  // NOLINT
      uint8_t extByte = 0;
      uint16_t currentPlace = 1;
      uint8_t extId = 0;
      uint8_t extLength = 0;
      while (currentPlace < (totalExtLength*4)) {
        extByte = (uint8_t)(*extBuffer);
        extId = extByte >> 4;
        extLength = extByte & 0x0F;
        if (extId != 0 && extMap[extId] != 0) {
          if (extMap[extId] == RTP_ID) {
            std::string rid;
            for (uint8_t i = 1; i <= extLength + 1; i++) {
              rid.push_back(*(extBuffer + i));
            }
            auto it = rids_.find(rid);
            if (it != rids_.end()) {
              if (it->second != head->getSSRC()) {
                ELOG_WARN("Conflict SSRC(%u, %u) for RID(%s)",
                  it->second, head->getSSRC(), rid.c_str());
              }
            } else {
              ELOG_INFO("New RID:%s, SSRC:%u", rid.c_str(), head->getSSRC());
              ret.first = rid;
              ret.second = head->getSSRC();
              rids_.insert(ret);
            }
          }
        }
        extBuffer = extBuffer + extLength + 2;
        currentPlace = currentPlace + extLength + 2;
      }
    }
  }
  return ret;
}

uint32_t RtpExtensionProcessor::processRtpExtensions(std::shared_ptr<DataPacket> p) 
{
  const RtpHeader* head = reinterpret_cast<const RtpHeader*>(p->data);
  uint32_t len = p->length;
  std::array<RTPExtensions, kRtpExtSize> extMap;

  last_mid_.clear();
  last_rid_.clear();
  if (head->getExtension()) {
    switch (p->type) {
      case VIDEO_PACKET:
        extMap = ext_map_video_;
        break;
      case AUDIO_PACKET:
        extMap = ext_map_audio_;
        break;
      default:
        ELOG_WARN("Won't process RTP extensions for unknown type packets");
        return 0;
    }
    
    uint16_t totalExtLength = head->getExtLength();
    if (head->getExtId() == 0xBEDE) {
      char* extBuffer = (char*)&head->extensions;  // NOLINT
      uint8_t extByte = 0;
      uint16_t currentPlace = 1;
      uint8_t extId = 0;
      uint8_t extLength = 0;
      while (currentPlace < (totalExtLength*4)) {
        extByte = (uint8_t)(*extBuffer);
        extId = extByte >> 4;
        extLength = extByte & 0x0F;
        if (extId != 0 && extMap[extId] != 0) {
          switch (extMap[extId]) {
            case ABS_SEND_TIME:
              // processAbsSendTime(extBuffer);
              break;
            case VIDEO_ORIENTATION:
              processVideoOrientation(extBuffer);
              break;
            case MEDIA_ID:
              processMid(extBuffer);
              break;
            case RTP_ID:
              processRid(extBuffer);
              break;
            default:
              break;
          }
        }
        extBuffer = extBuffer + extLength + 2;
        currentPlace = currentPlace + extLength + 2;
      }
    }
  }
  return len;
}

VideoRotation RtpExtensionProcessor::getVideoRotation() 
{
  return video_orientation_;
}

uint32_t RtpExtensionProcessor::processVideoOrientation(char* buf) 
{
  VideoOrientation* head = reinterpret_cast<VideoOrientation*>(buf);
  video_orientation_ = head->getVideoOrientation();
  return 0;
}

uint32_t RtpExtensionProcessor::processAbsSendTime(char* buf) 
{
  wa::duration now = wa::clock::now().time_since_epoch();
  AbsSendTimeExtension* head = reinterpret_cast<AbsSendTimeExtension*>(buf);
  auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now);
  auto now_usec = std::chrono::duration_cast<std::chrono::microseconds>(now);

  uint32_t now_usec_only = now_usec.count() - now_sec.count()*1e+6;

  uint8_t seconds = now_sec.count() & 0x3F;
  uint32_t absecs = now_usec_only * ((1LL << 18) - 1) * 1e-6;

  absecs = (seconds << 18) + absecs;
  head->setAbsSendTime(absecs);
  return 0;
}

uint32_t RtpExtensionProcessor::processMid(char* extBuffer) 
{
  uint8_t extByte = (uint8_t)(*extBuffer);
  uint8_t extLength = extByte & 0x0F;
  last_mid_.clear();
  for (uint8_t i = 1; i <= extLength + 1; i++) {
    last_mid_.push_back(*(extBuffer + i));
  }
  return 0;
}

uint32_t RtpExtensionProcessor::processRid(char* extBuffer) 
{
  uint8_t extByte = (uint8_t)(*extBuffer);
  uint8_t extLength = extByte & 0x0F;
  last_rid_.clear();
  for (uint8_t i = 1; i <= extLength + 1; i++) {
    last_rid_.push_back(*(extBuffer + i));
  }
  return 0;
}

uint32_t RtpExtensionProcessor::stripExtension(char* buf, int len)
{
  // TODO(pedro)
  return len;
}

}  // namespace erizo

