//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __WA_SDP_PROCESSOR_H__
#define __WA_SDP_PROCESSOR_H__

#include <string>
#include <vector>
#include <map>
#include <iostream>

#include "libsdptransform/include/json.hpp"
#include "h/rtc_stack_api.h"
#include "webrtc_track_interface.h"

using JSON_TYPE = nlohmann::json;

namespace wa {

struct FormatPreference;

struct SessionInfo {
  int decode(const JSON_TYPE& session);
  void encode(JSON_TYPE& session);
  void clear();
  std::string ice_ufrag_;
  std::string ice_pwd_;
  std::string ice_options_;
  std::string fingerprint_algo_;
  std::string fingerprint_;
  std::string setup_;
};

class MediaDesc {
 public:
  //UDP RFC 8445
  //TCP RFC 6544
  struct Candidate {
    void encode(JSON_TYPE& session);
    void decode(const JSON_TYPE& session);

    std::string foundation_;
    int32_t component_{0};
    std::string transport_type_;
    int32_t priority_{0};
    std::string ip_;
    int32_t port_{0};
    std::string type_;
  };

  struct rtpmap {
    rtpmap() = default;
    rtpmap(const rtpmap&) = default;
    rtpmap(rtpmap&&);
    rtpmap& operator=(const rtpmap&) = default;
  
    void encode_rtp(JSON_TYPE& session);
    void encode_fb(JSON_TYPE& session);
    void encode_fmtp(JSON_TYPE& session);
    std::string decode(const JSON_TYPE& session);
    
    int32_t payload_type_;
    std::string encoding_name_;
    int32_t clock_rate_;
    std::string encoding_param_;
    std::vector<std::string> rtcp_fb_;  //rtcp-fb
    std::string fmtp_;

    std::vector<rtpmap> related_;
  };

  struct SSRCGroup {
    void encode(JSON_TYPE& session);
    void decode(const JSON_TYPE& session);
    // e.g FIX, FEC, SIM.
    std::string semantic_;
    // SSRCs of this type. 
    std::vector<uint32_t> ssrcs_;
  };

  struct SSRCInfo {
    void encode(JSON_TYPE& session);
    uint32_t ssrc_;
    std::string cname_;
    std::string msid_;
    std::string msid_tracker_;
    std::string mslabel_;
    std::string label_;
  };

  struct RidInfo {
    std::string id_;
    std::string direction_;
    std::string params_;
  };

 public:
  MediaDesc(SessionInfo&);
  
  //parse m line
  void parse(const JSON_TYPE& session);
  
  void encode(JSON_TYPE&);
  
  inline bool isAudio() const { return type_ == "audio"; }
  
  inline bool isVideo() const { return type_ == "video"; }
  
  TrackSetting getTrackSettings();
  
  int32_t filterMediaPayload(const FormatPreference& option);
  
  std::string setSsrcs(const std::vector<uint32_t>& ssrcs, 
                       const std::string& inmsid);

  bool filterByPayload(int32_t payload, bool, bool, bool);
  
  void filterExtmap();

  void clearSsrcInfo();
 private:
  void parseCandidates(const JSON_TYPE& media);

  void parseRtcpfb(const JSON_TYPE& media);
  
  void parseFmtp(const JSON_TYPE& media);

  void parseSsrcInfo(const JSON_TYPE& media);

  SSRCInfo& fetchOrCreateSsrcInfo(uint32_t ssrc);
  
  rtpmap* findRtpmapWithPayloadType(int payload_type);

  void parseSsrcGroup(const JSON_TYPE& media);

  void buildSettingFromExtmap(TrackSetting& settings);

 public:
  std::string type_;
  std::string preference_codec_;
  
  int32_t port_{0};
  
  int32_t numPorts_{0};

  std::string protocols_;

  std::string payloads_;

  std::vector<Candidate> candidates_;
  
  SessionInfo& session_info_;

  std::string mid_;

  
  struct extmap_item {
    int id;
    std::string direction;
    std::string param;

    bool operator==(const std::string& in_param) const {
      if (!direction.empty())
        return false;
      return param == in_param;
    }
  };
  std::map<int, extmap_item> extmaps_;

  std::string direction_;  // "recvonly" "sendonly" "sendrecv"
  
  std::string msid_;

  std::string rtcp_mux_;

  std::vector<rtpmap> rtp_maps_;

  std::vector<SSRCInfo>  ssrc_infos_;
  
  std::string rtcp_rsize_;  // for video

  std::vector<SSRCGroup> ssrc_groups_; 

  //rids for simulcast not support
  std::vector<RidInfo> rids_;

  bool disable_audio_gcc_{false};
};

/*
 *  Unified plan only
 */
class WaSdpInfo {
 public:
  WaSdpInfo();
  
  WaSdpInfo(const std::string& sdp);

  int init(const std::string& sdp);

  inline bool empty() { 
    return media_descs_.empty(); 
  }

  void media() {}

  void rids() {}
  
  std::string mediaType(const std::string& mid);

  std::string mediaDirection(const std::string& mid);

  int32_t filterMediaPayload(const std::string& mid, 
                             const FormatPreference& type);

  bool filterByPayload(const std::string& mid, 
                       int32_t payload,
                       bool disable_red = false,
                       bool disable_rtx = false,
                       bool disable_ulpfec = false);

  void filterExtmap();

  int32_t getMediaPort(const std::string& mid);

  bool setMediaPort(const std::string& mid, int32_t port);
  
  std::string singleMediaSdp(const std::string& mid);

  void setCredentials(const WaSdpInfo&);
  
  void SetToken(const std::string&);

  void setCandidates(const WaSdpInfo&);
  
  TrackSetting getTrackSettings(const std::string& mid);

  void mergeMedia() {}
  
  void compareMedia() {}

  void getLegacySimulcast() {}

  void setSsrcs() {}

  WaSdpInfo* answer();
  
  std::string toString(const std::string& strMid = "");
 public:
  // version "v="
  int version_{0};

  // origin "o="
  std::string username_;
  uint64_t session_id_;
  uint32_t session_version_;
  std::string nettype_;
  int32_t addrtype_;
  std::string unicast_address_;

  // session_name  "s="
  std::string session_name_;

  // timing "t="
  int64_t start_time_;
  int64_t end_time_;

  //bool session_in_medias_{false};
  SessionInfo session_info_;

  std::vector<std::string> groups_;
  std::string group_policy_;

  std::string msid_semantic_;
  std::vector<std::string> token_;

  // m-line, media sessions  "m="
  std::vector<MediaDesc> media_descs_;

  bool ice_lite_{false};
  bool enable_extmapAllowMixed_{false};
  std::string extmapAllowMixed_;
};

} //namespace wa

#endif //!__WA_SDP_PROCESSOR_H__

