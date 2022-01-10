#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>
#include <exception>
#include <memory>

#include "gmock/gmock.h"

#include "../3rd/libsdptransform/include/sdptransform.hpp"
#include "h/rtc_stack_api.h"
#include "h/rtc_return_value.h"
#include "wa/sdp_processor.h"
#include "wa/helper.h"

static std::string chrome_sdp{"../../../src/test/data/chrome_91.sdp"};
static std::string chrome_sdp88{"../../../src/test/data/chrome_88.sdp"};
static std::string chrome_answer{"../../../src/test/data/answer.sdp"};
static std::string firefox_sdp{"../../../src/test/data/Firefox.sdp"};

//common
TEST(WaSdpInfo, init_failed) {
  wa::WaSdpInfo sdpinfo;
  int result = sdpinfo.init("");
  EXPECT_EQ(result, wa::wa_e_invalid_param);
}

//chrome
TEST(WaSdpInfo, chrome_init_88) {
  std::ifstream fin(chrome_sdp88);

  ASSERT_EQ(true, fin.is_open());

  std::string sdp;
  int result = wa::wa_failed;
  
  wa::WaSdpInfo sdpinfo;
  while(std::getline(fin, sdp)){
    sdp = wa::wa_string_replace(sdp, "\\r\\n", "\r\n");
    try{
      result = sdpinfo.init(sdp);
      std::cout << "sdpinfo.init_88 succeed." << std::endl;
    }catch(std::exception& ex){
      result = wa::wa_e_parse_offer_failed;
      std::cout << "exception catched :" << ex.what() << std::endl;
    }
  }
  ASSERT_EQ(result, wa::wa_ok);
  
  std::string sdpOut = sdpinfo.toString();

  wa::WaSdpInfo out_sdp_info;
  try{
    result = out_sdp_info.init(sdpOut);
  }catch(std::exception& ex){
    result = wa::wa_e_parse_offer_failed;
    std::cout << "write case: exception catched :" << ex.what() << std::endl;
  }
  ASSERT_EQ(result, wa::wa_ok);
  //sdp_info_comp(out_sdp_info);
}

TEST(WaSdpInfo, chrome_candidate2string) {
  std::ifstream fin(chrome_answer);
  ASSERT_EQ(true, fin.is_open());
  
  fin.seekg(0, std::ios::end);
  int length = fin.tellg();
  fin.seekg(0, std::ios::beg);
  char *buffer = new char[length];
  fin.read(buffer, length);
 
  std::string sdp(buffer);
  delete buffer;
 
  int result = wa::wa_failed;
  wa::WaSdpInfo sdpinfo;
  try{
    result = sdpinfo.init(sdp);
  }catch(std::exception& ex){
    result = wa::wa_e_parse_offer_failed;
    std::cout << "init_answer case: exception catched :" << ex.what() << std::endl;
  }

  ASSERT_EQ(result, wa::wa_ok);

#define JSON_OBJECT nlohmann::json::object()
#define JSON_ARRAY nlohmann::json::array()

  //candidate
  JSON_TYPE jcans = JSON_ARRAY;
  auto& candidates = sdpinfo.media_descs_[0].candidates_;
  for(size_t i = 0; i < candidates.size(); ++i){
    JSON_TYPE jcan = JSON_OBJECT;
    candidates[i].encode(jcan);
    jcans.push_back(jcan);
  }
  JSON_TYPE jmedia = JSON_OBJECT;
  jmedia["candidates"] = jcans;

  std::string strCandidates = sdptransform::write(jmedia);

  EXPECT_EQ(strCandidates, "v=0\r\ns=-\r\na=candidate:1 1 udp 2013266431 192.168.1.156 46462 typ host\r\n");
}

