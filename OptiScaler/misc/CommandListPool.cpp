#include "CommandListPool.h"

namespace OptiScaler {

CommandListPool::CommandListPool(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
    : _device(device), _type(type) {
    
    if (!_device) {
        LOG_ERROR("CommandListPool created with null device!");
        return;
    }

    // Create fence for tracking completion
    HRESULT hr = _device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create fence for CommandListPool: {:X}", (UINT)hr);
        return;
    }

    // Create event for fence
    _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!_fenceEvent) {
        LOG_ERROR("Failed to create fence event for CommandListPool");
        _fence->Release();
        _fence = nullptr;
        return;
    }

    LOG_DEBUG("CommandListPool created with type {}", (int)type);
}

CommandListPool::~CommandListPool() {
    // Wait for all in-flight lists to complete
    WaitForAll();

    // Clean up available lists
    for (auto& pooled : _available) {
        if (pooled->cmdList) {
            pooled->cmdList->Release();
        }
        if (pooled->allocator) {
            pooled->allocator->Release();
        }
    }
    _available.clear();

    // Clean up fence
    if (_fenceEvent) {
        CloseHandle(_fenceEvent);
    }
    if (_fence) {
        _fence->Release();
    }

    LOG_DEBUG("CommandListPool destroyed, total allocated: {}", _totalAllocated);
}

CommandListPool::PooledCommandList* CommandListPool::CreateNewCommandList() {
    auto pooled = std::make_unique<PooledCommandList>();

    // Create command allocator
    HRESULT hr = _device->CreateCommandAllocator(_type, IID_PPV_ARGS(&pooled->allocator));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create command allocator: {:X}", (UINT)hr);
        return nullptr;
    }

    // Create command list
    hr = _device->CreateCommandList(0, _type, pooled->allocator, nullptr, IID_PPV_ARGS(&pooled->cmdList));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create command list: {:X}", (UINT)hr);
        pooled->allocator->Release();
        return nullptr;
    }

    // Close the list initially
    pooled->cmdList->Close();

    _totalAllocated++;
    LOG_TRACE("Created new command list, total: {}", _totalAllocated);

    return pooled.release();
}

void CommandListPool::RetireCompletedLists() {
    if (_inFlight.empty()) return;

    uint64_t completedValue = _fence->GetCompletedValue();

    auto it = _inFlight.begin();
    while (it != _inFlight.end()) {
        if ((*it)->fenceValue <= completedValue) {
            // List is complete, reset allocator and move to available
            (*it)->allocator->Reset();
            (*it)->isReady = true;
            _available.push_back(std::move(*it));
            it = _inFlight.erase(it);
        } else {
            ++it;
        }
    }
}

CommandListPool::PooledCommandList* CommandListPool::Acquire() {
    std::lock_guard<std::mutex> lock(_mutex);

    // First, try to retire completed lists
    RetireCompletedLists();

    // Try to get from available pool
    if (!_available.empty()) {
        auto pooled = std::move(_available.back());
        _available.pop_back();
        
        // Reset the command list
        pooled->cmdList->Reset(pooled->allocator, nullptr);
        pooled->isReady = false;
        
        _inFlight.push_back(std::move(pooled));
        
        if (_inFlight.size() > _peakInFlight) {
            _peakInFlight = _inFlight.size();
        }
        
        return _inFlight.back().get();
    }

    // Create new command list
    auto* pooled = CreateNewCommandList();
    if (pooled) {
        pooled->isReady = false;
        _inFlight.push_back(std::unique_ptr<PooledCommandList>(pooled));
        
        if (_inFlight.size() > _peakInFlight) {
            _peakInFlight = _inFlight.size();
        }
    }

    return pooled;
}

void CommandListPool::Release(PooledCommandList* pooled) {
    if (!pooled) return;

    std::lock_guard<std::mutex> lock(_mutex);

    // Close the command list
    pooled->cmdList->Close();

    // Assign fence value
    _fenceValue++;
    pooled->fenceValue = _fenceValue;
}

void CommandListPool::WaitForAll() {
    if (!_fence || _inFlight.empty()) return;

    // Signal fence with current value
    _fenceValue++;
    
    // Wait for all in-flight lists
    uint64_t lastValue = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (const auto& pooled : _inFlight) {
            if (pooled->fenceValue > lastValue) {
                lastValue = pooled->fenceValue;
            }
        }
    }

    if (lastValue > 0) {
        uint64_t completedValue = _fence->GetCompletedValue();
        if (completedValue < lastValue) {
            _fence->SetEventOnCompletion(lastValue, _fenceEvent);
            WaitForSingleObject(_fenceEvent, INFINITE);
        }
    }

    // Retire all lists
    std::lock_guard<std::mutex> lock(_mutex);
    RetireCompletedLists();
}

void CommandListPool::Trim(size_t targetSize) {
    std::lock_guard<std::mutex> lock(_mutex);

    // Retire completed lists first
    RetireCompletedLists();

    // Remove excess available lists
    while (_available.size() > targetSize) {
        auto& pooled = _available.back();
        if (pooled->cmdList) {
            pooled->cmdList->Release();
        }
        if (pooled->allocator) {
            pooled->allocator->Release();
        }
        _available.pop_back();
    }

    LOG_DEBUG("CommandListPool trimmed to {} available lists", _available.size());
}

} // namespace OptiScaler
