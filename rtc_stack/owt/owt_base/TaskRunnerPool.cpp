// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "owt_base/TaskRunnerPool.h"

namespace owt_base {

static constexpr int kTaskRunnerPoolSize = 4;

TaskRunnerPool& TaskRunnerPool::GetInstance()
{
    static TaskRunnerPool taskRunnerPool;
    return taskRunnerPool;
}

std::shared_ptr<WebRTCTaskRunner> TaskRunnerPool::GetTaskRunner()
{
    // This function would always be called in Node's main thread
    std::shared_ptr<WebRTCTaskRunner> runner = m_taskRunners[m_nextRunner];
    m_nextRunner = (m_nextRunner + 1) % m_taskRunners.size();
    return runner;
}

TaskRunnerPool::TaskRunnerPool()
    : m_nextRunner(0)
    , m_taskRunners(kTaskRunnerPoolSize)
{
    for (size_t i = 0; i < m_taskRunners.size(); i++) {
        m_taskRunners[i] = std::make_shared<WebRTCTaskRunner>("TaskRunner");
        m_taskRunners[i]->Start();
    }
}

TaskRunnerPool::~TaskRunnerPool()
{
    for (size_t i = 0; i < m_taskRunners.size(); i++) {
        m_taskRunners[i]->Stop();
        m_taskRunners[i].reset();
    }
}

} // namespace owt_base