TEST(WaSdpInfo, chrom_answer_91) {
  std::ifstream fin(chrome_answer);
  ASSERT_EQ(true, fin.is_open());
  
  fin.seekg(0, std::ios::end);
  int length = fin.tellg();
  fin.seekg(0, std::ios::beg);
  char *buffer = new char[length];
  fin.read(buffer, length);
 
  std::string sdp(buffer);
  delete buffer;
 
  int result = wa::wa_failed;
  wa::WaSdpInfo sdpinfo;
  try{
    result = sdpinfo.init(sdp);
  }catch(std::exception& ex){
    result = wa::wa_e_parse_offer_failed;
    std::cout << "init_answer case: exception catched :" << ex.what() << std::endl;
  }

  ASSERT_EQ(result, wa::wa_ok);

  EXPECT_EQ(sdpinfo.version_, 0);
  EXPECT_EQ(sdpinfo.username_, "-");
  EXPECT_EQ(sdpinfo.session_id_, (uint64_t)0);
  EXPECT_EQ(sdpinfo.session_version_, (uint32_t)0);
  EXPECT_EQ(sdpinfo.nettype_, "IN");
  EXPECT_EQ(sdpinfo.addrtype_, 4);
  EXPECT_EQ(sdpinfo.unicast_address_, "127.0.0.1");

  EXPECT_EQ(sdpinfo.session_name_, "IntelWebRTCMCU");
  EXPECT_EQ(sdpinfo.start_time_, 0);
  EXPECT_EQ(sdpinfo.end_time_, 0);

  //EXPECT_TRUE(sdpinfo.session_in_medias_);

  EXPECT_EQ(sdpinfo.group_policy_, "BUNDLE");
  EXPECT_EQ(sdpinfo.groups_[0], "0");
  EXPECT_EQ(sdpinfo.groups_[1], "1");
  EXPECT_EQ(sdpinfo.groups_[2], "0");
  EXPECT_EQ(sdpinfo.groups_[3], "1");

  EXPECT_EQ(sdpinfo.extmapAllowMixed_, "");

  EXPECT_EQ(sdpinfo.msid_semantic_, "WMS");
  EXPECT_EQ(sdpinfo.msids_[0], "UxETroWmnM");

  //audio
  EXPECT_EQ(sdpinfo.media_descs_[0].type_, "audio");
  EXPECT_EQ(sdpinfo.media_descs_[0].port_, 1);
  EXPECT_EQ(sdpinfo.media_descs_[0].numPorts_, 0);
  //EXPECT_EQ(sdpinfo.media_descs_[0].rtcport_, 1);
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].foundation_, "1");
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].component_, 1);
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].transport_type_, "udp");
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].priority_, 2013266431);
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].ip_, "192.168.1.156");
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].port_, 46462);
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].type_, "host");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].protocols_, "UDP/TLS/RTP/SAVPF");
  EXPECT_EQ(sdpinfo.media_descs_[0].payloads_, "111");

  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].transport_type_, "udp");
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].ip_, "192.168.1.156");
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].port_, 46462);
  EXPECT_EQ(sdpinfo.media_descs_[0].candidates_[0].type_, "host");

  wa::SessionInfo& session_info = sdpinfo.media_descs_[0].session_info_;
  EXPECT_EQ(session_info.ice_ufrag_, "Y5L0");
  EXPECT_EQ(session_info.ice_pwd_, "KROGgN0MlCcdO30NJbzT9v");
  EXPECT_EQ(session_info.ice_options_, "");
  EXPECT_EQ(session_info.fingerprint_algo_, "sha-256");
  const std::string fingerprint = 
    "98:64:20:F6:CE:7A:D9:AF:73:B1:0D:8A:24:AB:32:8B:3C:17:F4:EC:8E:AC:62:51:F1:5A:93:65:D2:1C:95:5C";
  EXPECT_EQ(session_info.fingerprint_, fingerprint);
  EXPECT_EQ(session_info.setup_, "active");

  EXPECT_EQ(sdpinfo.media_descs_[0].mid_, "0");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[1], "urn:ietf:params:rtp-hdrext:ssrc-audio-level");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[2], "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[3], "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[4], "urn:ietf:params:rtp-hdrext:sdes:mid");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[5], "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[6], "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[0].direction_, "recvonly");
  EXPECT_EQ(sdpinfo.media_descs_[0].msid_, "");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_mux_, "rtcp-mux");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_.size(), (size_t)1);  
 
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].payload_type_, 111);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].encoding_name_, "opus");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].clock_rate_, 48000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].encoding_param_, "2");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[0].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].fmtp_, "minptime=10;useinbandfec=1");
  EXPECT_TRUE(sdpinfo.media_descs_[0].ssrc_groups_.empty());
  
  //video
  EXPECT_EQ(sdpinfo.media_descs_[1].type_, "video");
  EXPECT_EQ(sdpinfo.media_descs_[1].port_, 1);
  EXPECT_EQ(sdpinfo.media_descs_[1].numPorts_, 0);
  //EXPECT_EQ(sdpinfo.media_descs_[1].rtcport_, 1);

  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].foundation_, "1");
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].component_, 1);
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].transport_type_, "udp");
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].priority_, 2013266431);
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].ip_, "192.168.1.156");
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].port_, 46462);
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].type_, "host");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].protocols_, "UDP/TLS/RTP/SAVPF");
  EXPECT_EQ(sdpinfo.media_descs_[1].payloads_, 
    "97 99 101 102 120 127 119 125 107 108 109 36 124 118 123");

  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].transport_type_, "udp");
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].ip_, "192.168.1.156");
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].port_, 46462);
  EXPECT_EQ(sdpinfo.media_descs_[1].candidates_[0].type_, "host");

  wa::SessionInfo& session_info1 = sdpinfo.media_descs_[1].session_info_;
  EXPECT_EQ(session_info1.ice_ufrag_, "Y5L0");
  EXPECT_EQ(session_info1.ice_pwd_, "KROGgN0MlCcdO30NJbzT9v");
  EXPECT_EQ(session_info1.ice_options_, "");
  EXPECT_EQ(session_info1.fingerprint_algo_, "sha-256");
  EXPECT_EQ(session_info1.fingerprint_, fingerprint);
  EXPECT_EQ(session_info1.setup_, "active");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].mid_, "1");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[2], "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[3], "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[4], "urn:ietf:params:rtp-hdrext:sdes:mid");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[5], "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[6], "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[14], "urn:ietf:params:rtp-hdrext:toffset");

  EXPECT_EQ(sdpinfo.media_descs_[1].direction_, "recvonly");
  EXPECT_EQ(sdpinfo.media_descs_[1].msid_, "");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtcp_mux_, "rtcp-mux");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtcp_rsize_, "");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].payload_type_, 97);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].fmtp_, "apt=96");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].payload_type_, 99);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[1].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].fmtp_, "apt=98");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].payload_type_, 101);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].clock_rate_, 90000);
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].fmtp_, "apt=100");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].payload_type_, 102);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].encoding_param_, "");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].rtcp_fb_[0], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].rtcp_fb_[1], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].rtcp_fb_[2], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].fmtp_, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].payload_type_, 120);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].clock_rate_, 90000);
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].fmtp_, "apt=102");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].payload_type_, 127);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].rtcp_fb_[0], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].rtcp_fb_[1], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].rtcp_fb_[2], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].payload_type_, 119);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].fmtp_, "apt=127");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].payload_type_, 125);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].rtcp_fb_[0], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].rtcp_fb_[1], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].rtcp_fb_[2], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].payload_type_, 107);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[8].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].fmtp_, "apt=125");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].payload_type_, 108);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].rtcp_fb_[0], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].rtcp_fb_[1], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].rtcp_fb_[2], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].payload_type_, 109);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].clock_rate_, 90000);
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[10].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].fmtp_, "apt=108");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].payload_type_, 36);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[11].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].fmtp_, "apt=35");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].payload_type_, 124);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].encoding_name_, "red");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[12].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].payload_type_, 118);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[13].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].fmtp_, "apt=124");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].payload_type_, 123);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].encoding_name_, "ulpfec");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[14].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].fmtp_, "");

  EXPECT_TRUE(sdpinfo.media_descs_[1].ssrc_groups_.empty());

  //std::string toAnswer = sdpinfo.toString("");
  //std::cout << toAnswer << std::endl;
}

