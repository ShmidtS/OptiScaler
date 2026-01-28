#include "VulkanSyncOptimizer.h"

namespace OptiScaler {

VulkanSyncOptimizer::VulkanSyncOptimizer() = default;

VulkanSyncOptimizer::~VulkanSyncOptimizer() {
    Shutdown();
}

bool VulkanSyncOptimizer::Initialize(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex) {
    _device = device;
    _queue = queue;
    _queueFamilyIndex = queueFamilyIndex;
    
    // Pre-allocate fence pool
    _fencePool.reserve(_maxInFlight);
    for (size_t i = 0; i < _maxInFlight; ++i) {
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled
        
        FenceEntry entry;
        VkResult result = vkCreateFence(_device, &fenceInfo, nullptr, &entry.fence);
        if (result != VK_SUCCESS) {
            LOG_ERROR("VulkanSyncOptimizer: Failed to create fence: {}", static_cast<int>(result));
            Shutdown();
            return false;
        }
        
        entry.submitId = 0;
        entry.isSignaled = true;
        _fencePool.push_back(entry);
    }
    
    // Try to create timeline semaphore
    if (CreateTimelineSemaphore() == VK_SUCCESS) {
        _useTimelineSemaphores = true;
        LOG_INFO("VulkanSyncOptimizer: Using timeline semaphores");
    }
    
    LOG_INFO("VulkanSyncOptimizer: Initialized with {} fences", _fencePool.size());
    return true;
}

void VulkanSyncOptimizer::Shutdown() {
    if (_device == VK_NULL_HANDLE) return;
    
    // Wait for all in-flight work
    WaitForAll();
    
    // Cleanup timeline semaphore
    if (_timelineSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(_device, _timelineSemaphore, nullptr);
        _timelineSemaphore = VK_NULL_HANDLE;
    }
    
    // Cleanup fences
    for (auto& entry : _fencePool) {
        if (entry.fence != VK_NULL_HANDLE) {
            vkDestroyFence(_device, entry.fence, nullptr);
            entry.fence = VK_NULL_HANDLE;
        }
    }
    _fencePool.clear();
    _inFlight.clear();
    
    _device = VK_NULL_HANDLE;
    _queue = VK_NULL_HANDLE;
}

VkResult VulkanSyncOptimizer::CreateTimelineSemaphore() {
    // Check if timeline semaphores are supported
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = {};
    timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &timelineFeatures;
    
    // We can't query features without instance, assume supported for now
    // In production, check during device creation
    
    VkSemaphoreTypeCreateInfo timelineCreateInfo = {};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;
    
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    
    return vkCreateSemaphore(_device, &createInfo, nullptr, &_timelineSemaphore);
}

VulkanSyncOptimizer::FenceEntry* VulkanSyncOptimizer::AcquireFence() {
    RetireCompletedFences();
    
    for (auto& entry : _fencePool) {
        if (entry.isSignaled) {
            entry.isSignaled = false;
            return &entry;
        }
    }
    
    // All fences in use, create a new one
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    FenceEntry entry;
    VkResult result = vkCreateFence(_device, &fenceInfo, nullptr, &entry.fence);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VulkanSyncOptimizer: Failed to create additional fence");
        return nullptr;
    }
    
    _fencePool.push_back(entry);
    _fencePool.back().isSignaled = false;
    return &_fencePool.back();
}

void VulkanSyncOptimizer::ReleaseFence(FenceEntry* entry) {
    // Reset fence for reuse
    vkResetFences(_device, 1, &entry->fence);
    entry->isSignaled = false;
}

void VulkanSyncOptimizer::RetireCompletedFences() {
    for (auto* entry : _inFlight) {
        if (!entry->isSignaled) {
            VkResult result = vkGetFenceStatus(_device, entry->fence);
            if (result == VK_SUCCESS) {
                entry->isSignaled = true;
                _completedCounter = std::max(_completedCounter, entry->submitId);
            }
        }
    }
    
    // Remove completed entries from in-flight
    auto it = _inFlight.begin();
    while (it != _inFlight.end()) {
        if ((*it)->isSignaled) {
            it = _inFlight.erase(it);
        } else {
            ++it;
        }
    }
}

