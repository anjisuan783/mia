/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_
#define MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "rtp_rtcp/report_block_data.h"
#include "rtp_rtcp/rtcp_statistics.h"
#include "rtp_rtcp/rtp_rtcp.h"
#include "rtp_rtcp/rtp_rtcp_defines.h"
#include "rtp_rtcp/rtcp_nack_stats.h"
#include "rtp_rtcp/dlrr.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/ntp_time.h"

namespace webrtc {
class VideoBitrateAllocationObserver;
namespace rtcp {
class CommonHeader;
class ReportBlock;
class Rrtr;
class TargetBitrate;
class TmmbItem;
}  // namespace rtcp

class RTCPReceiver {
 public:
  class ModuleRtpRtcp {
   public:
    virtual void SetTmmbn(std::vector<rtcp::TmmbItem> bounding_set) = 0;
    virtual void OnRequestSendReport() = 0;
    virtual void OnReceivedNack(
        const std::vector<uint16_t>& nack_sequence_numbers) = 0;
    virtual void OnReceivedRtcpReportBlocks(
        const ReportBlockList& report_blocks) = 0;

   protected:
    virtual ~ModuleRtpRtcp() = default;
  };

  RTCPReceiver(const RtpRtcp::Configuration& config, ModuleRtpRtcp* owner);
  virtual ~RTCPReceiver();

  void IncomingPacket(const uint8_t* packet, size_t packet_size);

  int64_t LastReceivedReportBlockMs() const;

  void SetRemoteSSRC(uint32_t ssrc);
  uint32_t RemoteSSRC() const;

  // Get received cname.
  int32_t CNAME(uint32_t remote_ssrc, char cname[RTCP_CNAME_SIZE]) const;

  // Get received NTP.
  bool NTP(uint32_t* received_ntp_secs,
           uint32_t* received_ntp_frac,
           uint32_t* rtcp_arrival_time_secs,
           uint32_t* rtcp_arrival_time_frac,
           uint32_t* rtcp_timestamp) const;

  std::vector<rtcp::ReceiveTimeInfo> ConsumeReceivedXrReferenceTimeInfo();

  // Get rtt.
  int32_t RTT(uint32_t remote_ssrc,
              int64_t* last_rtt_ms,
              int64_t* avg_rtt_ms,
              int64_t* min_rtt_ms,
              int64_t* max_rtt_ms) const;

  void SetRtcpXrRrtrStatus(bool enable);
  bool GetAndResetXrRrRtt(int64_t* rtt_ms);

  // Get statistics.
  int32_t StatisticsReceived(std::vector<RTCPReportBlock>* receiveBlocks) const;
  // A snapshot of Report Blocks with additional data of interest to statistics.
  // Within this list, the sender-source SSRC pair is unique and per-pair the
  // ReportBlockData represents the latest Report Block that was received for
  // that pair.
  std::vector<ReportBlockData> GetLatestReportBlockData() const;

  // Returns true if we haven't received an RTCP RR for several RTCP
  // intervals, but only triggers true once.
  bool RtcpRrTimeout();

  // Returns true if we haven't received an RTCP RR telling the receive side
  // has not received RTP packets for too long, i.e. extended highest sequence
  // number hasn't increased for several RTCP intervals. The function only
  // returns true once until a new RR is received.
  bool RtcpRrSequenceNumberTimeout();

  std::vector<rtcp::TmmbItem> TmmbrReceived();
  // Return true if new bandwidth should be set.
  bool UpdateTmmbrTimers();
  std::vector<rtcp::TmmbItem> BoundingSet(bool* tmmbr_owner);
  // Set new bandwidth and notify remote clients about it.
  void NotifyTmmbrUpdated();

  void RegisterRtcpStatisticsCallback(RtcpStatisticsCallback* callback);
  void RegisterRtcpCnameCallback(RtcpCnameCallback* callback);
  RtcpStatisticsCallback* GetRtcpStatisticsCallback();
  void SetReportBlockDataObserver(ReportBlockDataObserver* observer);

 private:
  struct PacketInformation;
  struct TmmbrInformation;
  struct RrtrInformation;
  struct LastFirStatus;
  // RTCP report blocks mapped by remote SSRC.
  using ReportBlockDataMap = std::map<uint32_t, ReportBlockData>;
  // RTCP report blocks map mapped by source SSRC.
  using ReportBlockMap = std::map<uint32_t, ReportBlockDataMap>;

  bool ParseCompoundPacket(const uint8_t* packet_begin,
                           const uint8_t* packet_end,
                           PacketInformation* packet_information);

  void TriggerCallbacksFromRtcpPacket(
      const PacketInformation& packet_information);

  TmmbrInformation* FindOrCreateTmmbrInfo(uint32_t remote_ssrc);
  // Update TmmbrInformation (if present) is alive.
  void UpdateTmmbrRemoteIsAlive(uint32_t remote_ssrc);
  TmmbrInformation* GetTmmbrInformation(uint32_t remote_ssrc);

