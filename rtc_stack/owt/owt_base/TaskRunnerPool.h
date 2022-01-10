// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#ifndef TaskRunnerPool_h
#define TaskRunnerPool_h

#include <memory>
#include <vector>

#include "owt_base/WebRTCTaskRunner.h"

namespace owt_base {

/**
 * `TaskRunnerPool` contains a fixed number of TaskRunners,
 * `GetTaskRunner` function will get one with round robin.
 */
class TaskRunnerPool {
public:
    static TaskRunnerPool& GetInstance();
    std::shared_ptr<WebRTCTaskRunner> GetTaskRunner();

private:
    TaskRunnerPool();
    ~TaskRunnerPool();

    int m_nextRunner;
    std::vector<std::shared_ptr<WebRTCTaskRunner> > m_taskRunners;
};

} /* namespace owt_base */

#endif /* TaskRunnerPool_h */
