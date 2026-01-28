#pragma once

#include <pch.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>

namespace OptiScaler {

// Vulkan synchronization optimizer
// Replaces vkQueueWaitIdle with efficient fence-based synchronization
class VulkanSyncOptimizer {
private:
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _queue = VK_NULL_HANDLE;
    uint32_t _queueFamilyIndex = UINT32_MAX;
    
    // Fence pool for tracking submissions
    struct FenceEntry {
        VkFence fence = VK_NULL_HANDLE;
        uint64_t submitId = 0;
        bool isSignaled = false;
    };
    
    std::vector<FenceEntry> _fencePool;
    std::deque<FenceEntry*> _inFlight;
    mutable std::mutex _mutex;
    
    uint64_t _submitCounter = 0;
    uint64_t _completedCounter = 0;
    
    // Timeline semaphore support
    bool _useTimelineSemaphores = false;
    VkSemaphore _timelineSemaphore = VK_NULL_HANDLE;
    uint64_t _timelineValue = 0;
    
    // Configuration
    uint64_t _timeoutNs = 1000000000ULL; // 1 second default timeout
    size_t _maxInFlight = 8;

public:
    VulkanSyncOptimizer();
    ~VulkanSyncOptimizer();

    // Non-copyable
    VulkanSyncOptimizer(const VulkanSyncOptimizer&) = delete;
    VulkanSyncOptimizer& operator=(const VulkanSyncOptimizer&) = delete;

    // Initialize with device and queue
    bool Initialize(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex);
    
    // Shutdown and cleanup
    void Shutdown();
    
    // Submit command buffers with tracking
    VkResult Submit(VkCommandBuffer* cmdBuffers, uint32_t cmdBufferCount, 
                    VkSemaphore* waitSemaphores, uint32_t waitSemaphoreCount,
                    VkPipelineStageFlags* waitStages,
                    VkSemaphore* signalSemaphores, uint32_t signalSemaphoreCount);
    
    // Wait for a specific submission to complete (non-blocking check)
    bool IsSubmitComplete(uint64_t submitId);
    
    // Wait for a specific submission to complete (with timeout)
    VkResult WaitForSubmit(uint64_t submitId, uint64_t timeoutNs);
    
    // Wait for all in-flight submissions
    VkResult WaitForAll(uint64_t timeoutNs = UINT64_MAX);
    
    // Wait for previous frame (useful for double/triple buffering)
    VkResult WaitForPreviousFrame(uint32_t framesBack = 2);
    
    // Non-blocking check if queue is idle
    bool IsQueueIdle();
    
    // Get statistics
    uint64_t GetSubmitCounter() const { return _submitCounter; }
    uint64_t GetCompletedCounter() const { return _completedCounter; }
    size_t GetInFlightCount() const;
    
    // Set configuration
    void SetTimeout(uint64_t timeoutNs) { _timeoutNs = timeoutNs; }
    void SetMaxInFlight(size_t maxInFlight) { _maxInFlight = maxInFlight; }

private:
    FenceEntry* AcquireFence();
    void ReleaseFence(FenceEntry* entry);
    void RetireCompletedFences();
    VkResult CreateTimelineSemaphore();
};

// RAII wrapper for scoped command buffer submission
class ScopedVulkanSubmit {
private:
    VulkanSyncOptimizer* _optimizer = nullptr;
    uint64_t _submitId = 0;
    bool _waitOnDestruct = false;

public:
    ScopedVulkanSubmit() = default;
    ScopedVulkanSubmit(VulkanSyncOptimizer* optimizer, uint64_t submitId, bool waitOnDestruct = false)
        : _optimizer(optimizer), _submitId(submitId), _waitOnDestruct(waitOnDestruct) {}
    
    ~ScopedVulkanSubmit() {
        if (_waitOnDestruct && _optimizer) {
            _optimizer->WaitForSubmit(_submitId, UINT64_MAX);
        }
    }
    
    // Non-copyable
    ScopedVulkanSubmit(const ScopedVulkanSubmit&) = delete;
    ScopedVulkanSubmit& operator=(const ScopedVulkanSubmit&) = delete;
    
    // Movable
    ScopedVulkanSubmit(ScopedVulkanSubmit&& other) noexcept
        : _optimizer(other._optimizer), _submitId(other._submitId), _waitOnDestruct(other._waitOnDestruct) {
        other._optimizer = nullptr;
        other._submitId = 0;
    }
    
    ScopedVulkanSubmit& operator=(ScopedVulkanSubmit&& other) noexcept {
        if (this != &other) {
            if (_waitOnDestruct && _optimizer) {
                _optimizer->WaitForSubmit(_submitId, UINT64_MAX);
            }
            _optimizer = other._optimizer;
            _submitId = other._submitId;
            _waitOnDestruct = other._waitOnDestruct;
            other._optimizer = nullptr;
            other._submitId = 0;
        }
        return *this;
    }
    
    uint64_t GetSubmitId() const { return _submitId; }
    bool IsComplete() const { return _optimizer ? _optimizer->IsSubmitComplete(_submitId) : true; }
    VkResult Wait(uint64_t timeoutNs = UINT64_MAX) {
        return _optimizer ? _optimizer->WaitForSubmit(_submitId, timeoutNs) : VK_SUCCESS;
    }
};

// Present mode optimizer
class VulkanPresentOptimizer {
public:
    struct PresentModeInfo {
        VkPresentModeKHR mode;
        const char* name;
        bool supportsTearing;
        bool supportsVSync;
        uint32_t latencyFrames;
    };

private:
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    
    std::vector<VkPresentModeKHR> _availableModes;
    VkPresentModeKHR _currentMode = VK_PRESENT_MODE_FIFO_KHR;
    
    // Adaptive VSync state
    bool _adaptiveVSync = false;
    uint32_t _consecutiveLateFrames = 0;
    uint32_t _consecutiveEarlyFrames = 0;
    
    // Frame timing
    std::deque<double> _frameTimes;
    static constexpr size_t MAX_FRAME_TIME_SAMPLES = 10;

public:
    // Initialize with physical device and surface
    bool Initialize(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
    
    // Select optimal present mode based on requirements
    VkPresentModeKHR SelectOptimalMode(bool preferLowLatency, bool allowTearing, bool requireVSync);
    
    // Set present mode
    void SetPresentMode(VkPresentModeKHR mode) { _currentMode = mode; }
    VkPresentModeKHR GetPresentMode() const { return _currentMode; }
    
    // Adaptive VSync
    void SetAdaptiveVSync(bool enabled) { _adaptiveVSync = enabled; }
    bool IsAdaptiveVSyncEnabled() const { return _adaptiveVSync; }
    
    // Update frame timing for adaptive VSync
    void UpdateFrameTiming(double frameTimeMs);
    
    // Check if we should switch present mode
    bool ShouldSwitchPresentMode(VkPresentModeKHR& suggestedMode);
    
    // Get available modes
    const std::vector<VkPresentModeKHR>& GetAvailableModes() const { return _availableModes; }
    
    // Check if mode is available
    bool IsModeAvailable(VkPresentModeKHR mode) const;
    
    // Get present mode info
    static PresentModeInfo GetPresentModeInfo(VkPresentModeKHR mode);

private:
    void QueryPresentModes();
};

} // namespace OptiScaler