void session_info_comp(wa::SessionInfo& session_info) {
  EXPECT_EQ(session_info.ice_ufrag_, "Hcw5");
  EXPECT_EQ(session_info.ice_pwd_, "bHaWawVL0Jt/7Tq9+BX0EKJJ");
  EXPECT_EQ(session_info.ice_options_, "trickle");
  EXPECT_EQ(session_info.fingerprint_algo_, "sha-256");
  const std::string fingerprint = 
    "D8:87:EE:11:8B:17:05:55:57:79:30:92:3B:D4:F4:55:0A:E4:18:64:7F:4E:BC:BB:85:1F:88:67:B6:A9:61:1D";
  EXPECT_EQ(session_info.fingerprint_, fingerprint);
  EXPECT_EQ(session_info.setup_, "actpass");
}

void sdp_info_comp(wa::WaSdpInfo& sdpinfo) {
  EXPECT_EQ(sdpinfo.version_, 0);
  EXPECT_EQ(sdpinfo.username_, "-");
  EXPECT_EQ(sdpinfo.session_id_, 1701150822055760335);
  EXPECT_EQ(sdpinfo.session_version_, (uint32_t)2);
  EXPECT_EQ(sdpinfo.nettype_, "IN");
  EXPECT_EQ(sdpinfo.addrtype_, 4);
  EXPECT_EQ(sdpinfo.unicast_address_, "127.0.0.1");

  EXPECT_EQ(sdpinfo.session_name_, "-");
  EXPECT_EQ(sdpinfo.start_time_, 0);
  EXPECT_EQ(sdpinfo.end_time_, 0);

  //EXPECT_TRUE(sdpinfo.session_in_medias_);

  EXPECT_EQ(sdpinfo.group_policy_, "BUNDLE");

  EXPECT_EQ(sdpinfo.groups_[0], "0");
  EXPECT_EQ(sdpinfo.groups_[1], "1");

  if (sdpinfo.enable_extmapAllowMixed_) {
    EXPECT_EQ(sdpinfo.extmapAllowMixed_, "extmap-allow-mixed");
  }

  EXPECT_EQ(sdpinfo.msid_semantic_, "WMS");
  
  EXPECT_TRUE(sdpinfo.msids_.empty());

  //audio
  EXPECT_EQ(sdpinfo.media_descs_[0].type_, "audio");
  EXPECT_EQ(sdpinfo.media_descs_[0].port_, 9);
  EXPECT_EQ(sdpinfo.media_descs_[0].numPorts_, 0);
  EXPECT_EQ(sdpinfo.media_descs_[0].protocols_, "UDP/TLS/RTP/SAVPF");
  EXPECT_EQ(sdpinfo.media_descs_[0].payloads_, "111 103 104 9 0 8 106 105 13 110 112 113 126");

  EXPECT_TRUE(sdpinfo.media_descs_[0].candidates_.empty());

  wa::SessionInfo& session_info = sdpinfo.media_descs_[0].session_info_;
  session_info_comp(session_info);
  EXPECT_EQ(sdpinfo.media_descs_[0].mid_, "0");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[1], "urn:ietf:params:rtp-hdrext:ssrc-audio-level");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[2], "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[3], "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[4], "urn:ietf:params:rtp-hdrext:sdes:mid");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[5], "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[6], "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[0].direction_, "sendonly");
  EXPECT_EQ(sdpinfo.media_descs_[0].msid_, "- a9a03372-8648-4d55-8373-55a6e4916cb9");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_mux_, "rtcp-mux");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_.size(), (size_t)13);  
 
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].payload_type_, 111);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].encoding_name_, "opus");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].clock_rate_, 48000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].encoding_param_, "2");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].rtcp_fb_[0], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].fmtp_, "minptime=10;useinbandfec=1");
 
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].payload_type_, 103);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].encoding_name_, "ISAC");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].clock_rate_, 16000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[1].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].payload_type_, 104);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].encoding_name_, "ISAC");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].clock_rate_, 32000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[2].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].fmtp_, "");


  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].payload_type_, 9);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].encoding_name_, "G722");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[3].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].payload_type_, 0);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].encoding_name_, "PCMU");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[4].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[5].payload_type_, 8);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[5].encoding_name_, "PCMA");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[5].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[5].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[5].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[5].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[6].payload_type_, 106);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[6].encoding_name_, "CN");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[6].clock_rate_, 32000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[6].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[6].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[6].fmtp_, ""); 
  
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[7].payload_type_, 105);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[7].encoding_name_, "CN");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[7].clock_rate_, 16000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[7].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[7].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[7].fmtp_, ""); 

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[8].payload_type_, 13);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[8].encoding_name_, "CN");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[8].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[8].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[8].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[8].fmtp_, ""); 


  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[9].payload_type_, 110);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[9].encoding_name_, "telephone-event");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[9].clock_rate_, 48000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[9].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[9].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[9].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[10].payload_type_, 112);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[10].encoding_name_, "telephone-event");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[10].clock_rate_, 32000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[10].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[10].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[10].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[11].payload_type_, 113);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[11].encoding_name_, "telephone-event");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[11].clock_rate_, 16000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[11].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[11].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[11].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[12].payload_type_, 126);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[12].encoding_name_, "telephone-event");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[12].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[12].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[12].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[12].fmtp_, "");

  
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].ssrc_, (uint32_t)327982930);
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].cname_, "2wMlnw7SShxbrvaO");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].msid_, "-");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].msid_tracker_, "a9a03372-8648-4d55-8373-55a6e4916cb9");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].mslabel_, "-");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].label_, "a9a03372-8648-4d55-8373-55a6e4916cb9");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_mux_, "rtcp-mux");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_rsize_, "");
  
  //video
  EXPECT_EQ(sdpinfo.media_descs_[1].type_, "video");
  EXPECT_EQ(sdpinfo.media_descs_[1].port_, 9);
  EXPECT_EQ(sdpinfo.media_descs_[1].numPorts_, 0);
  EXPECT_EQ(sdpinfo.media_descs_[1].protocols_, "UDP/TLS/RTP/SAVPF");
  EXPECT_EQ(sdpinfo.media_descs_[1].payloads_, 
    "96 97 98 99 100 101 102 120 127 119 125 107 108 109 35 36 124 118 123");
  
  EXPECT_TRUE(sdpinfo.media_descs_[1].candidates_.empty());
  
  wa::SessionInfo& session_info1 = sdpinfo.media_descs_[1].session_info_;
  session_info_comp(session_info1);
  EXPECT_EQ(sdpinfo.media_descs_[1].mid_, "1");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[14], "urn:ietf:params:rtp-hdrext:toffset");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[2], "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[13], "urn:3gpp:video-orientation");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[3], "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[12], "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[11], "http://www.webrtc.org/experiments/rtp-hdrext/video-content-type");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[7], "http://www.webrtc.org/experiments/rtp-hdrext/video-timing");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[8], "http://www.webrtc.org/experiments/rtp-hdrext/color-space");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[4], "urn:ietf:params:rtp-hdrext:sdes:mid");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[5], "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[6], "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id");
  EXPECT_EQ(sdpinfo.media_descs_[1].direction_, "sendonly");
  EXPECT_EQ(sdpinfo.media_descs_[1].msid_, "- e54f4e13-3b9c-4aa7-bb61-bc91b01f53f9");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtcp_mux_, "rtcp-mux");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtcp_rsize_, "rtcp-rsize");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].payload_type_, 96);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].encoding_name_, "VP8");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].encoding_param_, "");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].fmtp_, "");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].payload_type_, 97);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[1].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].fmtp_, "apt=96");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].payload_type_, 98);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].encoding_name_, "VP9");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].fmtp_, "profile-id=0");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].payload_type_, 99);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[3].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].fmtp_, "apt=98");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].payload_type_, 100);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].encoding_name_, "VP9");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].fmtp_, "profile-id=2");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].payload_type_, 101);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[5].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].fmtp_, "apt=100");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].payload_type_, 102);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].payload_type_, 120);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[7].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].fmtp_, "apt=102");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].payload_type_, 127);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[8].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].payload_type_, 119);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[9].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[9].fmtp_, "apt=127");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].payload_type_, 125);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[10].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].payload_type_, 107);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[11].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[11].fmtp_, "apt=125");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].payload_type_, 108);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[12].fmtp_, 
    "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].payload_type_, 109);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[13].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[13].fmtp_, "apt=108");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].payload_type_, 35);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].encoding_name_, "AV1X");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].rtcp_fb_[0], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].rtcp_fb_[1], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].rtcp_fb_[3], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].rtcp_fb_[4], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[14].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[15].payload_type_, 36);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[15].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[15].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[15].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[15].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[15].fmtp_, "apt=35");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[16].payload_type_, 124);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[16].encoding_name_, "red");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[16].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[16].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[16].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[16].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[17].payload_type_, 118);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[17].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[17].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[17].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[17].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[17].fmtp_, "apt=124");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[18].payload_type_, 123);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[18].encoding_name_, "ulpfec");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[18].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[18].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[18].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[18].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[1].ssrc_groups_[0].semantic_, "FID");
  EXPECT_EQ(sdpinfo.media_descs_[1].ssrc_groups_[0].ssrcs_[0], 658290066);
  EXPECT_EQ(sdpinfo.media_descs_[1].ssrc_groups_[0].ssrcs_[1], (uint32_t)30849494);
}

