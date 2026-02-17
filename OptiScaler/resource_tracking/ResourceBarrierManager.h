#pragma once

#include "SysUtils.h"

#include <d3d12.h>
#include <vector>
#include <unordered_map>

// Maximum number of barriers that can be batched together
constexpr size_t MAX_BARRIER_BATCH_SIZE = 64;

// Cache key for frequently used barriers
struct BarrierCacheKey
{
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES beforeState;
    D3D12_RESOURCE_STATES afterState;

    bool operator==(const BarrierCacheKey& other) const
    {
        return resource == other.resource && beforeState == other.beforeState && afterState == other.afterState;
    }
};

// Hash function for BarrierCacheKey
struct BarrierCacheKeyHash
{
    size_t operator()(const BarrierCacheKey& key) const
    {
        size_t h1 = std::hash<ID3D12Resource*>{}(key.resource);
        size_t h2 = std::hash<UINT>{}(static_cast<UINT>(key.beforeState));
        size_t h3 = std::hash<UINT>{}(static_cast<UINT>(key.afterState));
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// Resource Barrier Manager for batching and caching D3D12 resource barriers
// This class optimizes barrier submission by batching multiple barriers together
// and caching frequently used barrier configurations.
class ResourceBarrierManager
{
private:
    std::vector<D3D12_RESOURCE_BARRIER> m_barriers;
    std::unordered_map<BarrierCacheKey, D3D12_RESOURCE_BARRIER, BarrierCacheKeyHash> m_cache;
    bool m_autoFlush;

public:
    ResourceBarrierManager(bool autoFlush = true);
    ~ResourceBarrierManager() = default;

    // Prevent copying
    ResourceBarrierManager(const ResourceBarrierManager&) = delete;
    ResourceBarrierManager& operator=(const ResourceBarrierManager&) = delete;

    // Add a transition barrier to the batch
    // If auto-flush is enabled and batch is full, barriers will be flushed automatically
    void AddTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    // Add a UAV barrier to the batch
    void AddUAVBarrier(ID3D12Resource* resource);

    // Add an aliasing barrier to the batch
    void AddAliasingBarrier(ID3D12Resource* resourceBefore, ID3D12Resource* resourceAfter);

    // Flush all pending barriers to the command list
    void FlushBarriers(ID3D12GraphicsCommandList* commandList);

    // Clear all pending barriers without flushing
    void Reset();

    // Get the current number of pending barriers
    size_t GetPendingBarrierCount() const { return m_barriers.size(); }

    // Check if there are any pending barriers
    bool HasPendingBarriers() const { return !m_barriers.empty(); }

    // Clear the barrier cache
    void ClearCache() { m_cache.clear(); }

    // Set auto-flush mode
    void SetAutoFlush(bool autoFlush) { m_autoFlush = autoFlush; }

    // Static helper for single barrier submission (convenience method)
    static void TransitionResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                   D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);

    // Static helper for single UAV barrier (convenience method)
    static void UAVBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource);
};
