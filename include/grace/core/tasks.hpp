#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace grace::core {

/// A task identifier. 0 is the null id; real ids start at 1 and are never
/// reused within a Scheduler's lifetime.
using TaskId = std::uint64_t;

inline constexpr TaskId kNoTask = 0;

/// The task system (GRACE 2's documented "task-based multithreading").
///
/// The design point is *determinism of results, not of schedules*. Tasks run
/// concurrently on a pool of worker threads, but a task's own execution is a
/// pure function of its closure: the system never shares data between tasks
/// implicitly, and a host that only lets tasks communicate through the
/// dependency edges gets the same final state no matter which worker ran
/// what. The deterministic tests rely on exactly that.
///
/// The lifecycle:
/// - construction starts no threads; startWorkers() spawns them,
/// - tasks are declared with addTask() (closure + dependencies) and only
///   begin executing once the host calls run(),
/// - run() returns when every declared task has finished; it can be called
///   again for a new batch,
/// - shutdown() joins the workers. Calling it twice is safe (the second
///   call observes the already-joined state and returns).
///
/// Dependencies are declared among tasks of the same batch. A cycle is
/// rejected at addTask() time with std::invalid_argument (a cycle can never
/// run; failing early keeps the host's batch inspectable). A dependency on
/// an unknown id is the same rejection.
class Scheduler
{
public:
    using TaskFn = std::function<void()>;

    Scheduler() = default;
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// The worker count startWorkers() will spawn: the hardware
    /// concurrency, at least 1. Recorded so a host can size batches.
    static std::uint32_t defaultWorkerCount() noexcept;

    /// Spawns the worker pool. Calling it twice is a no-op (the pool stays
    /// the one the first call created).
    void startWorkers();

    /// Declares one task of the next batch: the closure and the ids it
    /// must wait for. A dependency always names a task declared earlier
    /// in the same batch — that declaration order is what makes a cycle
    /// structurally inexpressible, so the cycle check below is a
    /// defensive invariant rather than a reachable failure. Throws
    /// std::invalid_argument on an empty closure or a dependency that is
    /// not a task of the current batch.
    TaskId addTask(TaskFn fn, std::vector<TaskId> dependencies = {});

    /// Runs the whole batch and returns when every task has finished. The
    /// batch is cleared; the next run() executes the next batch. Tasks may
    /// add nothing while the batch runs (declaring during a run is a
    /// programming error the mutex turns into a wait, not a corruption).
    void run();

    /// The number of tasks the current batch holds.
    std::size_t pendingCount() const;

    /// Joins the workers. Safe to call twice. Unfinished tasks are dropped:
    /// shutdown means the host is done with this scheduler.
    void shutdown();

private:
    struct Task
    {
        TaskId id = kNoTask;
        TaskFn fn;
        std::vector<TaskId> dependencies;
        std::uint32_t unfinishedPrerequisites = 0;  ///< valid once the batch is sealed
        std::vector<TaskId> dependents;             ///< tasks waiting on this one
    };

    /// The cycle check: walks the declared graph from the new task.
    bool hasCycle(const Task& candidate) const;

    /// The worker body: takes ready tasks off the queue, runs their
    /// closures with the lock released, and notifies dependents.
    void workerLoop();

    std::vector<Task> m_batch;
    TaskId m_nextId = 1;

    std::vector<std::thread> m_workers;
    /// mutable: pendingCount() is a const method that still locks.
    mutable std::mutex m_mutex;
    std::condition_variable m_ready;
    std::condition_variable m_done;
    /// The ready queue (task ids whose prerequisites are all finished) and
    /// the number of unfinished tasks of the running batch.
    std::vector<TaskId> m_queue;
    std::size_t m_remaining = 0;
    bool m_shutdown = false;
    bool m_running = false;
};

}  // namespace grace::core