int read_sdp_from_string(wa::WaSdpInfo& sdpInfo, const std::string& insdp) {
  std::ifstream fin(insdp);
  assert(true == fin.is_open());
  std::string sdp;
  int result = wa::wa_failed;

  while(std::getline(fin, sdp)){
    sdp = wa::wa_string_replace(sdp, "\\r\\n", "\r\n");
    try{
      result = sdpInfo.init(sdp);
    }catch(std::exception& ex){
      result = wa::wa_e_parse_offer_failed;
      std::cout << "init_ok case: exception catched :" << ex.what() << std::endl;
    }
  }

  return result;
}

TEST(WaSdpInfo, chrome_init_91) {
  wa::WaSdpInfo sdpinfo;
  int result = read_sdp_from_string(sdpinfo, chrome_sdp);
  ASSERT_EQ(result, wa::wa_ok);
  sdp_info_comp(sdpinfo);
}

TEST(WaSdpInfo, chrome_tostring) {
  wa::WaSdpInfo sdpinfo;
  int result = read_sdp_from_string(sdpinfo, chrome_sdp);
  ASSERT_EQ(result, wa::wa_ok);
  
  std::string sdpOut = sdpinfo.toString();

  wa::WaSdpInfo out_sdp_info;
  try{
    result = out_sdp_info.init(sdpOut);
  }catch(std::exception& ex){
    result = wa::wa_e_parse_offer_failed;
    std::cout << "write case: exception catched :" << ex.what() << std::endl;
  }
  ASSERT_EQ(result, wa::wa_ok);
  sdp_info_comp(out_sdp_info);
}

