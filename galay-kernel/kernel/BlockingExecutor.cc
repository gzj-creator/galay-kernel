#include "BlockingExecutor.h"
#include <stdexcept>
#include <thread>
#include <utility>

namespace galay::kernel
{

namespace
{

constexpr auto kDefaultBlockingKeepAlive = std::chrono::milliseconds(5000);

} // namespace

BlockingExecutor::BlockingExecutor()
    : BlockingExecutor(0, defaultMaxWorkers(), kDefaultBlockingKeepAlive)
{
}

BlockingExecutor::BlockingExecutor(size_t minWorkers,
                                   size_t maxWorkers,
                                   std::chrono::milliseconds keepAlive)
    : m_minWorkers(minWorkers),
      m_maxWorkers(maxWorkers > 0 ? maxWorkers : 1),
      m_keepAlive(keepAlive)
{
    if (m_minWorkers > m_maxWorkers) {
        m_minWorkers = m_maxWorkers;
    }
}

BlockingExecutor::~BlockingExecutor()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_stopping = true;
    m_taskCv.notify_all();
    m_shutdownCv.wait(lock, [this]() { return m_workerCount == 0; });
}

size_t BlockingExecutor::defaultMaxWorkers()
{
    const size_t hardware = std::thread::hardware_concurrency();
    return hardware > 0 ? hardware : 4;
}

void BlockingExecutor::submit(std::function<void()> task)
{
    if (!task) {
        return;
    }

    bool shouldNotify = false;
    bool shouldSpawn = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            throw std::runtime_error("blocking executor is stopping");
        }

        if (m_idleWorkers > 0) {
            m_tasks.push_back(std::move(task));
            shouldNotify = true;
        } else if (m_workerCount < m_maxWorkers) {
            ++m_workerCount;
            shouldSpawn = true;
        } else {
            m_tasks.push_back(std::move(task));
        }
    }

    if (shouldNotify) {
        m_taskCv.notify_one();
        return;
    }

    if (!shouldSpawn) {
        return;
    }

    try {
        std::thread([this, initialTask = std::move(task)]() mutable {
            workerLoop(std::move(initialTask));
        }).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_mutex);
        --m_workerCount;
        if (m_stopping && m_workerCount == 0) {
            m_shutdownCv.notify_all();
        }
        throw;
    }
}

void BlockingExecutor::workerLoop(std::function<void()> initialTask)
{
    std::function<void()> task = std::move(initialTask);

    for (;;) {
        if (task) {
            try {
                task();
            } catch (...) {
            }
            task = {};
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        while (m_tasks.empty() && !m_stopping) {
            ++m_idleWorkers;
            const bool canTimeOut = m_workerCount > m_minWorkers;

            bool ready = true;
            if (canTimeOut) {
                ready = m_taskCv.wait_for(lock, m_keepAlive, [this]() {
                    return m_stopping || !m_tasks.empty();
                });
            } else {
                m_taskCv.wait(lock, [this]() {
                    return m_stopping || !m_tasks.empty();
                });
            }

            --m_idleWorkers;

            if (!ready && m_tasks.empty() && m_workerCount > m_minWorkers) {
                retireWorkerLocked();
                return;
            }
        }

        if (m_stopping && m_tasks.empty()) {
            retireWorkerLocked();
            return;
        }

        if (m_tasks.empty()) {
            continue;
        }

        task = std::move(m_tasks.front());
        m_tasks.pop_front();
    }
}

void BlockingExecutor::retireWorkerLocked()
{
    --m_workerCount;
    if (m_stopping && m_workerCount == 0) {
        m_shutdownCv.notify_all();
    }
}

} // namespace galay::kernel
