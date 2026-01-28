#include "AsyncResourcePreparer.h"

namespace OptiScaler {

thread_local bool AsyncResourcePreparer::_isWorkerThread = false;

AsyncResourcePreparer::AsyncResourcePreparer() {
    Start();
}

AsyncResourcePreparer::~AsyncResourcePreparer() {
    Stop();
}

void AsyncResourcePreparer::Start() {
    if (_running.exchange(true)) {
        return; // Already running
    }
    
    _shouldExit = false;
    _workerThread = std::thread(&AsyncResourcePreparer::WorkerLoop, this);
    
    // Set thread priority
    SetThreadPriority(_workerThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);
    
    LOG_INFO("AsyncResourcePreparer started");
}

void AsyncResourcePreparer::Stop() {
    if (!_running.exchange(false)) {
        return; // Not running
    }
    
    _shouldExit = true;
    _cv.notify_all();
    
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
    
    // Cancel any pending tasks
    CancelPending();
    
    LOG_INFO("AsyncResourcePreparer stopped, completed: {}, cancelled: {}", 
             _tasksCompleted.load(), _tasksCancelled.load());
}

void AsyncResourcePreparer::SubmitSimple(std::function<void()> work, TaskPriority priority) {
    AsyncTask task;
    task.work = std::move(work);
    task.priority = priority;
    task.frameId = _tasksSubmitted.load();
    task.submitTime = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _taskQueue.push(std::move(task));
        _tasksSubmitted++;
    }
    
    _cv.notify_one();
}

void AsyncResourcePreparer::CancelPending() {
    std::lock_guard<std::mutex> lock(_mutex);
    
    while (!_taskQueue.empty()) {
        auto task = std::move(const_cast<AsyncTask&>(_taskQueue.top()));
        _taskQueue.pop();
        _tasksCancelled++;
    }
}

void AsyncResourcePreparer::WaitForAll() {
    std::unique_lock<std::mutex> lock(_mutex);
    
    _cv.wait(lock, [this]() {
        return _taskQueue.empty();
    });
}

size_t AsyncResourcePreparer::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _taskQueue.size();
}

void AsyncResourcePreparer::WorkerLoop() {
    _isWorkerThread = true;
    
    LOG_DEBUG("AsyncResourcePreparer worker thread started");
    
    while (!_shouldExit) {
        AsyncTask task;
        
        {
            std::unique_lock<std::mutex> lock(_mutex);
            
            _cv.wait(lock, [this]() {
                return !_taskQueue.empty() || _shouldExit;
            });
            
            if (_shouldExit) {
                break;
            }
            
            if (_taskQueue.empty()) {
                continue;
            }
            
            task = std::move(const_cast<AsyncTask&>(_taskQueue.top()));
            _taskQueue.pop();
        }
        
        // Execute the task
        try {
            task.work();
            _tasksCompleted++;
            
            // Execute callback if provided
            if (task.callback) {
                task.callback();
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Async task exception: {}", e.what());
        } catch (...) {
            LOG_ERROR("Async task unknown exception");
        }
    }
    
    _isWorkerThread = false;
    LOG_DEBUG("AsyncResourcePreparer worker thread stopped");
}

// ParallelFor implementation
void ParallelFor::Execute(size_t start, size_t end, std::function<void(size_t)> func, size_t minGrainSize) {
    size_t count = end - start;
    
    if (count == 0) {
        return;
    }
    
    if (count <= minGrainSize) {
        // Execute sequentially for small ranges
        for (size_t i = start; i < end; ++i) {
            func(i);
        }
        return;
    }
    
    // Determine number of threads
    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) {
        numThreads = 4;
    }
    
    // Calculate grain size
    size_t grainSize = std::max(minGrainSize, (count + numThreads - 1) / numThreads);
    
    // Launch threads
    std::vector<std::thread> threads;
    
    for (size_t t = 0; t < numThreads; ++t) {
        size_t threadStart = start + t * grainSize;
        size_t threadEnd = std::min(threadStart + grainSize, end);
        
        if (threadStart >= threadEnd) {
            break;
        }
        
        threads.emplace_back([threadStart, threadEnd, &func]() {
            for (size_t i = threadStart; i < threadEnd; ++i) {
                func(i);
            }
        });
    }
    
    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }
}

void ParallelFor::ExecuteDynamic(size_t start, size_t end, std::function<void(size_t)> func) {
    // For now, use static distribution
    // Could be enhanced with work-stealing queue
    Execute(start, end, func, 1);
}

// FrameTaskManager implementation
void FrameTaskManager::SubmitForCurrentFrame(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    if (_pendingFrames.empty() || _pendingFrames.back().frameId != _currentFrame) {
        _pendingFrames.emplace_back(_currentFrame);
    }
    
    // Create a packaged task
    std::packaged_task<void()> packaged(std::move(task));
    _pendingFrames.back().futures.push_back(packaged.get_future());
    
    // Launch the task
    std::thread(std::move(packaged)).detach();
}

void FrameTaskManager::NextFrame() {
    std::lock_guard<std::mutex> lock(_mutex);
    
    _currentFrame++;
    
    // Remove old completed frames
    while (_pendingFrames.size() > _maxPendingFrames) {
        auto& frame = _pendingFrames.front();
        bool allComplete = true;
        
        for (auto& future : frame.futures) {
            if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                allComplete = false;
                break;
            }
        }
        
        if (allComplete) {
            _pendingFrames.pop_front();
        } else {
            break;
        }
    }
}

void FrameTaskManager::WaitForFrame(uint64_t frameId) {
    std::unique_lock<std::mutex> lock(_mutex);
    
    for (auto& frame : _pendingFrames) {
        if (frame.frameId == frameId) {
            lock.unlock();
            
            for (auto& future : frame.futures) {
                future.wait();
            }
            
            return;
        }
    }
}

void FrameTaskManager::WaitForAll() {
    std::unique_lock<std::mutex> lock(_mutex);
    // Process frames while holding lock to avoid copying
    for (auto& frame : _pendingFrames) {
        // Need to unlock while waiting
        auto futures = std::move(frame.futures);
        lock.unlock();

        for (auto& future : futures) {
            future.wait();
        }

        lock.lock();
    }
    _pendingFrames.clear();
}

bool FrameTaskManager::IsFrameComplete(uint64_t frameId) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    for (auto& frame : _pendingFrames) {
        if (frame.frameId == frameId) {
            for (auto& future : frame.futures) {
                if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    return false;
                }
            }
            return true;
        }
    }
    
    // Frame not found, assume complete
    return true;
}

} // namespace OptiScaler
