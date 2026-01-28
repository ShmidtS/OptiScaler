#pragma once

#include <pch.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

namespace OptiScaler {

// Resource descriptor for pooling
template<typename T>
struct ResourceDescriptor {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0;  // DXGI_FORMAT or VkFormat
    uint32_t flags = 0;
    
    bool operator==(const ResourceDescriptor& other) const {
        return width == other.width && 
               height == other.height && 
               format == other.format && 
               flags == other.flags;
    }
};

// Hash function for resource descriptors
template<typename T>
struct ResourceDescriptorHash {
    size_t operator()(const ResourceDescriptor<T>& desc) const {
        size_t hash = 0;
        hash ^= std::hash<uint32_t>{}(desc.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<uint32_t>{}(desc.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<uint32_t>{}(desc.format) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<uint32_t>{}(desc.flags) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

// Base resource pool interface
template<typename T, typename DescType>
class IResourcePool {
public:
    virtual ~IResourcePool() = default;
    virtual T Acquire(const DescType& desc) = 0;
    virtual void Release(T resource, const DescType& desc) = 0;
    virtual void Trim(size_t maxSize) = 0;
    virtual void Clear() = 0;
};

// DX12 Resource Pool
class D3D12ResourcePool : public IResourcePool<ID3D12Resource*, ResourceDescriptor<ID3D12Resource>> {
public:
    struct PooledResource {
        ID3D12Resource* resource = nullptr;
        uint64_t lastUsedFrame = 0;
        bool inUse = false;
    };

private:
    ID3D12Device* _device = nullptr;
    D3D12_RESOURCE_STATES _defaultState;
    
    std::unordered_map<ResourceDescriptor<ID3D12Resource>, std::vector<std::unique_ptr<PooledResource>>, 
                       ResourceDescriptorHash<ID3D12Resource>> _pools;
    std::mutex _mutex;
    
    uint64_t _currentFrame = 0;
    size_t _maxPoolSize = 32;
    size_t _totalAllocated = 0;
    size_t _totalReused = 0;

public:
    D3D12ResourcePool(ID3D12Device* device, D3D12_RESOURCE_STATES defaultState = D3D12_RESOURCE_STATE_COMMON)
        : _device(device), _defaultState(defaultState) {}
    
    ~D3D12ResourcePool() {
        Clear();
    }

    // Non-copyable
    D3D12ResourcePool(const D3D12ResourcePool&) = delete;
    D3D12ResourcePool& operator=(const D3D12ResourcePool&) = delete;

    ID3D12Resource* Acquire(const ResourceDescriptor<ID3D12Resource>& desc) override {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto& pool = _pools[desc];
        
        // Find available resource
        for (auto& pooled : pool) {
            if (!pooled->inUse && pooled->resource) {
                pooled->inUse = true;
                pooled->lastUsedFrame = _currentFrame;
                _totalReused++;
                
                LOG_TRACE("D3D12ResourcePool: Reusing resource {}x{}", desc.width, desc.height);
                return pooled->resource;
            }
        }
        
        // Create new resource
        auto pooled = std::make_unique<PooledResource>();
        
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = desc.width;
        resourceDesc.Height = desc.height;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = static_cast<DXGI_FORMAT>(desc.format);
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(desc.flags);
        
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        
        HRESULT hr = _device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            _defaultState,
            nullptr,
            IID_PPV_ARGS(&pooled->resource)
        );
        
        if (FAILED(hr)) {
            LOG_ERROR("D3D12ResourcePool: Failed to create resource: {:X}", (UINT)hr);
            return nullptr;
        }
        
        pooled->inUse = true;
        pooled->lastUsedFrame = _currentFrame;
        
        ID3D12Resource* result = pooled->resource;
        pool.push_back(std::move(pooled));
        _totalAllocated++;
        
        LOG_TRACE("D3D12ResourcePool: Created new resource {}x{}", desc.width, desc.height);
        return result;
    }

    void Release(ID3D12Resource* resource, const ResourceDescriptor<ID3D12Resource>& desc) override {
        if (!resource) return;
        
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto it = _pools.find(desc);
        if (it == _pools.end()) {
            // Not from pool, just release
            resource->Release();
            return;
        }
        
        for (auto& pooled : it->second) {
            if (pooled->resource == resource) {
                pooled->inUse = false;
                pooled->lastUsedFrame = _currentFrame;
                return;
            }
        }
        
        // Not found in pool, release
        resource->Release();
    }

    void Trim(size_t maxSize) override {
        std::lock_guard<std::mutex> lock(_mutex);
        
        for (auto& [desc, pool] : _pools) {
            // Remove old unused resources
            auto it = pool.begin();
            while (it != pool.end() && pool.size() > maxSize) {
                if (!(*it)->inUse && (_currentFrame - (*it)->lastUsedFrame) > 60) {
                    if ((*it)->resource) {
                        (*it)->resource->Release();
                    }
                    it = pool.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    void Clear() override {
        std::lock_guard<std::mutex> lock(_mutex);
        
        for (auto& [desc, pool] : _pools) {
            for (auto& pooled : pool) {
                if (pooled->resource) {
                    pooled->resource->Release();
                }
            }
        }
        _pools.clear();
    }

    void NextFrame() {
        _currentFrame++;
    }

    // Statistics
    size_t GetTotalAllocated() const { return _totalAllocated; }
    size_t GetTotalReused() const { return _totalReused; }
    size_t GetPoolCount() const { return _pools.size(); }
    
    size_t GetTotalPooledResources() const {
        size_t count = 0;
        for (const auto& [desc, pool] : _pools) {
            count += pool.size();
        }
        return count;
    }
};

// RAII wrapper for pooled D3D12 resources
class ScopedD3D12Resource {
private:
    D3D12ResourcePool* _pool = nullptr;
    ID3D12Resource* _resource = nullptr;
    ResourceDescriptor<ID3D12Resource> _desc{};

public:
    ScopedD3D12Resource() = default;
    
    ScopedD3D12Resource(D3D12ResourcePool* pool, ID3D12Resource* resource, const ResourceDescriptor<ID3D12Resource>& desc)
        : _pool(pool), _resource(resource), _desc(desc) {}
    
    ~ScopedD3D12Resource() {
        Reset();
    }
    
    // Non-copyable
    ScopedD3D12Resource(const ScopedD3D12Resource&) = delete;
    ScopedD3D12Resource& operator=(const ScopedD3D12Resource&) = delete;
    
    // Movable
    ScopedD3D12Resource(ScopedD3D12Resource&& other) noexcept
        : _pool(other._pool), _resource(other._resource), _desc(other._desc) {
        other._pool = nullptr;
        other._resource = nullptr;
    }
    
    ScopedD3D12Resource& operator=(ScopedD3D12Resource&& other) noexcept {
        if (this != &other) {
            Reset();
            _pool = other._pool;
            _resource = other._resource;
            _desc = other._desc;
            other._pool = nullptr;
            other._resource = nullptr;
        }
        return *this;
    }
    
    void Reset() {
        if (_pool && _resource) {
            _pool->Release(_resource, _desc);
        }
        _pool = nullptr;
        _resource = nullptr;
    }
    
    ID3D12Resource* Get() const { return _resource; }
    ID3D12Resource* operator->() const { return _resource; }
    explicit operator bool() const { return _resource != nullptr; }
};

} // namespace OptiScaler
