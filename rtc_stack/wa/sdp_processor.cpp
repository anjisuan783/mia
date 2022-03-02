//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#include "sdp_processor.h"

#include <iostream>
#include <sstream>
#include <string.h>
#include <string_view>
#include <random>

#include "common_define.h"
#include "h/rtc_stack_api.h"
#include "libsdptransform/include/sdptransform.hpp"
#include "wa_rtp_define.h"
#include "wa_log.h"
#include "h/rtc_return_value.h"
#include "helper.h"
#include "media_config.h"

//#define __DEBUG_SDP__

using my_json = nlohmann::json;

#define JSON_OBJECT my_json::object()
#define JSON_ARRAY nlohmann::json::array()

namespace wa {

namespace {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("wa.sdp");

std::string get_preference_name(EFormatPreference t) {
  switch(t) {
    case p_h264:
      return "H264";
    case p_opus:
      return "opus";
    default:
      return "unknow";
  }
}

int32_t get_codec_by_preference(std::string_view t) {
  if ("H264" == t)
    return H264_90000_PT;

  if ("opus" == t)
    return OPUS_48000_PT;

  return INVALID_PT;
}

int32_t filer_h264(const std::vector<MediaDesc::rtpmap>& rtpMaps, 
                   const std::string& profile) {
  int32_t result = 0;
  for (auto& item : rtpMaps) {
    if ("H264" != item.encoding_name_) {
      continue;
    }

    result = item.payload_type_;

    std::string_view view_fmtp(item.fmtp_);
    auto pkz_found = view_fmtp.find("packetization-mode=1");
    if (pkz_found == std::string_view::npos) {
      continue;
    }
    
    result = item.payload_type_;

    auto level_found = view_fmtp.find("profile-level-id=");
    if (level_found == std::string_view::npos) {
      continue;
    }

    std::string_view str_level = 
        view_fmtp.substr(level_found + strlen("profile-level-id="), 6);

    //found
    if (str_level == profile) {
      result = item.payload_type_;
      break;
    }
  }
  
  return result;
}

}

int32_t get_pt_by_preference(EFormatPreference t) {
  std::string name = get_preference_name(t);
  return get_codec_by_preference(name);
}

// SessionInfo
void SessionInfo::encode(JSON_TYPE& session) {
  if (!fingerprint_algo_.empty()) {
     JSON_TYPE fingerprint = JSON_OBJECT;
     fingerprint["type"] = fingerprint_algo_;
     fingerprint["hash"] = fingerprint_;
     session["fingerprint"] = fingerprint;
   }

  if (!ice_ufrag_.empty())
    session["iceUfrag"] = ice_ufrag_;

  if (!ice_pwd_.empty())
    session["icePwd"] = ice_pwd_;
  
  if(!ice_options_.empty())
    session["iceOptions"] = ice_options_;

  if (!setup_.empty())
    session["setup"] = setup_;
}

int SessionInfo::decode(const JSON_TYPE& session) {
  auto fingerprint_found = session.find("fingerprint");
  if (fingerprint_found != session.end()) {
    fingerprint_algo_ = (*fingerprint_found).at("type");
    fingerprint_ = (*fingerprint_found).at("hash");
  }
  
  auto iceOptions_found = session.find("iceOptions");
  if(iceOptions_found != session.end()){
    ice_options_ = *iceOptions_found;
  }

  auto ugrag_found = session.find("iceUfrag");
  if(ugrag_found != session.end()){
    ice_ufrag_ = *ugrag_found;
    ice_pwd_ = session.at("icePwd");
  }

  auto setup_found = session.find(
"setup");
  if (setup_found != session.end()) {
    setup_ = *setup_found;
  }
 
  return wa_ok;
}

void SessionInfo::clear() {
  ice_ufrag_.clear();
  ice_pwd_.clear();
  ice_options_.clear();
  fingerprint_algo_.clear();
  fingerprint_.clear();
  setup_.clear();
}

// Candidate
void MediaDesc::Candidate::encode(JSON_TYPE& session) {
  session["foundation"] = foundation_;
  session["component"] = component_;
  session["transport"] = transport_type_;
  session["priority"] = priority_;
  session["ip"] = ip_;
  session["port"] = port_;
  session["type"] = type_;
}

void MediaDesc::Candidate::decode(const JSON_TYPE& session) { 
  foundation_ = session.at("foundation");
  component_ = session.at("component");
  transport_type_ = session.at("transport");
  priority_ = session.at("priority");
  ip_ = session.at("ip");
  port_ = session.at("port");
  type_ = session.at("type");
}

// rtpmap
MediaDesc::rtpmap::rtpmap(rtpmap&& r)
  : payload_type_(r.payload_type_),
    encoding_name_(std::move(r.encoding_name_)),
    clock_rate_(r.clock_rate_),
    encoding_param_(std::move(r.encoding_param_)),
    rtcp_fb_(std::move(r.rtcp_fb_)),
    fmtp_(std::move(r.fmtp_)),
    related_(std::move(r.related_)) {
}

void MediaDesc::rtpmap::encode_rtp(JSON_TYPE& session) {
  session["payload"] = payload_type_;
  session["codec"] = encoding_name_;
  session["rate"] = clock_rate_;
  if(!encoding_param_.empty()){
    session["encoding"] = encoding_param_;
  }
}

void MediaDesc::rtpmap::encode_fb(JSON_TYPE& session) {
  for(size_t i=0; i<rtcp_fb_.size(); ++i){
    JSON_TYPE item = JSON_OBJECT;
    item["payload"] = payload_type_;
    item["type"] = rtcp_fb_[i];
    session.push_back(item);
  }
}

void MediaDesc::rtpmap::encode_fmtp(JSON_TYPE& session) {
  session["payload"] = payload_type_;
  session["config"] = fmtp_;
}

std::string MediaDesc::rtpmap::decode(const JSON_TYPE& session) {
  payload_type_ = session.at("payload");
  encoding_name_ = session.at("codec");
  clock_rate_ = session.at("rate");

  auto encoding_it = session.find("encoding");
  if(encoding_it != session.end()){
    encoding_param_ = *encoding_it;
  }

  return encoding_name_;
}

// SSRCInfo
void MediaDesc::SSRCInfo::encode(JSON_TYPE& session) {
  JSON_TYPE cname = JSON_OBJECT;
  cname["id"] = ssrc_;
  cname["attribute"] = "cname";
  cname["value"] = cname_;
  session.push_back(cname);

  JSON_TYPE msid = JSON_OBJECT;
  msid["id"] = ssrc_;
  msid["attribute"] = "msid";
  std::string msid_value = msid_;
  if(!msid_tracker_.empty()){
    msid_value += " " + msid_tracker_;
  }
  msid["value"] = msid_value;
  session.push_back(msid);

  JSON_TYPE mslabel = JSON_OBJECT;
  mslabel["id"] = ssrc_;
  mslabel["attribute"] = "mslabel";

  mslabel["value"] = mslabel_;
  session.push_back(mslabel);

  JSON_TYPE label = JSON_OBJECT;
  label["id"] = ssrc_;
  label["attribute"] = "label";
  label["value"] = label_;
  session.push_back(label);
}

// SSRCGroup
void MediaDesc::SSRCGroup::encode(JSON_TYPE& session) {
  session["semantics"] = semantic_;
  std::string ssrcs;
  std::ostringstream oss;
  for(size_t i=0; i<ssrcs_.size();){
    oss << ssrcs_[i];
    if(++i < ssrcs_.size()){
      oss << " ";
    }
  }
  ssrcs = oss.str();
  session["ssrcs"] = ssrcs;
}

void MediaDesc::SSRCGroup::decode(const JSON_TYPE& session) {
  semantic_ = session.at("semantics");
  std::string ssrcs = session.at("ssrcs");
  std::istringstream iss(ssrcs);
  unsigned int word;
  while(iss >> word){
    ssrcs_.push_back(word);
  }
}

// MediaDesc
MediaDesc::MediaDesc(SessionInfo& si) 
  : session_info_(si) { }

MediaDesc::SSRCInfo& MediaDesc::fetchOrCreateSsrcInfo(uint32_t ssrc) {
  for(size_t i = 0; i < ssrc_infos_.size(); ++i) {
    if (ssrc_infos_[i].ssrc_ == ssrc) {
      return ssrc_infos_[i];
    }
  }

  SSRCInfo ssrc_info;
  ssrc_info.ssrc_ = ssrc;
  ssrc_infos_.push_back(ssrc_info);

  return ssrc_infos_.back();
}

MediaDesc::rtpmap* MediaDesc::findRtpmapWithPayloadType(int payload_type) {
  for (size_t i = 0; i < rtp_maps_.size(); ++i) {
    if (rtp_maps_[i].payload_type_ == payload_type) {
      return &rtp_maps_[i];
    }
  }
  return nullptr;
}

void MediaDesc::parseCandidates(const JSON_TYPE& media){
  auto found = media.find("candidates");
  
  if(found == media.end()) {
    return;
  }
  
  auto& candidates = media.at("candidates");
  candidates_.resize(candidates.size());
  for(size_t i = 0; i < candidates.size(); ++i){
    candidates_[i].decode(candidates[i]);
  }
}

void MediaDesc::parseRtcpfb(const JSON_TYPE& media) {
  auto fb = media.find("rtcpFb");
  if(fb == media.end()) {
    return;
  }

  for(size_t i = 0; i < (*fb).size(); ++i){
    std::string str_payload = (*fb)[i].at("payload");
    std::istringstream iss(str_payload);
    int p_type = 0;
    iss >> p_type;
   
    rtpmap* found = findRtpmapWithPayloadType(p_type);
    if(!found){
      continue;
    }

    std::string fb_str = (*fb)[i].at("type");
    
    auto sub_type_found = (*fb)[i].find("subtype");
    if(sub_type_found != (*fb)[i].end()){
      std::string fb_sub_type = *sub_type_found;
      fb_str += " " + fb_sub_type;
    }
    found->rtcp_fb_.push_back(fb_str);
  }
}

void MediaDesc::parseFmtp(const JSON_TYPE& media) {
  auto fmtp = media.find("fmtp");
  
  if(fmtp == media.end()){
    return;
  }

  for(size_t i = 0; i < (*fmtp).size(); ++i){
    int p_type = (*fmtp)[i].at("payload");
    rtpmap* found = findRtpmapWithPayloadType(p_type);
    if(!found){
      continue;
    }
    
    found->fmtp_ = (*fmtp)[i].at("config");
    size_t pos = found->fmtp_.find("apt=");
    if(std::string::npos == pos){
      continue;
    }
    
    std::string strAtp = found->fmtp_.substr(pos+4);
    std::istringstream iss(strAtp);
    int32_t atp = 0;
    iss >> atp;

    rtpmap* relate_map = findRtpmapWithPayloadType(atp);

    if(!relate_map){
      continue;
    }
    
    relate_map->related_.push_back(*found);
  }
}

void MediaDesc::parseSsrcInfo(const JSON_TYPE& media) {
  auto found = media.find("ssrcs");
  
  if(found == media.end()) {
    return;
  }

  auto& ssrcs = *found;

  for(size_t i = 0; i < ssrcs.size(); ++i){
    uint32_t _ssrc = ssrcs[i].at("id");
    SSRCInfo& ssrc_info = fetchOrCreateSsrcInfo(_ssrc);
  
    std::string ssrc_attr = ssrcs[i].at("attribute");

#ifdef __DEBUG_SDP__    
    std::cout << "parse ssrc info " << type_ << 
                 " size:" << ssrcs.size() << 
                 " ssrc:" << _ssrc <<
                 ", attr:" << ssrc_attr <<
                 std::endl;
#endif
    std::string ssrc_value{""};
    auto found = ssrcs[i].find("value");
    if (found != ssrcs[i].end()) {
      ssrc_value = *found;
    }
#ifdef __DEBUG_SDP__
        std::cout << "parse ssrc info " << type_ << 
                     " size:" << ssrcs.size() << 
                     " ssrc:" << _ssrc <<
                     ", attr:" << ssrc_attr <<
                     

", v:" << ssrc_value <<
                     std::endl;
#endif

    if (ssrc_attr == "cname") {
      ssrc_info.cname_ = ssrc_value;
      ssrc_info.ssrc_ = _ssrc;
    } else if (!ssrc_value.empty()) { 
      if (ssrc_attr == "msid") {
        std::vector<std::string> vec = wa_split_str(ssrc_value, " ");
        if (!vec.empty()) {
          ssrc_info.msid_ = vec[0];
          if (vec.size() > 1) {
              ssrc_info.msid_tracker_ = vec[1];
          }
        }
      } else if (ssrc_attr == "mslabel") {
          ssrc_info.mslabel_ = ssrc_value;
      } else if (ssrc_attr == "label") {
          ssrc_info.label_ = ssrc_value;
      }
    }
  }
}

void MediaDesc::parseSsrcGroup(const JSON_TYPE& media) {
  auto found = media.find("ssrcGroups");

  if(found == media.end()){
    return;
  }

  ssrc_groups_.resize((*found).size());
  for(size_t i=0; i<(*found).size(); ++i){
    ssrc_groups_[i].decode((*found)[i]);
  }
}

void MediaDesc::parse(const JSON_TYPE& media) {
  MediaDesc& desc = *this;
  desc.type_ = media.at("type");
  desc.port_ = media.at("port");

  auto numPorts_found = media.find("numPorts");
  if(numPorts_found != media.end()){
    desc.numPorts_ = (*numPorts_found);
  }
  
  desc.protocols_ = media.at("protocol"); // "UDP/TLS/RTP/SAVPF"
  desc.payloads_ = media.at("payloads");

  //ignore "c=" "a=rtcp"

  //rtcp
  //rtcport_ = media.at("rtcp").at("port");

  //candidates
  auto candidate_found = media.find("candidates");
  if(candidate_found != media.end()){
    auto& candidates = *candidate_found;
    candidates_.resize(candidates.size());
    for(size_t i = 0; i < candidates.size(); ++i){
      candidates_[i].decode(candidates[i]);
    }
  }

  //ice
  desc.session_info_.decode(media);

  desc.mid_ = media.at("mid");

  //extmap
  auto& ext = media.at("ext");
  for(size_t i = 0; i < ext.size(); ++i){
#ifdef __DEBUG_SDP__
    std::cout << "parse extmap " << type_ <<
                 ext[i].at("value") << 
                 ext[i].at("uri") << std::endl;
#endif
    extmap_item item{ext[i].at("value"), "", ext[i].at("uri")};
    auto dir_found = ext[i].find("direction");
    if (dir_found != ext[i].end())
      item.direction = *dir_found;
    
    desc.extmaps_.emplace(item.id, item);
  }

  desc.direction_ = media.at("direction");

  auto msid_found = media.find("msid");
  if(msid_found != media.end()){
    desc.msid_ = *msid_found;
  }

  {
    auto rtc_mux_found = media.find("rtcpMux");
    if(rtc_mux_found != media.end()){
      rtcp_mux_ = *rtc_mux_found;
    }
  }

  {
    auto rtc_rsize_found = media.find("rtcpRsize");
    if(rtc_rsize_found != media.end()){
      rtcp_rsize_ = *rtc_rsize_found;
    }
  }

  //rtpmap
  rtpmap red_map;
  rtpmap ulpfec_map;
  
  std::string rtp_name;
  
  auto& rtp = media.at("rtp");
  desc.rtp_maps_.resize(rtp.size());
  for(size_t i = 0; i < rtp.size(); ++i){
    rtp_name = desc.rtp_maps_[i].decode(rtp[i]);
    if ("red" == rtp_name) {
      red_map = desc.rtp_maps_[i];
    } else if ("ulpfec" == rtp_name) {
      ulpfec_map = desc.rtp_maps_[i];
    }
  }

  //rtcp-fb
  parseRtcpfb(media);

  //fmt
  parseFmtp(media);

  parseSsrcInfo(media);

  parseSsrcGroup(media);

  //make relations with red ulpfec for video
  if (red_map.encoding_name_.empty() && ulpfec_map.encoding_name_.empty()) {
    return;
  }
  
  for (auto& x : rtp_maps_) {
    if (x.encoding_name_ != "VP8" &&
        x.encoding_name_ != "VP9" &&
        x.encoding_name_ != "H264" &&
        x.encoding_name_ != "H265") {
      continue;
    }

    if (!red_map.encoding_name_.empty()) {
      x.related_.push_back(red_map);
    }

    if (!ulpfec_map.encoding_name_.empty()) {
      x.related_.push_back(ulpfec_map);
    }
  }
}

void MediaDesc::encode(JSON_TYPE& media) {
  //construct
  media["type"] = type_;
  media["port"] = port_;

  if(numPorts_ != 0){
    media["numPorts"] = numPorts_;
  }
  
  media["protocol"] = protocols_; 
  media["payloads"] = payloads_;

  //c=IN IP4 0.0.0.0
  JSON_TYPE connection = JSON_OBJECT;
  connection["version"] = 4;
  connection["ip"] = "0.0.0.0";
  media["connection"] = connection;

  session_info_.encode(media);

  if(!candidates_.empty()){
    //candidate
    JSON_TYPE jcans = JSON_ARRAY;
    for(size_t i = 0; i < candidates_.size(); ++i){
      JSON_TYPE jcan = JSON_OBJECT;
      candidates_[i].encode(jcan);
      jcans.push_back(jcan);
    }
    media["candidates"] = jcans;
  }

  media["mid"] = mid_;

  //extmap
  if(!extmaps_.empty()){
    JSON_TYPE extmap = JSON_ARRAY;
    for(auto& i : extmaps_){
    JSON_TYPE ext = JSON_OBJECT;
      ext["value"] = i.second.id;
      if (!i.second.direction.empty()) {
        ext["direction"] = i.second.direction;
      }
      ext["uri"] = i.second.param;
      extmap.push_back(ext);
    }
    media["ext"] = extmap;
  }

  media["direction"] = direction_;

  if(!msid_.empty())
    media["msid"] = msid_;

  if(!rtcp_mux_.empty()){
    media["rtcpMux"] = rtcp_mux_;
  }

  if(!rtcp_rsize_.empty()){
    media["rtcpRsize"] = rtcp_rsize_;
  }

  //rtpmap
  if(!rtp_maps_.empty()) {
    JSON_TYPE rtpmaps = JSON_ARRAY;
    JSON_TYPE fbs = JSON_ARRAY;
    JSON_TYPE fmtps = JSON_ARRAY;
    
    for(size_t i = 0; i < rtp_maps_.size(); ++i){
      JSON_TYPE rtpmap = JSON_OBJECT;
      rtp_maps_[i].encode_rtp(rtpmap);
      rtpmaps.push_back(rtpmap);

      if(!rtp_maps_[i].rtcp_fb_.empty()){
        rtp_maps_[i].encode_fb(fbs);
      }

      if(!rtp_maps_[i].fmtp_.empty()){
        JSON_TYPE fmtp = JSON_OBJECT;
        rtp_maps_[i].encode_fmtp(fmtp);
        fmtps.push_back(fmtp);
      }
    }

    media["rtp"] = rtpmaps;
    
    if(!fbs.empty())
      media["rtcpFb"] = fbs;

    if(!fmtps.empty())
      media["fmtp"] = fmtps;
  }

  if(!ssrc_infos_.empty()){
    JSON_TYPE ssrc_infos = JSON_ARRAY;
    for(size_t i = 0; i < ssrc_infos_.size(); ++i){
      ssrc_infos_[i].encode(ssrc_infos);
    }
    media["ssrcs"] = ssrc_infos;
  }

  if(!ssrc_groups_.empty()){
    JSON_TYPE ssrc_groups = JSON_ARRAY;
    for(size_t i = 0; i < ssrc_groups_.size(); ++i){
      JSON_TYPE item = JSON_OBJECT;
      ssrc_groups_[i].encode(item);
      ssrc_groups.push_back(item);
    }
    media["ssrcGroups"] = ssrc_groups;
  }
}

void MediaDesc::buildSettingFromExtmap(TrackSetting& settings) {
  // Video ulpfec red transport-cc
  std::for_each(rtp_maps_.begin(), 
                rtp_maps_.end(), 
                [this, &settings](rtpmap& item){          
    if (!strcasecmp(item.encoding_name_.data(), "red")) {
      settings.red = item.payload_type_;
    } else if (!strcasecmp(item.encoding_name_.data(), "ulpfec")) {
      settings.ulpfec = item.payload_type_;
    }
    for (auto& x : item.rtcp_fb_) {
      if (x == "transport-cc") {
        for (auto& j : extmaps_) {
          if(j.second == extMappings[EExtmap::TransportCC]){
            settings.transportcc = j.first;
            break;
          }
        }
        break;
      }
    }
  });
}

TrackSetting MediaDesc::getTrackSettings() {
  TrackSetting settings;
  if (type_ == "audio") {
    settings.is_audio = true;
    // Audio ssrc
    if(!ssrc_infos_.empty() && ssrc_infos_[0].ssrc_) {
      settings.ssrcs.push_back(ssrc_infos_[0].ssrc_);
    }

    auto found = std::find_if(extmaps_.begin(), extmaps_.end(),[](auto& item) {
      return item.second == extMappings[EExtmap::TransportCC];
    });
    if (found != extmaps_.end()) {
      settings.transportcc = found->first;
    }
  } else if(type_ == "video") {
    // Video ssrcs
    // for publishers ssrc_groups_ is empty, ssrc_infos_ can't be empty
    // for subscribers both are empty
    if (!ssrc_groups_.empty()) {
      LOG_ASSERT(ssrc_groups_.size() == 1);
      if ( ssrc_groups_[0].semantic_ == "FID" ) {
        LOG_ASSERT(ssrc_groups_[0].ssrcs_.size() == 2);
        settings.ssrcs = ssrc_groups_[0].ssrcs_;
      
        buildSettingFromExtmap(settings);
        
      } else if(ssrc_groups_[0].semantic_ == "MID") {
        // TODO
      }
    } else {
      for (auto& x : ssrc_infos_) {
        if (x.ssrc_)
          settings.ssrcs.push_back(x.ssrc_);
      }
      buildSettingFromExtmap(settings);
    }
  }

  settings.rtcp_rsize = rtcp_rsize_.empty() ? false : true;

  settings.format = get_codec_by_preference(preference_codec_);
  ELOG_INFO("settings.format:%d", settings.format);

#ifdef WA_ENABLE_SDESMID
  for(auto& i : extmaps_){
    if(i.second == extMappings[EExtmap::SdesMid]){
      settings.mid = mid_;
      settings.mid_ext = i.first;
    }
  }
#endif

  return settings;
}

int32_t MediaDesc::filterMediaPayload(const FormatPreference& prefer_type) {
  int32_t type = 0;
  for(auto& item : rtp_maps_){
    if (isAudio()) {
      if(get_preference_name(prefer_type.format_) == item.encoding_name_) {
        type = item.payload_type_;
      }
    } else if (isVideo()) {
      if (get_preference_name(prefer_type.format_) == "H264") {
        type = filer_h264(rtp_maps_, prefer_type.profile_);
      } else if (get_preference_name(prefer_type.format_) == "VP9") {
        //TODO not implement
      }
    }
  }
  return type;
}

std::string MediaDesc::setSsrcs(const std::vector<uint32_t>& ssrcs, 
                                const std::string& inmsid) {
  std::string msid{inmsid};
  if (msid.empty()) {
    // Generate msid
    const std::string alphanum = "0123456789" \
                                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                                 "abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<int> dist {0, int(alphanum.length()-1)};
    std::random_device rd;
    std::default_random_engine rng {rd()};

    constexpr int msidLength = 10;
    for(int i = 0; i < msidLength; ++i){
      msid += alphanum[dist(rng)];
    }
  }

  std::string mtype = (type_ == "audio") ? "a" : "v";
  // Only support one ssrc now
  uint32_t ssrc = ssrcs[0];
  ssrc_infos_.resize(1);

  /*
   * media.ssrcs = [
   *   {id: ssrc, attribute: 'cname', value: },
   *   {id: ssrc, attribute: 'msid', value: `${msid} ${mtype}0`},
   *   {id: ssrc, attribute: 'mslabel', value: msid},
   *   {id: ssrc, attribute: 'label', value: `${mtype}0`},
   * ]
   */
  
  ssrc_infos_[0].ssrc_ = ssrc;
  ssrc_infos_[0].cname_ = "o/i14u9pJrxRKAsu";
  ssrc_infos_[0].mslabel_ = msid;
  ssrc_infos_[0].label_ = mtype + "0";
  ssrc_infos_[0].msid_ = msid + " " + ssrc_infos_[0].label_;

  JSON_TYPE jarray = JSON_ARRAY;
  ssrc_infos_[0].encode(jarray);
  JSON_TYPE jobj = JSON_OBJECT;
  jobj["ssrcs"] = jarray;
  std::ostringstream oss;
  oss << jobj;
  std::string log = oss.str();
  ELOG_INFO("Set SSRC mid:%s, msg:%s", mid_.c_str(), log.c_str());
  return msid;
}

bool MediaDesc::filterByPayload(int32_t payload,
                                bool disable_red,
                                bool disable_rtx,
                                bool disable_ulpfec) {
  bool ret = false;
  
  auto found = std::find_if(rtp_maps_.begin(), rtp_maps_.end(), 
      [payload](auto& i) {
        return payload == i.payload_type_;
  });

  if(found == rtp_maps_.end()) {
    return ret;
  }

  std::vector<rtpmap> new_rtp_maps;
  new_rtp_maps.emplace_back(std::move(*found));
  rtpmap& map = new_rtp_maps[0];

  preference_codec_ = map.encoding_name_;
  
  ret = true;
  
  if (!map.related_.empty()) {
    auto& related = map.related_;
    related.erase(std::remove_if(related.begin(), related.end(), 
        [disable_red, disable_rtx, disable_ulpfec](auto& i)->bool {
      return ((disable_red && i.encoding_name_ == "red") ||
              (disable_rtx && i.encoding_name_ == "rtx") ||
              (disable_ulpfec && i.encoding_name_ == "ulpfec"))? true : false;
    }), related.end());
  }
  
  //remove recive side cc
  auto& rtcp_fb = map.rtcp_fb_;
  if (!rtcp_fb.empty())
    rtcp_fb.erase(std::remove(rtcp_fb.begin(), rtcp_fb.end(), "goog-remb"));

  std::ostringstream oss;
  oss << map.payload_type_;

  // build payload type relation ship, eg fmtp rtx
  for(size_t i = 0; i < map.related_.size(); ++i) {
    oss << " ";
    oss << map.related_[i].payload_type_;
    new_rtp_maps.emplace_back(map.related_[i]);
    if (!map.related_[i].related_.empty()) {
      LOG_ASSERT("rtx" == map.related_[i].related_[0].encoding_name_ &&
          map.related_[i].related_.size() == 1);
      
      oss << " ";
      oss << map.related_[i].related_[0].payload_type_;
      new_rtp_maps.emplace_back(map.related_[i].related_[0]);
    }
  }

  payloads_ = oss.str();

  
  std::swap(new_rtp_maps, rtp_maps_);

  return ret;
}

void MediaDesc::filterExtmap() {
  for(auto x = extmaps_.begin(); x != extmaps_.end();) {
    size_t i = 0;
    size_t ext_map_size = extMappings.size();
    for(; i < ext_map_size; ++i) {
      if(x->second == extMappings[i]) {
        break;
      }
    }

    if(i >= ext_map_size) {
      //not found
      extmaps_.erase(x++);
    }else{
      // found
      ++x;
    }
  }

  if (disable_audio_gcc_ && type_ == "audio") {
    extmaps_.erase(std::find_if(extmaps_.begin(), extmaps_.end(), [](auto& item) {
        return item.second.param == extMappings[EExtmap::TransportCC];
      }));
  }
}

void MediaDesc::clearSsrcInfo() {

  msid_.clear();
  ssrc_groups_.clear();
  ssrc_infos_.clear();
}

///////////////////////////////////////////////////////////////////////////////
// WaSdpInfo
///////////////////////////////////////////////////////////////////////////////
WaSdpInfo::WaSdpInfo() = default;

WaSdpInfo::WaSdpInfo(const std::string& strSdp) {
  init(strSdp);
}

int WaSdpInfo::init(const std::string& strSdp) {
  if(!media_descs_.empty()) {
    return wa_e_already_initialized;
  }

  if(strSdp.empty()) {
    return wa_e_invalid_param;
  }

  auto session = sdptransform::parse(strSdp);
  
  if(session.find("media") == session.end()) {
    return wa_e_parse_offer_failed;
  }

  //version_ = session.at("version");  parser bug need fix

  // origin
  username_ = session.at("origin").at("username");
  session_id_ = session.at("origin").at("sessionId");
  session_version_ = session.at("origin").at("sessionVersion");
  nettype_ = session.at("origin").at("netType");
  addrtype_ = session.at("origin").at("ipVer");
  unicast_address_ = session.at("origin").at("address");

  // session_name
  session_name_ = session.at("name");

  // timing
  start_time_ = session.at("timing").at("start");
  end_time_ = session.at("timing").at("stop");

  if(session.find("icelite") != session.end()) {
    ice_lite_ = true;
  }

  //SessionInfo
  session_info_.decode(session);

  //groups
  assert(session.at("groups").size() == 1);
  group_policy_ = session.at("groups")[0].at("type");
  {
    std::string tmp = session.at("groups")[0].at("mids");
    std::istringstream is(tmp);
    std::string word;
    while (is >> word) {
      groups_.push_back(word);
    }
  }

  if (enable_extmapAllowMixed_) {
    auto extmapAllowMixed_found = session.find("extmapAllowMixed");
    if(extmapAllowMixed_found != session.end()){
      extmapAllowMixed_ = *extmapAllowMixed_found;
    }
  }
  
  //msid_semantic
  auto msidSemantic = session.find("msidSemantic");
  
  if(msidSemantic != session.end()){
    msid_semantic_ = (*msidSemantic).at("semantic");
    auto token_found = (*msidSemantic).find("token");
    if(token_found != (*msidSemantic).end()){
      std::string tmp = *token_found;
      std::istringstream is(tmp);
      std::string word;
      while(is >> word) {
        token_.push_back(word);
      }
    }
  }
  
  // m-line, media sessions
  auto m_found = session.find("media");
  if(m_found == session.end()){
    return wa_e_parse_offer_failed;
  }
  
  auto& m_line = *m_found;
  for(size_t i = 0; i < m_line.size(); ++i) {
    media_descs_.emplace_back(session_info_);
    media_descs_[i].parse(m_line[i]);
  }
  
  return wa_ok;
}

std::string WaSdpInfo::toString(const std::string& strMid) {   
  if(media_descs_.empty()){
    return "";
  }

  if(!strMid.empty()){
    bool found = false;
    for(size_t i = 0; i < media_descs_.size(); ++i){
      if(media_descs_[i].mid_ == strMid){
        found = true;
      }
    }

    if(!found){
      return "";
    }
  }

  JSON_TYPE session = JSON_OBJECT;

  // version "v="
  session["version"] = version_;

  // origin "o="
  JSON_TYPE org = JSON_OBJECT;
  org["username"] = username_;
  org["sessionId"] = session_id_;
  org["sessionVersion"] = session_version_;
  org["netType"] = nettype_;
  org["ipVer"] = addrtype_;
  org["address"] = unicast_address_;
  session["origin"] = org;

  // session_name  "s="
  session["name"] = session_name_;

  // timing "t="
  JSON_TYPE time = JSON_OBJECT;
  time["start"] = start_time_;
  time["stop"] = end_time_;
  session["timing"] = time;

  if(ice_lite_){
    session["icelite"] = "ice-lite";
  }

  //session_info_.encode(session);

  assert(!groups_.empty());
  //group bundle
  {
    std::string group_mids{""};
    for(auto itor=groups_.begin(); itor!=groups_.end();){
      group_mids += *itor++;
      if(itor != groups_.end()){
        group_mids += " ";
      }
    }

    JSON_TYPE groups = JSON_ARRAY;
    JSON_TYPE group = JSON_OBJECT;
    group["type"] = group_policy_;
    group["mids"] = group_mids;
    groups.push_back(group);
    session["groups"] = groups;
  }
  
  if(!extmapAllowMixed_.empty())
    session["extmapAllowMixed"] = extmapAllowMixed_;

  //msidSemantic
  if(!msid_semantic_.empty()){
    JSON_TYPE msid_semantic = JSON_OBJECT;
    
    if(!token_.empty()){
      std::string token{""};
      for(auto itor=token_.begin(); itor!=token_.end();){
        token += *itor++;
        if(itor != token_.end()){
          token += " ";
        }
      }
      msid_semantic["token"] = token;
    }
    msid_semantic["semantic"] = msid_semantic_;
    
    session["msidSemantic"] = msid_semantic;
  }
  
  JSON_TYPE media = JSON_ARRAY;

  for(size_t i=0; i<media_descs_.size(); ++i){
    JSON_TYPE m = JSON_OBJECT;
    if(!strMid.empty()){
      if(strMid == media_descs_[i].mid_){
        media_descs_[i].encode(m);
        media.push_back(m);
      }
    }
    else{
      media_descs_[i].encode(m);
      media.push_back(m);
    }
  }

  //link up
  session["media"] = media;

  return sdptransform::write(session);
}

WaSdpInfo* WaSdpInfo::answer() {
  WaSdpInfo* answer = new WaSdpInfo(this->toString());

  answer->username_ = "-";
  answer->session_id_ = 0;
  answer->session_version_ = 0;
  answer->nettype_ = "IN";
  answer->addrtype_ = 4;
  answer->unicast_address_ = "127.0.0.1";

  // set session name
  std::ostringstream oss;
  oss << __WA_STACK_NAME__;
  oss << "/";
  oss << __WA_MAJOR_VERSION__;
  oss << ".";
  oss << __WA_MINOR_VERSION__;
  answer->session_name_ = oss.str();

  session_info_.ice_options_.clear();
  if(session_info_.setup_ == "active" || session_info_.setup_ == "actpass") {
    answer->session_info_.setup_ = "passive";
  } else {
    answer->session_info_.setup_ = "active";
  }

  std::for_each(answer->media_descs_.begin(), answer->media_descs_.end(), 
        [](MediaDesc& mediaInfo) {
    mediaInfo.port_ = 1;

    mediaInfo.rtcp_rsize_.clear();
    
    if(mediaInfo.direction_ == "recvonly"){
      mediaInfo.direction_ = "sendonly";
    }
    else if(mediaInfo.direction_ == "sendonly"){
      mediaInfo.direction_ = "recvonly";
    }

    // TOD simulcast
  });

  return answer;
}

std::string WaSdpInfo::mediaType(const std::string& mid) {
 for(auto& item : media_descs_){
    if(item.mid_ == mid){
      return item.type_;
    }
  }
  return "";
}

std::string WaSdpInfo::mediaDirection(const std::string& mid) { 
  for(auto& item : media_descs_){
    if(item.mid_ == mid){
      return item.direction_;
    }
  }
  return "";
}

int32_t WaSdpInfo::filterMediaPayload(const std::string& mid, 
                                      const FormatPreference& prefer_type) {
  int32_t type = 0;
  for(auto& item : media_descs_){
    if(item.mid_ == mid){
      return item.filterMediaPayload(prefer_type);
    }
  }
  return type;
}

bool WaSdpInfo::filterByPayload(const std::string& mid, 
                                int32_t payload,
                                bool disable_red/* = false*/,
                                bool disable_rtx/* = false*/,
                                bool disable_ulpfec/* = false*/) {
  for(auto& item : media_descs_){
    if(item.mid_ != mid)
      continue;
    
    return item.filterByPayload(
        payload, disable_red, disable_rtx, disable_ulpfec);
  }

  return false;
}

void WaSdpInfo::filterExtmap() {
  for(auto& i : media_descs_){
    i.filterExtmap();
  }
}

int32_t WaSdpInfo::getMediaPort(const std::string& mid) {
  for(auto& item : media_descs_){
    if(item.mid_ == mid){
      return item.port_;
    }
  }
  return 0;
}

bool WaSdpInfo::setMediaPort(const std::string& mid, int32_t port) {
  for(auto& item : media_descs_){
    if(item.mid_ == mid){
      item.port_ = port;
      return true;
    }
  }
  
  return false;
}

std::string WaSdpInfo::singleMediaSdp(const std::string& mid) {
  return toString(mid);
}

void WaSdpInfo::SetToken(const std::string& str) {
  token_.clear();
  token_.push_back(str);
}

void WaSdpInfo::setCredentials(const WaSdpInfo& sdpInfo) {
  const SessionInfo* pSessionInfo = nullptr;
  //if(sdpInfo.session_in_medias_){
  //  LOG_ASSERT(false == sdpInfo.media_descs_.empty());
  //  pSessionInfo = &(sdpInfo.media_descs_[0].session_info_);
  //}
  //else{
    pSessionInfo = &(sdpInfo.session_info_);
  //}

  //if(!session_in_medias_){
    session_info_ = *pSessionInfo;
  //  return;
  //}

  //for(auto& i : media_descs_){
  //  i.session_info_ = *pSessionInfo;
  //}
}

void WaSdpInfo::setCandidates(const WaSdpInfo& sdpInfo) {
  LOG_ASSERT(!sdpInfo.media_descs_.empty());
  for(auto& i : media_descs_){
    i.candidates_ = sdpInfo.media_descs_[0].candidates_;
  }
}

TrackSetting WaSdpInfo::getTrackSettings(const std::string& mid) {
  auto found = std::find_if(media_descs_.begin(), media_descs_.end(), 
      [mid](auto& item) {
    return item.mid_ == mid;
  });

  if (found != media_descs_.end()) {
    return (*found).getTrackSettings();
  }

  return TrackSetting();
}

} //namespace wa

