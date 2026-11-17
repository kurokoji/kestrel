#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

// One running job and at most one pending replacement. No job runs under
// the mutex. Stop callbacks must be quick: they run on the submitting thread.
class LatestWorker {
public:
    using Job = std::function<void(std::stop_token)>;

    ~LatestWorker() {
        worker_.request_stop();
        cancel();
        // jthread joins before the shared state is destroyed.
    }

    void submit(Job job) {
        replace(std::move(job));
    }

    void cancel() { replace({}); }

private:
    void replace(Job job) {
        std::stop_source running(std::nostopstate);
        {
            std::lock_guard lock(mutex_);
            pending_ = std::move(job);
            running = running_;
        }
        running.request_stop();
        ready_.notify_one();
    }

    void run(std::stop_token shutdown) {
        for (;;) {
            Job job;
            std::stop_token stop;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, shutdown, [this] { return bool(pending_); });
                if (shutdown.stop_requested()) return;
                job = std::exchange(pending_, {});
                running_ = std::stop_source{};
                stop = running_.get_token();
            }
            job(stop);
        }
    }

    std::mutex mutex_;
    std::condition_variable_any ready_;
    Job pending_;
    std::stop_source running_{std::nostopstate};
    std::jthread worker_{[this](std::stop_token shutdown) { run(shutdown); }};
};