TEST(WaSdpInfo, chrome_filterVideoPayload) {
  wa::WaSdpInfo sdpinfo;
  int result = read_sdp_from_string(sdpinfo, chrome_sdp88);
  ASSERT_EQ(result, wa::wa_ok);
 
  wa::FormatPreference pre;
  pre.format_ = wa::p_h264;
  
  pre.profile_ = "42e01f";
  result = sdpinfo.filterMediaPayload("1", pre);
  ASSERT_EQ(result, 125);

  sdpinfo.filterByPayload("1", result, true, true, true);

  //for (auto& i : sdpinfo.media_descs_[1].rtp_maps_) {
  //  std::cout << i.encoding_name_ << std::endl;
  //}
  ASSERT_EQ(sdpinfo.media_descs_[1].rtp_maps_.size(), (size_t)1);

  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[0].related_.empty());
}

// firefox
void session_info_comp_firefox(wa::SessionInfo& session_info) {
  EXPECT_EQ(session_info.ice_ufrag_, "4036364d");
  EXPECT_EQ(session_info.ice_pwd_, "f472827f28e428ec7340dd5b67a22e4f");
  EXPECT_EQ(session_info.ice_options_, "trickle");
  EXPECT_EQ(session_info.fingerprint_algo_, "sha-256");
  const std::string fingerprint = 
    "CA:89:9C:2E:76:7A:86:59:DF:67:C6:EF:11:04:51:5A:81:31:02:29:F6:99:E5:2A:ED:EB:A5:A8:C5:3B:9F:44";
  EXPECT_EQ(session_info.fingerprint_, fingerprint);
  EXPECT_EQ(session_info.setup_, "actpass");
}