  void HandleSenderReport(const rtcp::CommonHeader& rtcp_block,
                          PacketInformation* packet_information);

  void HandleReceiverReport(const rtcp::CommonHeader& rtcp_block,
                            PacketInformation* packet_information);

  void HandleReportBlock(const rtcp::ReportBlock& report_block,
                         PacketInformation* packet_information,
                         uint32_t remote_ssrc);

  void HandleSdes(const rtcp::CommonHeader& rtcp_block,
                  PacketInformation* packet_information);

  void HandleXr(const rtcp::CommonHeader& rtcp_block,
                PacketInformation* packet_information);

  void HandleXrReceiveReferenceTime(uint32_t sender_ssrc,
                                    const rtcp::Rrtr& rrtr);

  void HandleXrDlrrReportBlock(const rtcp::ReceiveTimeInfo& rti);

  void HandleXrTargetBitrate(uint32_t ssrc,
                             const rtcp::TargetBitrate& target_bitrate,
                             PacketInformation* packet_information);

  void HandleNack(const rtcp::CommonHeader& rtcp_block,
                  PacketInformation* packet_information);

  void HandleApp(const rtcp::CommonHeader& rtcp_block,
                 PacketInformation* packet_information);

  void HandleBye(const rtcp::CommonHeader& rtcp_block);

  void HandlePli(const rtcp::CommonHeader& rtcp_block,
                 PacketInformation* packet_information);

  void HandlePsfbApp(const rtcp::CommonHeader& rtcp_block,
                     PacketInformation* packet_information);

  void HandleTmmbr(const rtcp::CommonHeader& rtcp_block,
                   PacketInformation* packet_information);

  void HandleTmmbn(const rtcp::CommonHeader& rtcp_block,
                   PacketInformation* packet_information);

  void HandleSrReq(const rtcp::CommonHeader& rtcp_block,
                   PacketInformation* packet_information);

  void HandleFir(const rtcp::CommonHeader& rtcp_block,
                 PacketInformation* packet_information);

  void HandleTransportFeedback(const rtcp::CommonHeader& rtcp_block,
                               PacketInformation* packet_information);

  Clock* const clock_;
  const bool receiver_only_;
  ModuleRtpRtcp* const rtp_rtcp_;
  const uint32_t main_ssrc_;
  const std::set<uint32_t> registered_ssrcs_;

  RtcpBandwidthObserver* const rtcp_bandwidth_observer_;
  RtcpIntraFrameObserver* const rtcp_intra_frame_observer_;
  RtcpLossNotificationObserver* const rtcp_loss_notification_observer_;
  NetworkStateEstimateObserver* const network_state_estimate_observer_;
  TransportFeedbackObserver* const transport_feedback_observer_;
  VideoBitrateAllocationObserver* const bitrate_allocation_observer_;
  const int report_interval_ms_;

  uint32_t remote_ssrc_ ;

  // Received sender report.
  NtpTime remote_sender_ntp_time_ ;
  uint32_t remote_sender_rtp_time_ ;
  // When did we receive the last send report.
  NtpTime last_received_sr_ntp_ ;

  // Received RRTR information in ascending receive time order.
  std::list<RrtrInformation> received_rrtrs_
      ;
  // Received RRTR information mapped by remote ssrc.
  std::map<uint32_t, std::list<RrtrInformation>::iterator>
      received_rrtrs_ssrc_it_ ;

  // Estimated rtt, zero when there is no valid estimate.
  bool xr_rrtr_status_ ;
  int64_t xr_rr_rtt_ms_;

  int64_t oldest_tmmbr_info_ms_ ;
  // Mapped by remote ssrc.
  std::map<uint32_t, TmmbrInformation> tmmbr_infos_;

  ReportBlockMap received_report_blocks_ ;
  std::map<uint32_t, LastFirStatus> last_fir_;
  std::map<uint32_t, std::string> received_cnames_;

  // The last time we received an RTCP Report block for this module.
  int64_t last_received_rb_ms_ ;

  // The time we last received an RTCP RR telling we have successfully
  // delivered RTP packet to the remote side.
  int64_t last_increased_sequence_number_ms_;

  RtcpStatisticsCallback* stats_callback_;
  RtcpCnameCallback* cname_callback_;
  // TODO(hbos): Remove RtcpStatisticsCallback in favor of
  // ReportBlockDataObserver; the ReportBlockData contains a superset of the
  // RtcpStatistics data.
  ReportBlockDataObserver* report_block_data_observer_;

  RtcpPacketTypeCounterObserver* const packet_type_counter_observer_;
  RtcpPacketTypeCounter packet_type_counter_;

  RtcpNackStats nack_stats_;

  size_t num_skipped_packets_;
  int64_t last_skipped_packets_warning_ms_;
};
}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_
