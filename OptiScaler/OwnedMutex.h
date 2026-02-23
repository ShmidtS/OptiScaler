#pragma once

#include "SysUtils.h"

#include <atomic>
#include <shared_mutex>

class OwnedMutex
{
  private:
    std::shared_mutex mtx;
    std::atomic<uint32_t> owner { 0 }; // don't use 0

  public:
    // Returns true if lock acquired, false if already owned by same owner (recursive lock attempt)
    bool lock(uint32_t _owner)
    {
        // Check if already owned by same owner - prevent recursive deadlock
        if (owner.load(std::memory_order_acquire) == _owner)
        {
            LOG_WARN("Recursive lock attempt by owner {}, ignoring to prevent deadlock", _owner);
            return false; // Already locked by same owner
        }

        mtx.lock();
        owner.store(_owner, std::memory_order_release);
        return true;
    }

    // Only unlocks if owner matches
    void unlockThis(uint32_t _owner)
    {
        uint32_t current_owner = owner.load(std::memory_order_acquire);

        if (current_owner == 0 || current_owner != _owner)
        {
            LOG_WARN("unlockThis failed: current_owner: {}, _owner: {}", current_owner, _owner);
            return;
        }

        owner.store(0, std::memory_order_release);
        mtx.unlock();
    }

    uint32_t getOwner() { return owner.load(std::memory_order_seq_cst); }

    // Check if lock is currently held by anyone
    bool isLocked() { return owner.load(std::memory_order_acquire) != 0; }
};

class OwnedLockGuard
{
  private:
    OwnedMutex& _mutex;
    uint32_t _owner_id;
    bool _locked;

  public:
    OwnedLockGuard(OwnedMutex& mutex, uint32_t owner_id) : _mutex(mutex), _owner_id(owner_id), _locked(false)
    {
        _locked = _mutex.lock(_owner_id);
    }

    ~OwnedLockGuard() { if (_locked) { _mutex.unlockThis(_owner_id); } }

    bool isLocked() const { return _locked; }
};