void sdp_info_comp_firefox(wa::WaSdpInfo& sdpinfo) {
  EXPECT_EQ(sdpinfo.version_, 0);
  EXPECT_EQ(sdpinfo.username_, "mozilla...THIS_IS_SDPARTA-95.0.2");
  EXPECT_EQ(sdpinfo.session_id_, 6924795097247936072);
  EXPECT_EQ(sdpinfo.session_version_, 0);
  EXPECT_EQ(sdpinfo.nettype_, "IN");
  EXPECT_EQ(sdpinfo.addrtype_, 4);
  EXPECT_EQ(sdpinfo.unicast_address_, "0.0.0.0");

  EXPECT_EQ(sdpinfo.session_name_, "-");
  EXPECT_EQ(sdpinfo.start_time_, 0);
  EXPECT_EQ(sdpinfo.end_time_, 0);

  //EXPECT_TRUE(!sdpinfo.session_in_medias_);

  session_info_comp_firefox(sdpinfo.session_info_);

  EXPECT_EQ(sdpinfo.group_policy_, "BUNDLE");
  EXPECT_EQ(sdpinfo.groups_[0], "0");
  EXPECT_EQ(sdpinfo.groups_[1], "1");

  if (sdpinfo.enable_extmapAllowMixed_) {
    EXPECT_EQ(sdpinfo.extmapAllowMixed_, "extmap-allow-mixed");
  }
  EXPECT_EQ(sdpinfo.msid_semantic_, "WMS");
  EXPECT_EQ(sdpinfo.msids_[0], "*");

  //audio
  EXPECT_EQ(sdpinfo.media_descs_[0].type_, "audio");
  EXPECT_EQ(sdpinfo.media_descs_[0].port_, 9);
  EXPECT_EQ(sdpinfo.media_descs_[0].numPorts_, 0);
  EXPECT_EQ(sdpinfo.media_descs_[0].protocols_, "UDP/TLS/RTP/SAVPF");
  EXPECT_EQ(sdpinfo.media_descs_[0].payloads_, "109 9 0 8 101");

  EXPECT_TRUE(sdpinfo.media_descs_[0].candidates_.empty());

  wa::SessionInfo& session_info = sdpinfo.media_descs_[0].session_info_;
  session_info_comp_firefox(session_info);
  EXPECT_EQ(sdpinfo.media_descs_[0].mid_, "0");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[1], 
      "urn:ietf:params:rtp-hdrext:ssrc-audio-level");
  EXPECT_FALSE(sdpinfo.media_descs_[0].extmaps_[2] == 
      "urn:ietf:params:rtp-hdrext:csrc-audio-level");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[2].param, 
      "urn:ietf:params:rtp-hdrext:csrc-audio-level");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[2].direction, "recvonly");
  EXPECT_EQ(sdpinfo.media_descs_[0].extmaps_[3], 
      "urn:ietf:params:rtp-hdrext:sdes:mid");
  EXPECT_EQ(sdpinfo.media_descs_[0].direction_, "sendonly");
  EXPECT_EQ(sdpinfo.media_descs_[0].msid_, 
      "- {e8bca8b4-825e-4111-9469-c402d855bdf1}");
  
  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_mux_, "rtcp-mux");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_.size(), 5);  
 
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].payload_type_, 109);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].encoding_name_, "opus");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].clock_rate_, 48000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].encoding_param_, "2");
  
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[0].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[0].fmtp_, 
      "maxplaybackrate=48000;stereo=1;useinbandfec=1");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].payload_type_, 9);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].encoding_name_, "G722");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].encoding_param_, "1");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[1].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[1].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].payload_type_, 0);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].encoding_name_, "PCMU");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[2].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[2].fmtp_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].payload_type_, 8);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].encoding_name_, "PCMA");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[3].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[3].fmtp_, "");

  
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].payload_type_, 101);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].encoding_name_, "telephone-event");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].clock_rate_, 8000);
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].encoding_param_, "1");
  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[4].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[0].rtp_maps_[4].fmtp_, "0-15");

  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].ssrc_, 3383105307);
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].cname_, 
        "{523fef26-b449-4bad-886b-e2b2c890f314}");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].msid_, "");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].msid_tracker_, "");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].mslabel_, "");
  EXPECT_EQ(sdpinfo.media_descs_[0].ssrc_infos_[0].label_, "");

  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_mux_, "rtcp-mux");
  EXPECT_EQ(sdpinfo.media_descs_[0].rtcp_rsize_, "");

  //video
  EXPECT_EQ(sdpinfo.media_descs_[1].type_, "video");
  EXPECT_EQ(sdpinfo.media_descs_[1].port_, 9);
  EXPECT_EQ(sdpinfo.media_descs_[1].numPorts_, 0);
  EXPECT_EQ(sdpinfo.media_descs_[1].protocols_, "UDP/TLS/RTP/SAVPF");
  EXPECT_EQ(sdpinfo.media_descs_[1].payloads_, 
      "120 124 121 125 126 127 97 98");
  
  EXPECT_TRUE(sdpinfo.media_descs_[1].candidates_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].mid_, "1");

  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[3], 
      "urn:ietf:params:rtp-hdrext:sdes:mid");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[4], 
      "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[5], 
      "urn:ietf:params:rtp-hdrext:toffset");
  EXPECT_FALSE(sdpinfo.media_descs_[1].extmaps_[6] ==
      "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[6].param, 
      "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay");
  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[6].direction, "recvonly");

  EXPECT_EQ(sdpinfo.media_descs_[1].extmaps_[7], 
      "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01");

  EXPECT_EQ(sdpinfo.media_descs_[1].direction_, "sendonly");
  EXPECT_EQ(sdpinfo.media_descs_[1].msid_, "- {5a84b14d-aa34-43b2-8b9e-d773738fd3fa}");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtcp_mux_, "rtcp-mux");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtcp_rsize_, "rtcp-rsize");
 
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].payload_type_, 120);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].encoding_name_, "VP8");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].encoding_param_, "");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[0], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[1], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].rtcp_fb_[4], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[0].fmtp_, "max-fs=12288;max-fr=60");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].payload_type_, 124);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[1].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[1].fmtp_, "apt=120");
   
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].payload_type_, 121);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].encoding_name_, "VP9");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[0], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[1], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].rtcp_fb_[4], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[2].fmtp_, "max-fs=12288;max-fr=60");
  
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].payload_type_, 125);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[3].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[3].fmtp_, "apt=121");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].payload_type_, 126);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[0], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[1], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].rtcp_fb_[4], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[4].fmtp_, 
    "profile-level-id=42e01f;level-asymmetry-allowed=1;packetization-mode=1");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].payload_type_, 127);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[5].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[5].fmtp_, "apt=126");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].payload_type_, 97);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].encoding_name_, "H264");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[0], "nack");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[1], "nack pli");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[2], "ccm fir");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[3], "goog-remb");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].rtcp_fb_[4], "transport-cc");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[6].fmtp_, 
    "profile-level-id=42e01f;level-asymmetry-allowed=1");

  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].payload_type_, 98);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].encoding_name_, "rtx");
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].clock_rate_, 90000);
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].encoding_param_, "");
  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[7].rtcp_fb_.empty());
  EXPECT_EQ(sdpinfo.media_descs_[1].rtp_maps_[7].fmtp_, "apt=97");

  EXPECT_EQ(sdpinfo.media_descs_[1].ssrc_groups_[0].semantic_, "FID");
  EXPECT_EQ(sdpinfo.media_descs_[1].ssrc_groups_[0].ssrcs_[0], 705814802);
  EXPECT_EQ(sdpinfo.media_descs_[1].ssrc_groups_[0].ssrcs_[1], 2879488689);
}

