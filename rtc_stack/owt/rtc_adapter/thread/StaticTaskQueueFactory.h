// Copyright (C) <2020> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef RTC_ADAPTER_THREAD_STATIC_TASK_QUEUE_FACTORY_
#define RTC_ADAPTER_THREAD_STATIC_TASK_QUEUE_FACTORY_

#include <memory>

#include "api/task_queue_factory.h"

namespace rtc_adapter {

std::unique_ptr<webrtc::TaskQueueFactory> 
    createDummyTaskQueueFactory(webrtc::TaskQueueBase* pQueue);

}  // namespace webrtc

#endif  // RTC_ADAPTER_THREAD_STATIC_TASK_QUEUE_FACTORY_

