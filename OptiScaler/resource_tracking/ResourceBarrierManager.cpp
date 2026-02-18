#include "pch.h"
#include "ResourceBarrierManager.h"

ResourceBarrierManager::ResourceBarrierManager(bool autoFlush)
    : m_autoFlush(autoFlush)
{
    m_barriers.reserve(MAX_BARRIER_BATCH_SIZE);
}

void ResourceBarrierManager::AddTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                                   D3D12_RESOURCE_STATES after)
{
    if (resource == nullptr)
        return;

    // Skip no-op barriers
    if (before == after)
        return;

    // If batch is full, expand capacity (auto-flush without command list is not possible)
    if (m_barriers.size() >= m_barriers.capacity())
    {
        m_barriers.reserve(m_barriers.capacity() + MAX_BARRIER_BATCH_SIZE);
    }

    // Create the barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_barriers.push_back(barrier);
}

void ResourceBarrierManager::AddUAVBarrier(ID3D12Resource* resource)
{
    // If batch is full, expand capacity
    if (m_barriers.size() >= m_barriers.capacity())
    {
        m_barriers.reserve(m_barriers.capacity() + MAX_BARRIER_BATCH_SIZE);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;

    m_barriers.push_back(barrier);
}

void ResourceBarrierManager::AddAliasingBarrier(ID3D12Resource* resourceBefore, ID3D12Resource* resourceAfter)
{
    // If batch is full, expand capacity
    if (m_barriers.size() >= m_barriers.capacity())
    {
        m_barriers.reserve(m_barriers.capacity() + MAX_BARRIER_BATCH_SIZE);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Aliasing.pResourceBefore = resourceBefore;
    barrier.Aliasing.pResourceAfter = resourceAfter;

    m_barriers.push_back(barrier);
}

void ResourceBarrierManager::FlushBarriers(ID3D12GraphicsCommandList* commandList)
{
    if (commandList == nullptr || m_barriers.empty())
        return;

    commandList->ResourceBarrier(static_cast<UINT>(m_barriers.size()), m_barriers.data());
    m_barriers.clear();
}

void ResourceBarrierManager::Reset()
{
    m_barriers.clear();
}

void ResourceBarrierManager::TransitionResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                                 D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (commandList == nullptr || resource == nullptr)
        return;

    // Skip no-op barriers
    if (before == after)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &barrier);
}

void ResourceBarrierManager::UAVBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource)
{
    if (commandList == nullptr)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;

    commandList->ResourceBarrier(1, &barrier);
}
