// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

// This file is borrowed from lynckia/licode with some modifications.


#ifndef ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_
#define ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_

#include <string>
#include <vector>
#include <queue>
#include <map>
#include <mutex>

#include "erizo/IceConnection.h"
#include "erizo/MediaDefinitions.h"
#include "erizo/SdpInfo.h"
#include "erizo/logger.h"
#include "erizo/lib/LibNiceInterface.h"
#include "utils/IOWorker.h"

typedef struct _NiceAgent NiceAgent;
typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;

typedef unsigned int uint;

namespace erizo {

#define NICE_STREAM_MAX_UFRAG   256 + 1  /* ufrag + NULL */
#define NICE_STREAM_MAX_UNAME   256 * 2 + 1 + 1 /* 2*ufrag + colon + NULL */
#define NICE_STREAM_MAX_PWD     256 + 1  /* pwd + NULL */
#define NICE_STREAM_DEF_UFRAG   4 + 1    /* ufrag + NULL */
#define NICE_STREAM_DEF_PWD     22 + 1   /* pwd + NULL */

class CandidateInfo;
class WebRtcConnection;

class LibNiceConnection : public IceConnection {
  DECLARE_LOGGER();

 public:
  static void libnice_log(const gchar *log_domain, 
                          GLogLevelFlags log_level, 
                          const gchar *message, 
                          gpointer user_data);

  static LibNiceConnection* create(const IceConfig& ice_config, wa::IOWorker* worker);
 
  LibNiceConnection(std::unique_ptr<LibNiceInterface> libnice,
    const IceConfig& ice_config, wa::IOWorker* worker);

  virtual ~LibNiceConnection();
  /**
   * Starts Gathering candidates in a new thread.
   */
  void start() override;
  bool setRemoteCandidates(const std::vector<CandidateInfo> &candidates, bool is_bundle) override;
  
  void gatheringDone(uint stream_id);
  
  void setRemoteCredentials(const std::string& username, const std::string& password) override;
  
  int sendData(unsigned int component_id, const void* buf, int len) override;

  void updateComponentState(unsigned int component_id, IceState state);
  
  void onData(unsigned int component_id, char* buf, int len) override;
  
  CandidatePair getSelectedPair() override;
  
  void setReceivedLastCandidate(bool hasReceived) override;
  
  void close() override;

  bool removeRemoteCandidates() override;

  void gotCandidate(NiceCandidate*);

  void onRemoteNewCandidate(NiceCandidate*);

 private:
  void mainLoop();
  CandidateInfo transformCandidate(NiceCandidate* cand, bool local);
  
 private:
  std::unique_ptr<LibNiceInterface> lib_nice_;
  NiceAgent* agent_;
  GMainContext* context_;
  GMainLoop* loop_;

  unsigned int candsDelivered_;
  std::mutex close_mutex_;
  bool receivedLastCandidate_;

  guint stream_id_{0};
};

}  // namespace erizo
#endif  // ERIZO_SRC_ERIZO_LIBNICECONNECTION_H_
