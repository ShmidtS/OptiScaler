#pragma once

#include <pch.h>
#include <d3d12.h>
#include <mutex>
#include <queue>
#include <vector>
#include <memory>

namespace OptiScaler {

// High-performance command list pool for DX12 frame generation
// Reduces allocation overhead and improves command list reuse
class CommandListPool {
public:
    struct PooledCommandList {
        ID3D12GraphicsCommandList* cmdList = nullptr;
        ID3D12CommandAllocator* allocator = nullptr;
        uint64_t fenceValue = 0;
        bool isReady = true;
    };

private:
    ID3D12Device* _device = nullptr;
    D3D12_COMMAND_LIST_TYPE _type;
    
    // Pool of available command lists
    std::vector<std::unique_ptr<PooledCommandList>> _available;
    std::vector<std::unique_ptr<PooledCommandList>> _inFlight;
    
    // Fence for tracking completion
    ID3D12Fence* _fence = nullptr;
    uint64_t _fenceValue = 0;
    HANDLE _fenceEvent = nullptr;
    
    // Synchronization
    std::mutex _mutex;
    
    // Stats for debugging
    size_t _totalAllocated = 0;
    size_t _peakInFlight = 0;

public:
    CommandListPool(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT);
    ~CommandListPool();

    // Non-copyable
    CommandListPool(const CommandListPool&) = delete;
    CommandListPool& operator=(const CommandListPool&) = delete;

    // Acquire a command list from the pool
    PooledCommandList* Acquire();
    
    // Release a command list back to the pool (mark for reuse when GPU finishes)
    void Release(PooledCommandList* pooled);
    
    // Wait for all in-flight command lists to complete
    void WaitForAll();
    
    // Trim pool to reduce memory usage
    void Trim(size_t targetSize);
    
    // Get statistics
    size_t GetAvailableCount() const { return _available.size(); }
    size_t GetInFlightCount() const { return _inFlight.size(); }
    size_t GetTotalAllocated() const { return _totalAllocated; }
    size_t GetPeakInFlight() const { return _peakInFlight; }

private:
    PooledCommandList* CreateNewCommandList();
    void RetireCompletedLists();
};

// RAII wrapper for pooled command lists
class ScopedCommandList {
private:
    CommandListPool* _pool = nullptr;
    CommandListPool::PooledCommandList* _pooled = nullptr;

public:
    ScopedCommandList(CommandListPool* pool) : _pool(pool) {
        if (_pool) {
            _pooled = _pool->Acquire();
        }
    }
    
    ~ScopedCommandList() {
        if (_pool && _pooled) {
            _pool->Release(_pooled);
        }
    }
    
    // Non-copyable
    ScopedCommandList(const ScopedCommandList&) = delete;
    ScopedCommandList& operator=(const ScopedCommandList&) = delete;
    
    // Movable
    ScopedCommandList(ScopedCommandList&& other) noexcept 
        : _pool(other._pool), _pooled(other._pooled) {
        other._pool = nullptr;
        other._pooled = nullptr;
    }
    
    ScopedCommandList& operator=(ScopedCommandList&& other) noexcept {
        if (this != &other) {
            if (_pool && _pooled) {
                _pool->Release(_pooled);
            }
            _pool = other._pool;
            _pooled = other._pooled;
            other._pool = nullptr;
            other._pooled = nullptr;
        }
        return *this;
    }

    ID3D12GraphicsCommandList* Get() const { return _pooled ? _pooled->cmdList : nullptr; }
    ID3D12CommandAllocator* GetAllocator() const { return _pooled ? _pooled->allocator : nullptr; }
    
    explicit operator bool() const { return _pooled != nullptr; }
    ID3D12GraphicsCommandList* operator->() const { return Get(); }
};

} // namespace OptiScaler
