#pragma once

#include <pch.h>
#include <chrono>
#include <atomic>

namespace OptiScaler {

// High-resolution timer using QueryPerformanceCounter
// Provides microsecond and nanosecond precision for frame pacing
class HighResTimer {
private:
    static inline LARGE_INTEGER s_frequency;
    static inline bool s_initialized = false;
    static inline std::atomic<uint64_t> s_frameCounter{0};

public:
    // Initialize the timer (called automatically on first use)
    static void Initialize() {
        if (!s_initialized) {
            QueryPerformanceFrequency(&s_frequency);
            s_initialized = true;
        }
    }

    // Get current time in microseconds
    static inline double MicrosecondsNow() {
        if (!s_initialized) Initialize();
        
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (double)(now.QuadPart * 1000000.0) / (double)s_frequency.QuadPart;
    }

    // Get current time in nanoseconds
    static inline double NanosecondsNow() {
        if (!s_initialized) Initialize();
        
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (double)(now.QuadPart * 1000000000.0) / (double)s_frequency.QuadPart;
    }

    // Get current time in milliseconds (higher precision than Util::MillisecondsNow)
    static inline double MillisecondsNowPrecise() {
        if (!s_initialized) Initialize();
        
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (double)(now.QuadPart * 1000.0) / (double)s_frequency.QuadPart;
    }

    // Get raw counter value
    static inline int64_t GetCounter() {
        if (!s_initialized) Initialize();
        
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now.QuadPart;
    }

    // Convert counter difference to microseconds
    static inline double CounterToMicroseconds(int64_t counterDelta) {
        if (!s_initialized) Initialize();
        return (double)(counterDelta * 1000000.0) / (double)s_frequency.QuadPart;
    }

    // Convert counter difference to milliseconds
    static inline double CounterToMilliseconds(int64_t counterDelta) {
        if (!s_initialized) Initialize();
        return (double)(counterDelta * 1000.0) / (double)s_frequency.QuadPart;
    }

    // Get timer frequency
    static inline int64_t GetFrequency() {
        if (!s_initialized) Initialize();
        return s_frequency.QuadPart;
    }

    // Increment and get frame counter
    static inline uint64_t IncrementFrame() {
        return ++s_frameCounter;
    }

    // Get current frame counter
    static inline uint64_t GetFrame() {
        return s_frameCounter.load();
    }
};

// Adaptive frame pacer for smooth frame generation
class AdaptiveFramePacer {
private:
    double _targetFrameTimeUs = 0.0;      // Target frame time in microseconds
    double _currentFrameTimeUs = 0.0;     // Smoothed actual frame time
    double _alpha = 0.9;                  // Smoothing factor (0.0 - 1.0, higher = more smoothing)
    double _accumulatedErrorUs = 0.0;     // Accumulated timing error
    double _maxErrorUs = 500.0;           // Maximum accumulated error (500us)
    
    int64_t _lastFrameCounter = 0;
    bool _initialized = false;

public:
    // Initialize with target FPS
    void Initialize(double targetFps) {
        _targetFrameTimeUs = 1000000.0 / targetFps;
        _currentFrameTimeUs = _targetFrameTimeUs;
        _accumulatedErrorUs = 0.0;
        _initialized = true;
        
        LOG_INFO("AdaptiveFramePacer initialized: target {:.2f} FPS ({:.2f} us/frame)", 
                 targetFps, _targetFrameTimeUs);
    }

    // Update with actual frame time
    void Update(double actualFrameTimeUs) {
        if (!_initialized) return;

        // Exponential moving average for smoothing
        _currentFrameTimeUs = _alpha * _currentFrameTimeUs + (1.0 - _alpha) * actualFrameTimeUs;

        // Accumulate error
        double error = actualFrameTimeUs - _targetFrameTimeUs;
        _accumulatedErrorUs += error;
        
        // Clamp accumulated error
        if (_accumulatedErrorUs > _maxErrorUs) _accumulatedErrorUs = _maxErrorUs;
        if (_accumulatedErrorUs < -_maxErrorUs) _accumulatedErrorUs = -_maxErrorUs;
    }

    // Get sleep duration to maintain target frame rate
    // Returns microseconds to sleep (can be negative if running behind)
    double GetSleepDurationUs() const {
        if (!_initialized) return 0.0;

        // Calculate how much we need to adjust
        double adjustment = _accumulatedErrorUs * 0.1; // 10% error correction per frame
        
        return _targetFrameTimeUs - _currentFrameTimeUs - adjustment;
    }