TEST(WaSdpInfo, firefox_init) {
  std::ifstream fin(firefox_sdp);
  
  ASSERT_EQ(true, fin.is_open());

  std::string sdp;
  int result = wa::wa_failed;
  
  wa::WaSdpInfo sdpinfo;
  while(std::getline(fin, sdp)){
    sdp = wa::wa_string_replace(sdp, "\\r\\n", "\r\n");
    try{
      result = sdpinfo.init(sdp);
    }catch(std::exception& ex){
      result = wa::wa_e_parse_offer_failed;
      std::cout << "exception catched :" << ex.what() << std::endl;
    }
  }
  ASSERT_EQ(result, wa::wa_ok);

  sdp_info_comp_firefox(sdpinfo);

  std::string sdpOut = sdpinfo.toString();

  wa::WaSdpInfo out_sdp_info;
  try{
    result = out_sdp_info.init(sdpOut);
  }catch(std::exception& ex){
    result = wa::wa_e_parse_offer_failed;
    std::cout << "write case: exception catched :" << ex.what() << std::endl;
  }
  ASSERT_EQ(result, wa::wa_ok);
}

void filterVideoPayload_firefox(wa::WaSdpInfo& sdpinfo) {
 
  wa::FormatPreference pre;
  pre.format_ = wa::p_h264;
  
  pre.profile_ = "42e01f";
  int result = sdpinfo.filterMediaPayload("1", pre);
  ASSERT_EQ(result, 127);

  sdpinfo.filterByPayload("1", result, true, true, true);

  //for (auto& i : sdpinfo.media_descs_[1].rtp_maps_) {
  //  std::cout << i.encoding_name_ << std::endl;
  //}
  ASSERT_EQ(sdpinfo.media_descs_[1].rtp_maps_.size(), 1);

  EXPECT_TRUE(sdpinfo.media_descs_[1].rtp_maps_[0].related_.empty());
}


