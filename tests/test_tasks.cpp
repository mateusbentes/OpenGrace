#include "grace/core/tasks.hpp"

#include <atomic>
#include <stdexcept>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::Scheduler;
using grace::core::TaskId;

bool throwsInvalidArgument(Scheduler& scheduler, Scheduler::TaskFn fn,
                           std::vector<TaskId> deps = {})
{
    try
    {
        static_cast<void>(scheduler.addTask(std::move(fn), std::move(deps)));
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // Rejections: an empty closure, an unknown dependency, a cycle.
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        GRACE_CHECK(throwsInvalidArgument(scheduler, nullptr));
        GRACE_CHECK(throwsInvalidArgument(scheduler, []() {}, {grace::core::kNoTask}));
        const TaskId a = scheduler.addTask([]() {});
        GRACE_CHECK(throwsInvalidArgument(scheduler, []() {}, {a, 999u}));
        // A dependency on an earlier task of the same batch is valid: the
        // declaration order makes a cycle structurally impossible to
        // express (a dependency always names a task declared before), and
        // the scheduler's cycle guard stays as the defensive invariant.
        const TaskId b = scheduler.addTask([]() {}, {a});
        GRACE_CHECK(b != a);
    }

    // A two-task chain runs in the dependency's order: the dependent sees
    // the dependency's write. This is the determinism the contract states.
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        int value = 0;
        const TaskId first = scheduler.addTask([&value]() { value = 41; });
        scheduler.addTask([&value]() { ++value; }, {first});
        scheduler.run();
        GRACE_CHECK_EQ(value, 42);
    }

    // A diamond: one root, two middles, one sink. The sink runs after both
    // middles; the middles run after the root. The final sum is the same
    // on every schedule.
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        std::atomic<int> sum{0};
        std::atomic<int> middlesDone{0};
        const TaskId root = scheduler.addTask([&sum]() { sum += 10; });
        const TaskId left = scheduler.addTask(
            [&sum, &middlesDone]() {
                sum += 1;
                ++middlesDone;
            },
            {root});
        const TaskId right = scheduler.addTask(
            [&sum, &middlesDone]() {
                sum += 2;
                ++middlesDone;
            },
            {root});
        scheduler.addTask([&sum, &middlesDone]() { sum += middlesDone.load(); }, {left, right});
        scheduler.run();
        // root 10 + one middle 1 + the other 2 + sink (2 middles) = 15.
        GRACE_CHECK_EQ(sum.load(), 15);
    }

    // Independent tasks all run: a batch of 64 increments a shared atomic;
    // every worker schedule ends with the same total.
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        std::atomic<int> counter{0};
        for (int i = 0; i < 64; ++i)
            scheduler.addTask([&counter]() { ++counter; });
        scheduler.run();
        GRACE_CHECK_EQ(counter.load(), 64);
    }

    // run() clears the batch: the next run() executes only the new tasks.
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        int first = 0;
        int second = 0;
        scheduler.addTask([&first]() { first = 1; });
        scheduler.run();
        GRACE_CHECK_EQ(first, 1);
        GRACE_CHECK_EQ(scheduler.pendingCount(), static_cast<std::size_t>(0));
        scheduler.addTask([&second]() { second = 7; });
        GRACE_CHECK_EQ(scheduler.pendingCount(), static_cast<std::size_t>(1));
        scheduler.run();
        GRACE_CHECK_EQ(second, 7);
        GRACE_CHECK_EQ(first, 1);
    }

    // An empty run() is a no-op, not an error.
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        scheduler.run();
        GRACE_CHECK(true);
    }

    // addTask during a running batch is rejected (a logic error, not a
    // corruption).
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        bool rejected = false;
        scheduler.addTask([&scheduler, &rejected]() {
            try
            {
                scheduler.addTask([]() {});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
        scheduler.run();
        GRACE_CHECK(rejected);
    }

    // shutdown() twice is safe (the documented rule).
    {
        Scheduler scheduler;
        scheduler.startWorkers();
        scheduler.shutdown();
        scheduler.shutdown();
        GRACE_CHECK(true);
    }

    // A scheduler never started still shuts down cleanly and its batch is
    // dropped.
    {
        Scheduler scheduler;
        scheduler.addTask([]() {});
        scheduler.shutdown();
        GRACE_CHECK(true);
    }

    return grace::test::summary("tasks");
}
