#include "doctest.h"
#include "LatestWorker.h"

#include <chrono>
#include <atomic>
#include <future>

TEST_CASE("Replacing blocked work returns promptly and keeps only the latest job") {
    using namespace std::chrono_literals;
    std::promise<void> entered, release;
    auto unblock = release.get_future();
    std::promise<bool> cancelled;
    std::atomic<bool> ranSuperseded = false;
    std::promise<int> handled;
    LatestWorker worker;
    worker.submit([&](std::stop_token stop) {
        entered.set_value();
        unblock.wait();
        cancelled.set_value(stop.stop_requested());
    });
    entered.get_future().wait();

    // Release the blocked task even on failure, so a regression cannot
    // deadlock this test.
    auto submit = std::async(std::launch::async, [&] {
        worker.submit([&](std::stop_token) { ranSuperseded = true; });
        worker.submit([&](std::stop_token) { handled.set_value(2); });
    });
    const auto submitted = submit.wait_for(2s);
    release.set_value();
    submit.get();
    REQUIRE(submitted == std::future_status::ready);
    CHECK(cancelled.get_future().get());
    auto result = handled.get_future();
    REQUIRE(result.wait_for(2s) == std::future_status::ready);
    CHECK(result.get() == 2);
    CHECK_FALSE(ranSuperseded.load());
}
