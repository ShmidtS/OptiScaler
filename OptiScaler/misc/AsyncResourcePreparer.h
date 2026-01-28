#pragma once

#include <pch.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <future>

namespace OptiScaler {

// Task priority for async resource preparation
enum class TaskPriority {
    Critical = 0,  // Must complete before next frame
    High = 1,      // Should complete soon
    Normal = 2,    // Standard priority
    Low = 3        // Can be deferred
};

// Async task for resource preparation
struct AsyncTask {
    std::function<void()> work;
    std::function<void()> callback;
    TaskPriority priority = TaskPriority::Normal;
    uint64_t frameId = 0;
    std::chrono::steady_clock::time_point submitTime;
    
    bool operator<(const AsyncTask& other) const {
        // Higher priority (lower number) comes first
        if (priority != other.priority) {
            return priority > other.priority;
        }
        // Earlier submit time comes first
        return submitTime > other.submitTime;
    }
};

// Async resource preparer for non-blocking resource operations
class AsyncResourcePreparer {
private:
    std::thread _workerThread;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::priority_queue<AsyncTask> _taskQueue;
    std::atomic<bool> _running{false};
    std::atomic<bool> _shouldExit{false};
    
    // Statistics
    std::atomic<uint64_t> _tasksSubmitted{0};
    std::atomic<uint64_t> _tasksCompleted{0};
    std::atomic<uint64_t> _tasksCancelled{0};
    
    // Thread-local storage for worker
    static thread_local bool _isWorkerThread;

public:
    AsyncResourcePreparer();
    ~AsyncResourcePreparer();

    // Non-copyable
    AsyncResourcePreparer(const AsyncResourcePreparer&) = delete;
    AsyncResourcePreparer& operator=(const AsyncResourcePreparer&) = delete;

    // Start the worker thread
    void Start();
    
    // Stop the worker thread
    void Stop();
    
    // Submit a task for async execution
    // Returns a future that can be used to wait for completion
    template<typename Func, typename Callback = std::function<void()>>
    auto Submit(Func&& func, Callback&& callback = nullptr, TaskPriority priority = TaskPriority::Normal) 
        -> std::future<decltype(func())> {
        
        using ReturnType = decltype(func());
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future();
        
        AsyncTask task;
        task.work = [promise, func = std::forward<Func>(func)]() mutable {
            try {
                if constexpr (std::is_void_v<ReturnType>) {
                    func();
                    promise->set_value();
                } else {
                    promise->set_value(func());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        };
        
        if (callback) {
            task.callback = std::forward<Callback>(callback);
        }
        
        task.priority = priority;
        task.frameId = _tasksSubmitted.load();
        task.submitTime = std::chrono::steady_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _taskQueue.push(std::move(task));
            _tasksSubmitted++;
        }
        
        _cv.notify_one();
        return future;
    }
    
    // Submit a simple void task
    void SubmitSimple(std::function<void()> work, TaskPriority priority = TaskPriority::Normal);
    
    // Cancel all pending tasks
    void CancelPending();
    
    // Wait for all tasks to complete
    void WaitForAll();
    
    // Check if current thread is the worker thread
    static bool IsWorkerThread() { return _isWorkerThread; }
    
    // Get statistics
    uint64_t GetTasksSubmitted() const { return _tasksSubmitted.load(); }
    uint64_t GetTasksCompleted() const { return _tasksCompleted.load(); }
    uint64_t GetTasksCancelled() const { return _tasksCancelled.load(); }
    size_t GetPendingCount() const;
    
    // Check if running
    bool IsRunning() const { return _running.load(); }

private:
    void WorkerLoop();
};

// Parallel for implementation using thread pool
class ParallelFor {
public:
    // Execute function in parallel across multiple threads
    static void Execute(size_t start, size_t end, std::function<void(size_t)> func, size_t minGrainSize = 64);
    
    // Execute with dynamic workload distribution
    static void ExecuteDynamic(size_t start, size_t end, std::function<void(size_t)> func);
};

// Frame-parallel task manager
class FrameTaskManager {
private:
    struct FrameTasks {
        uint64_t frameId;
        std::vector<std::future<void>> futures;

        FrameTasks(uint64_t id) : frameId(id) {}

        // Move-only
        FrameTasks(FrameTasks&&) = default;
        FrameTasks& operator=(FrameTasks&&) = default;
        FrameTasks(const FrameTasks&) = delete;
        FrameTasks& operator=(const FrameTasks&) = delete;
    };
    
    std::deque<FrameTasks> _pendingFrames;
    std::mutex _mutex;
    uint64_t _currentFrame = 0;
    size_t _maxPendingFrames = 3;

public:
    void SetMaxPendingFrames(size_t maxFrames) { _maxPendingFrames = maxFrames; }
    
    // Submit a task for the current frame
    void SubmitForCurrentFrame(std::function<void()> task);
    
    // Move to next frame
    void NextFrame();
    
    // Wait for a specific frame to complete
    void WaitForFrame(uint64_t frameId);
    
    // Wait for all pending frames
    void WaitForAll();
    
    // Get current frame ID
    uint64_t GetCurrentFrame() const { return _currentFrame; }
    
    // Check if a frame has completed
    bool IsFrameComplete(uint64_t frameId);
};

} // namespace OptiScaler