    // Get current smoothed frame time
    double GetCurrentFrameTimeUs() const { return _currentFrameTimeUs; }
    
    // Get target frame time
    double GetTargetFrameTimeUs() const { return _targetFrameTimeUs; }
    
    // Check if we're running behind target
    bool IsRunningBehind() const { return _currentFrameTimeUs > _targetFrameTimeUs * 1.05; }
    
    // Get performance ratio (1.0 = on target, >1.0 = slower, <1.0 = faster)
    double GetPerformanceRatio() const {
        if (!_initialized || _targetFrameTimeUs <= 0) return 1.0;
        return _currentFrameTimeUs / _targetFrameTimeUs;
    }

    // Reset accumulated error
    void ResetError() { _accumulatedErrorUs = 0.0; }
    
    // Set smoothing factor
    void SetSmoothingFactor(double alpha) { 
        _alpha = std::clamp(alpha, 0.0, 0.99); 
    }
};

// Frame time tracker with statistics
class FrameTimeTracker {
private:
    static constexpr size_t MAX_SAMPLES = 120; // 2 seconds at 60 FPS
    
    std::array<double, MAX_SAMPLES> _samples{};
    size_t _writeIndex = 0;
    size_t _sampleCount = 0;
    
    double _minTime = std::numeric_limits<double>::max();
    double _maxTime = 0.0;
    double _avgTime = 0.0;
    double _variance = 0.0;
    
    int64_t _lastCounter = 0;

public:
    // Add a new frame time sample (in microseconds)
    void AddSample(double frameTimeUs) {
        _samples[_writeIndex] = frameTimeUs;
        _writeIndex = (_writeIndex + 1) % MAX_SAMPLES;
        
        if (_sampleCount < MAX_SAMPLES) {
            _sampleCount++;
        }
        
        // Recalculate statistics
        CalculateStats();
    }
    
    // Start frame timing
    void BeginFrame() {
        _lastCounter = HighResTimer::GetCounter();
    }
    
    // End frame timing and add sample
    double EndFrame() {
        int64_t now = HighResTimer::GetCounter();
        double frameTimeUs = HighResTimer::CounterToMicroseconds(now - _lastCounter);
        AddSample(frameTimeUs);
        return frameTimeUs;
    }
    
    // Get statistics
    double GetMinTimeUs() const { return _minTime; }
    double GetMaxTimeUs() const { return _maxTime; }
    double GetAvgTimeUs() const { return _avgTime; }
    double GetVarianceUs() const { return _variance; }
    double GetStdDevUs() const { return std::sqrt(_variance); }
    
    // Get FPS from average frame time
    double GetAverageFps() const {
        if (_avgTime <= 0) return 0.0;
        return 1000000.0 / _avgTime;
    }
    
    // Get percentile frame time (0.0 - 1.0)
    double GetPercentileTimeUs(double percentile) const {
        if (_sampleCount == 0) return 0.0;
        
        std::vector<double> sorted(_samples.begin(), _samples.begin() + _sampleCount);
        std::sort(sorted.begin(), sorted.end());
        
        size_t index = static_cast<size_t>(percentile * (sorted.size() - 1));
        return sorted[index];
    }
    
    // Get 1% low FPS
    double Get1PercentLowFps() const {
        double p99Time = GetPercentileTimeUs(0.99);
        if (p99Time <= 0) return 0.0;
        return 1000000.0 / p99Time;
    }
    
    // Get 0.1% low FPS
    double GetPoint1PercentLowFps() const {
        double p999Time = GetPercentileTimeUs(0.999);
        if (p999Time <= 0) return 0.0;
        return 1000000.0 / p999Time;
    }

private:
    void CalculateStats() {
        if (_sampleCount == 0) return;
        
        double sum = 0.0;
        _minTime = std::numeric_limits<double>::max();
        _maxTime = 0.0;
        
        for (size_t i = 0; i < _sampleCount; i++) {
            double val = _samples[i];
            sum += val;
            if (val < _minTime) _minTime = val;
            if (val > _maxTime) _maxTime = val;
        }
        
        _avgTime = sum / _sampleCount;
        
        // Calculate variance
        double varSum = 0.0;
        for (size_t i = 0; i < _sampleCount; i++) {
            double diff = _samples[i] - _avgTime;
            varSum += diff * diff;
        }
        _variance = varSum / _sampleCount;
    }
};

} // namespace OptiScaler