VkResult VulkanSyncOptimizer::Submit(VkCommandBuffer* cmdBuffers, uint32_t cmdBufferCount,
                                     VkSemaphore* waitSemaphores, uint32_t waitSemaphoreCount,
                                     VkPipelineStageFlags* waitStages,
                                     VkSemaphore* signalSemaphores, uint32_t signalSemaphoreCount) {
    if (_device == VK_NULL_HANDLE || _queue == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Retire completed fences
    RetireCompletedFences();
    
    // Acquire a fence for this submission
    FenceEntry* fence = AcquireFence();
    if (!fence) {
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    
    // Prepare submit info
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = cmdBufferCount;
    submitInfo.pCommandBuffers = cmdBuffers;
    submitInfo.waitSemaphoreCount = waitSemaphoreCount;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.signalSemaphoreCount = signalSemaphoreCount;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    // Submit
    VkResult result = vkQueueSubmit(_queue, 1, &submitInfo, fence->fence);
    if (result != VK_SUCCESS) {
        LOG_ERROR("VulkanSyncOptimizer: Queue submit failed: {}", static_cast<int>(result));
        ReleaseFence(fence);
        return result;
    }
    
    // Track submission
    _submitCounter++;
    fence->submitId = _submitCounter;
    _inFlight.push_back(fence);
    
    return VK_SUCCESS;
}

bool VulkanSyncOptimizer::IsSubmitComplete(uint64_t submitId) {
    std::lock_guard<std::mutex> lock(_mutex);
    RetireCompletedFences();
    return submitId <= _completedCounter;
}

VkResult VulkanSyncOptimizer::WaitForSubmit(uint64_t submitId, uint64_t timeoutNs) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Check if already complete
    if (submitId <= _completedCounter) {
        return VK_SUCCESS;
    }
    
    // Find the fence for this submit
    FenceEntry* targetFence = nullptr;
    for (auto* entry : _inFlight) {
        if (entry->submitId == submitId) {
            targetFence = entry;
            break;
        }
    }
    
    if (!targetFence) {
        // Already completed or invalid submit ID
        return VK_SUCCESS;
    }
    
    // Wait for the fence
    VkResult result = vkWaitForFences(_device, 1, &targetFence->fence, VK_TRUE, timeoutNs);
    
    if (result == VK_SUCCESS) {
        targetFence->isSignaled = true;
        RetireCompletedFences();
    }
    
    return result;
}

VkResult VulkanSyncOptimizer::WaitForAll(uint64_t timeoutNs) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    if (_inFlight.empty()) {
        return VK_SUCCESS;
    }
    
    // Collect all fences
    std::vector<VkFence> fences;
    fences.reserve(_inFlight.size());
    for (auto* entry : _inFlight) {
        if (!entry->isSignaled) {
            fences.push_back(entry->fence);
        }
    }
    
    if (fences.empty()) {
        return VK_SUCCESS;
    }
    
    // Wait for all fences
    VkResult result = vkWaitForFences(_device, static_cast<uint32_t>(fences.size()), 
                                       fences.data(), VK_TRUE, timeoutNs);
    
    if (result == VK_SUCCESS) {
        for (auto* entry : _inFlight) {
            entry->isSignaled = true;
        }
        _completedCounter = _submitCounter;
        _inFlight.clear();
    }
    
    return result;
}

VkResult VulkanSyncOptimizer::WaitForPreviousFrame(uint32_t framesBack) {
    std::lock_guard<std::mutex> lock(_mutex);
    
    if (_submitCounter <= framesBack) {
        return VK_SUCCESS;
    }
    
    uint64_t targetSubmitId = _submitCounter - framesBack;
    
    // Find fence for target submit
    FenceEntry* targetFence = nullptr;
    for (auto* entry : _inFlight) {
        if (entry->submitId == targetSubmitId) {
            targetFence = entry;
            break;
        }
    }
    
    if (!targetFence || targetFence->isSignaled) {
        return VK_SUCCESS;
    }
    
    return vkWaitForFences(_device, 1, &targetFence->fence, VK_TRUE, _timeoutNs);
}

bool VulkanSyncOptimizer::IsQueueIdle() {
    std::lock_guard<std::mutex> lock(_mutex);
    RetireCompletedFences();
    return _inFlight.empty();
}

size_t VulkanSyncOptimizer::GetInFlightCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _inFlight.size();
}

// VulkanPresentOptimizer implementation

bool VulkanPresentOptimizer::Initialize(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    _physicalDevice = physicalDevice;
    _surface = surface;
    
    QueryPresentModes();
    
    return !_availableModes.empty();
}

void VulkanPresentOptimizer::QueryPresentModes() {
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface, &modeCount, nullptr);
    
    if (modeCount == 0) {
        return;
    }
    
    _availableModes.resize(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(_physicalDevice, _surface, &modeCount, _availableModes.data());
    
    LOG_INFO("VulkanPresentOptimizer: {} present modes available", modeCount);
}

