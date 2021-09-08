//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
// This file is borrowed from srs with some modifications.

#ifndef __MEDIA_CONSUMER_H__
#define __MEDIA_CONSUMER_H__

#include <memory>
#include <vector>

#include "common/media_define.h"
#include "common/media_kernel_error.h"
#include "h/media_server_api.h"

namespace ma {

class MediaMessage;

class MediaConsumer;

// The message queue for the consumer(client), forwarder.
// We limit the size in seconds, drop old messages(the whole gop) if full.
class MessageQueue final {
public:
  MessageQueue(bool ignore_shrink = false);
  ~MessageQueue();
public:
  // Get the size of queue.
  int size();
  // Get the duration of queue.
  srs_utime_t duration();
  // Set the queue size
  // @param queue_size the queue size in srs_utime_t.
  void set_queue_size(srs_utime_t queue_size);
public:
  void enqueue(std::shared_ptr<MediaMessage> msg, bool* is_overflow = NULL);
  // Get packets in consumer queue.
  // @pmsgs SrsSharedPtrMessage*[], used to store the msgs, user must alloc it.
  // @count the count in array, output param.
  // @max_count the max count to dequeue, must be positive.
  void fetch_packets(int max_count, std::vector<std::shared_ptr<MediaMessage>>& pmsgs, int& count);
  // Dumps packets to consumer, use specified args.
  // @remark the atc/tba/tbv/ag are same to SrsConsumer.enqueue().
  void fetch_packets(MediaConsumer* consumer, bool atc, JitterAlgorithm ag);

private:
  // Remove a gop from the front.
  // if no iframe found, clear it.
  void shrink();
public:
  // clear all messages in queue.
  void clear();

private:
  // The start and end time.
  srs_utime_t av_start_time{-1};
  srs_utime_t av_end_time{-1};

// Whether do logging when shrinking.
  bool _ignore_shrink;
  // The max queue size, shrink if exceed it.
  srs_utime_t max_queue_size{0};
  std::vector<std::shared_ptr<MediaMessage>> msgs;
};

class MediaSource;

class MediaConsumer final {
public:
  MediaConsumer(MediaSource*);
  ~MediaConsumer();

  // Set the size of queue.
  void set_queue_size(srs_utime_t queue_size);

  // Get current client time, the last packet time.
  int64_t get_time();
  // Enqueue an shared ptr message.
  // @param shared_msg, directly ptr, copy it if need to save it.
  // @param whether atc, donot use jitter correct if true.
  // @param ag the algorithm of time jitter.
  void enqueue(std::shared_ptr<MediaMessage> shared_msg, bool atc, JitterAlgorithm ag);

  // Get packets in consumer queue.
  // @param msgs the msgs array to dump packets to send.
  // @param count the count in array, intput and output param.
  // @remark user can specifies the count to get specified msgs; 0 to get all if possible.
  void fetch_packets(int max, std::vector<std::shared_ptr<MediaMessage>>& msgs, int& count);

  // when client send the pause message.
  void on_play_client_pause(bool is_pause);
  
 private:
  // Time jitter detect and correct, to ensure the rtmp stream is monotonically.
  class MediaJitter final {
   public:
    MediaJitter();

   public:
    // detect the time jitter and correct it.
    // @param ag the algorithm to use for time jitter.
    void correct(std::shared_ptr<MediaMessage> msg, JitterAlgorithm ag);
    // Get current client time, the last packet time.
    int64_t get_time();

   private:
    int64_t last_pkt_time{0};
    int64_t last_pkt_correct_time{-1};
  };
 
  MediaJitter jitter_;
  MediaSource* source_;
  MessageQueue queue_;

  bool paused_;
};

}
#endif //!__MEDIA_CONSUMER_H__