void filterPayload_firefox_audio_crash(wa::WaSdpInfo& sdpinfo) {
 
  wa::FormatPreference pre;
  pre.format_ = wa::p_opus;

  int32_t pt = sdpinfo.filterMediaPayload("0", pre);

  EXPECT_TRUE(pt == 120);
  bool ret = sdpinfo.filterByPayload("0", pt, true, false, true);
  EXPECT_TRUE(ret);
  //for (auto& i : sdpinfo.media_descs_[1].rtp_maps_) {
  //  std::cout << i.encoding_name_ << std::endl;
  //}
  ASSERT_EQ(sdpinfo.media_descs_[0].rtp_maps_.size(), 1);

  EXPECT_TRUE(sdpinfo.media_descs_[0].rtp_maps_[0].related_.empty());
}

TEST(WaSdpInfo, firefox_answer) {
  std::ifstream fin(firefox_sdp);
  
  ASSERT_EQ(true, fin.is_open());

  std::string sdp;
  int result = wa::wa_failed;
  
  wa::WaSdpInfo sdpinfo;
  while(std::getline(fin, sdp)){
    sdp = wa::wa_string_replace(sdp, "\\r\\n", "\r\n");
    try{
      result = sdpinfo.init(sdp);
    }catch(std::exception& ex){
      result = wa::wa_e_parse_offer_failed;
      std::cout << "exception catched :" << ex.what() << std::endl;
    }
  }
  ASSERT_EQ(result, wa::wa_ok);

  std::unique_ptr<wa::WaSdpInfo> answer(sdpinfo.answer());
  EXPECT_EQ(answer->session_info_.setup_, "passive");

  filterVideoPayload_firefox(*answer.get());

  answer.reset(sdpinfo.answer());
  filterPayload_firefox_audio_crash(*answer.get());
}