VkPresentModeKHR VulkanPresentOptimizer::SelectOptimalMode(bool preferLowLatency, bool allowTearing, bool requireVSync) {
    // Priority order based on requirements
    std::vector<VkPresentModeKHR> priorityOrder;
    
    if (requireVSync) {
        // VSync required - prefer FIFO
        if (_adaptiveVSync) {
            priorityOrder = {
                VK_PRESENT_MODE_FIFO_RELAXED_KHR,  // Adaptive VSync
                VK_PRESENT_MODE_FIFO_KHR           // Standard VSync
            };
        } else {
            priorityOrder = {
                VK_PRESENT_MODE_FIFO_KHR,
                VK_PRESENT_MODE_FIFO_RELAXED_KHR
            };
        }
    } else if (preferLowLatency) {
        // Low latency preferred
        if (allowTearing) {
            priorityOrder = {
                VK_PRESENT_MODE_IMMEDIATE_KHR,     // Lowest latency, may tear
                VK_PRESENT_MODE_MAILBOX_KHR,       // Low latency, no tearing
                VK_PRESENT_MODE_FIFO_RELAXED_KHR,
                VK_PRESENT_MODE_FIFO_KHR
            };
        } else {
            priorityOrder = {
                VK_PRESENT_MODE_MAILBOX_KHR,       // Low latency, no tearing
                VK_PRESENT_MODE_FIFO_RELAXED_KHR,
                VK_PRESENT_MODE_FIFO_KHR,
                VK_PRESENT_MODE_IMMEDIATE_KHR
            };
        }
    } else {
        // Balanced
        priorityOrder = {
            VK_PRESENT_MODE_MAILBOX_KHR,
            VK_PRESENT_MODE_FIFO_KHR,
            VK_PRESENT_MODE_FIFO_RELAXED_KHR,
            VK_PRESENT_MODE_IMMEDIATE_KHR
        };
    }
    
    // Find first available mode in priority order
    for (auto mode : priorityOrder) {
        if (IsModeAvailable(mode)) {
            _currentMode = mode;
            LOG_INFO("VulkanPresentOptimizer: Selected present mode: {}", 
                     GetPresentModeInfo(mode).name);
            return mode;
        }
    }
    
    // Fallback to FIFO (always available)
    _currentMode = VK_PRESENT_MODE_FIFO_KHR;
    return _currentMode;
}

bool VulkanPresentOptimizer::IsModeAvailable(VkPresentModeKHR mode) const {
    for (auto available : _availableModes) {
        if (available == mode) {
            return true;
        }
    }
    return false;
}

VulkanPresentOptimizer::PresentModeInfo VulkanPresentOptimizer::GetPresentModeInfo(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return {mode, "IMMEDIATE", true, false, 0};
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return {mode, "MAILBOX", false, false, 1};
        case VK_PRESENT_MODE_FIFO_KHR:
            return {mode, "FIFO", false, true, 1};
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return {mode, "FIFO_RELAXED", true, true, 1};
        case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR:
            return {mode, "SHARED_DEMAND", false, false, 0};
        case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR:
            return {mode, "SHARED_CONTINUOUS", false, false, 0};
        default:
            return {mode, "UNKNOWN", false, false, 1};
    }
}

void VulkanPresentOptimizer::UpdateFrameTiming(double frameTimeMs) {
    _frameTimes.push_back(frameTimeMs);
    if (_frameTimes.size() > MAX_FRAME_TIME_SAMPLES) {
        _frameTimes.pop_front();
    }
    
    if (!_adaptiveVSync || _frameTimes.size() < 3) {
        return;
    }
    
    // Calculate average frame time
    double avgFrameTime = 0.0;
    for (auto ft : _frameTimes) {
        avgFrameTime += ft;
    }
    avgFrameTime /= _frameTimes.size();
    
    // Detect late/early frames
    // Assuming 16.67ms target for 60Hz
    const double targetFrameTime = 16.67;
    
    if (avgFrameTime > targetFrameTime * 1.1) {
        _consecutiveLateFrames++;
        _consecutiveEarlyFrames = 0;
    } else if (avgFrameTime < targetFrameTime * 0.9) {
        _consecutiveEarlyFrames++;
        _consecutiveLateFrames = 0;
    } else {
        _consecutiveLateFrames = 0;
        _consecutiveEarlyFrames = 0;
    }
}

bool VulkanPresentOptimizer::ShouldSwitchPresentMode(VkPresentModeKHR& suggestedMode) {
    if (!_adaptiveVSync) {
        return false;
    }
    
    const uint32_t CONSECUTIVE_THRESHOLD = 3;
    
    if (_consecutiveLateFrames >= CONSECUTIVE_THRESHOLD) {
        // Running late - consider disabling VSync if currently using FIFO
        if (_currentMode == VK_PRESENT_MODE_FIFO_KHR || 
            _currentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
            if (IsModeAvailable(VK_PRESENT_MODE_MAILBOX_KHR)) {
                suggestedMode = VK_PRESENT_MODE_MAILBOX_KHR;
                return true;
            } else if (IsModeAvailable(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
                suggestedMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                return true;
            }
        }
    } else if (_consecutiveEarlyFrames >= CONSECUTIVE_THRESHOLD) {
        // Running early - can enable VSync for smoother experience
        if (_currentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ||
            _currentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            if (IsModeAvailable(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) {
                suggestedMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
                return true;
            } else if (IsModeAvailable(VK_PRESENT_MODE_FIFO_KHR)) {
                suggestedMode = VK_PRESENT_MODE_FIFO_KHR;
                return true;
            }
        }
    }
    
    return false;
}

} // namespace OptiScaler
