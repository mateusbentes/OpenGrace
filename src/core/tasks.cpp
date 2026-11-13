#include "grace/core/tasks.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace grace::core {

Scheduler::~Scheduler()
{
    shutdown();
}

std::uint32_t Scheduler::defaultWorkerCount() noexcept
{
    const unsigned hardware = std::thread::hardware_concurrency();
    // hardware_concurrency may report 0 when the platform cannot tell; one
    // worker is the floor that keeps every batch runnable.
    return hardware == 0 ? 1u : static_cast<std::uint32_t>(hardware);
}

void Scheduler::startWorkers()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_shutdown || !m_workers.empty())
        return;
    const std::uint32_t count = defaultWorkerCount();
    m_workers.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        m_workers.emplace_back([this]() { workerLoop(); });
    }
}

void Scheduler::workerLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;)
    {
        m_ready.wait(lock, [this]() { return m_shutdown || !m_queue.empty(); });
        if (m_shutdown && m_queue.empty())
            return;
        const TaskId id = m_queue.back();
        m_queue.pop_back();
        // Find the task, take its closure out (so a re-entrant fn cannot
        // observe a half-cleared batch) and run it with the lock released.
        TaskFn fn;
        Task* task = nullptr;
        for (Task& candidate : m_batch)
        {
            if (candidate.id == id)
            {
                task = &candidate;
                break;
            }
        }
        if (task)
        {
            fn = std::move(task->fn);
            task->fn = nullptr;
        }
        lock.unlock();
        if (fn)
            fn();
        lock.lock();
        // Notify dependents: their unfinishedPrerequisites drop by one and
        // those reaching zero join the ready queue.
        if (task)
        {
            for (const TaskId dependentId : task->dependents)
            {
                for (Task& dependent : m_batch)
                {
                    if (dependent.id == dependentId)
                    {
                        --dependent.unfinishedPrerequisites;
                        if (dependent.unfinishedPrerequisites == 0u)
                            m_queue.push_back(dependent.id);
                        break;
                    }
                }
            }
        }
        --m_remaining;
        if (m_remaining == 0)
            m_done.notify_all();
    }
}

bool Scheduler::hasCycle(const Task& candidate) const
{
    // Depth-first walk from the candidate through its dependencies. The
    // batch is small when declared (hosts declare dozens, not thousands),
    // so the linear scan per hop is fine and keeps the structure simple.
    std::vector<TaskId> stack = candidate.dependencies;
    std::vector<TaskId> visited;
    while (!stack.empty())
    {
        const TaskId current = stack.back();
        stack.pop_back();
        if (current == candidate.id)
            return true;
        if (std::find(visited.begin(), visited.end(), current) != visited.end())
            continue;
        visited.push_back(current);
        for (const Task& task : m_batch)
        {
            if (task.id == current)
            {
                stack.insert(stack.end(), task.dependencies.begin(), task.dependencies.end());
                break;
            }
        }
    }
    return false;
}

TaskId Scheduler::addTask(TaskFn fn, std::vector<TaskId> dependencies)
{
    if (!fn)
        throw std::invalid_argument("Scheduler::addTask: the task closure must not be empty");
    std::lock_guard<std::mutex> guard(m_mutex);
    if (m_running)
        throw std::logic_error("Scheduler::addTask: the previous batch is still running");
    for (const TaskId dependency : dependencies)
    {
        const bool known = std::any_of(m_batch.begin(), m_batch.end(),
                                       [dependency](const Task& task) {
                                           return task.id == dependency;
                                       });
        if (!known)
            throw std::invalid_argument(
                "Scheduler::addTask: dependency is not a task of this batch");
    }
    Task task;
    task.id = m_nextId++;
    task.fn = fn;
    task.dependencies = std::move(dependencies);
    if (hasCycle(task))
        throw std::invalid_argument("Scheduler::addTask: the dependency graph has a cycle");
    m_batch.push_back(std::move(task));
    return m_batch.back().id;
}

void Scheduler::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_running)
        throw std::logic_error("Scheduler::run: the previous batch is still running");
    if (m_batch.empty())
        return;

    // Seal the batch: resolve the dependency lists into prerequisite counts
    // and dependents lists, then queue the tasks with zero prerequisites.
    m_remaining = m_batch.size();
    for (Task& task : m_batch)
        task.unfinishedPrerequisites = static_cast<std::uint32_t>(task.dependencies.size());
    for (const Task& task : m_batch)
    {
        for (const TaskId dependency : task.dependencies)
        {
            for (Task& other : m_batch)
            {
                if (other.id == dependency)
                {
                    other.dependents.push_back(task.id);
                    break;
                }
            }
        }
    }
    m_queue.clear();
    for (const Task& task : m_batch)
    {
        if (task.unfinishedPrerequisites == 0u)
            m_queue.push_back(task.id);
    }

    m_running = true;
    lock.unlock();
    m_ready.notify_all();

    lock.lock();
    m_done.wait(lock, [this]() { return m_remaining == 0; });
    m_running = false;

    // The batch is finished: clear it so the next run() starts fresh. The
    // workers only touch m_batch under the lock, which they hold, so
    // clearing here is safe.
    m_batch.clear();
    m_queue.clear();
}

std::size_t Scheduler::pendingCount() const
{
    // const method over a mutable mutex: the guard's mutex_type is the
    // member itself; the mutex is declared mutable for exactly this.
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_batch.size();
}

void Scheduler::shutdown()
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_shutdown = true;
    }
    // Notify with the flag set and join without holding the lock: the
    // workers need the mutex to observe the shutdown and return, so a
    // join under the lock would wait for them forever.
    m_ready.notify_all();
    for (std::thread& worker : m_workers)
        worker.join();
    m_workers.clear();
    // A repeated shutdown observes the empty worker list and returns: the
    // second call is safe, exactly as the contract states.
}

}  // namespace grace::core
