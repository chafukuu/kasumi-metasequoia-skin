#include "utils/serial_task_runner.h"
#include "tests/includes/test_framework.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

TEST_CASE(serial_task_runner_runs_jobs_in_posting_order)
{
    std::vector<int> order;
    {
        SerialTaskRunner runner;
        for (int i = 0; i < 100; ++i)
        {
            // Only the runner thread touches `order`, so no lock is needed.
            runner.Post([&order, i] { order.push_back(i); });
        }
    }
    REQUIRE(order.size() == 100);
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE(order[i] == i);
    }
}

TEST_CASE(serial_task_runner_stop_drains_queued_jobs)
{
    std::atomic<int> done{0};
    SerialTaskRunner runner(/*below_normal_priority=*/true);
    // Hold the thread busy so the rest are still queued when Stop() arrives.
    runner.Post([&done] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ++done;
    });
    for (int i = 0; i < 10; ++i)
    {
        runner.Post([&done] { ++done; });
    }
    runner.Stop();
    REQUIRE(done.load() == 11);
    // Posting after Stop() is ignored rather than run or crashing.
    runner.Post([&done] { ++done; });
    runner.Stop();
    REQUIRE(done.load() == 11);
}

TEST_CASE(serial_task_runner_survives_a_throwing_job)
{
    std::atomic<int> done{0};
    {
        SerialTaskRunner runner;
        runner.Post([] { throw std::runtime_error("write failed"); });
        runner.Post([&done] { ++done; });
    }
    REQUIRE(done.load() == 1);
}

TEST_CASE(serial_task_runner_does_not_run_jobs_on_the_posting_thread)
{
    const std::thread::id poster = std::this_thread::get_id();
    std::thread::id runner_thread;
    {
        SerialTaskRunner runner;
        runner.Post([&runner_thread] { runner_thread = std::this_thread::get_id(); });
    }
    REQUIRE(runner_thread != std::thread::id());
    REQUIRE(runner_thread != poster);
}
