#pragma once

#include "pkgi.hpp"

#ifndef PKGI_SIMULATOR
#include <psp2/kernel/threadmgr.h>
#endif // PKGI_SIMULATOR

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef PKGI_SIMULATOR
// ──────────────────────────────────────────────────────────────────────────────
// Linux / simulator implementations using std::mutex + std::thread
// ──────────────────────────────────────────────────────────────────────────────
#include <condition_variable>
#include <mutex>
#include <thread>

class ScopeProcessLock
{
public:
    ScopeProcessLock(const ScopeProcessLock&) = delete;
    ScopeProcessLock(ScopeProcessLock&&) = delete;
    ScopeProcessLock& operator=(const ScopeProcessLock&) = delete;
    ScopeProcessLock& operator=(ScopeProcessLock&&) = delete;

    ScopeProcessLock()  { pkgi_lock_process(); }
    ~ScopeProcessLock() { pkgi_unlock_process(); }
};

class Mutex
{
public:
    Mutex(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    explicit Mutex(const std::string& /*name*/) {}

    void lock()     { _m.lock(); }
    bool try_lock() { return _m.try_lock(); }
    void unlock()   { _m.unlock(); }

private:
    std::mutex _m;
    friend class Cond;
};

class Cond
{
public:
    Cond(const Cond&) = delete;
    Cond(Cond&&) = delete;
    Cond& operator=(const Cond&) = delete;
    Cond& operator=(Cond&&) = delete;

    explicit Cond(const std::string& name) : _mutex(name + "_mutex") {}

    void notify_one()
    {
        _cv.notify_one();
    }

    void wait()
    {
        std::unique_lock<std::mutex> lk(_mutex._m, std::adopt_lock);
        _cv.wait(lk);
        lk.release(); // keep locked after wait
    }

    Mutex& get_mutex() { return _mutex; }

private:
    Mutex _mutex;
    std::condition_variable _cv;
};

class Thread
{
public:
    using EntryPoint = std::function<void()>;
    static constexpr std::size_t kDefaultStackSize = 256 * 1024;

    Thread(const Thread&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread& operator=(Thread&&) = delete;

    Thread(
            const std::string& /*name*/,
            EntryPoint entry,
            std::size_t /*stack_size*/ = kDefaultStackSize)
        : _t(std::move(entry))
    {}

    ~Thread()
    {
        if (_t.joinable())
            _t.detach();
    }

    void join() { _t.join(); }

private:
    std::thread _t;
};

#else // PKGI_SIMULATOR — original Vita implementation below

class ScopeProcessLock
{
public:
    ScopeProcessLock(const ScopeProcessLock&) = delete;
    ScopeProcessLock(ScopeProcessLock&&) = delete;
    ScopeProcessLock& operator=(const ScopeProcessLock&) = delete;
    ScopeProcessLock& operator=(ScopeProcessLock&&) = delete;

    ScopeProcessLock()
    {
        pkgi_lock_process();
    }
    ~ScopeProcessLock()
    {
        pkgi_unlock_process();
    }
};

class Mutex
{
public:
    Mutex(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    Mutex(const std::string& name)
    {
        const auto res =
                sceKernelCreateLwMutex(&_mutex, name.c_str(), 0, 0, nullptr);
        if (res < 0)
        {
            LOG_ERR("Mutex creation failed: err=0x%08x", res);
            throw std::runtime_error("Mutex creation failed");
        }
    }

    ~Mutex()
    {
        const auto res = sceKernelDeleteLwMutex(&_mutex);
        if (res < 0)
        {
            // TODO assert
            LOG_ERR("Mutex deletion failed: err=0x%08x", res);
        }
    }

    void lock()
    {
        const auto res = sceKernelLockLwMutex(&_mutex, 1, nullptr);
        if (res < 0)
        {
            LOG_ERR("Mutex lock failed: err=0x%08x", res);
            throw std::runtime_error("Mutex lock failed");
        }
    }

    bool try_lock()
    {
        // I don't know how to handle errors
        // const auto res = sceKernelTryLockLwMutex(&_mutex);
        // if (res < 0)
        //{
        //    // TODO throw
        //    LOG("lock failed error=0x%08x", res);
        //}
        throw std::runtime_error("try_lock not implemented");
    }

    void unlock()
    {
        const auto res = sceKernelUnlockLwMutex(&_mutex, 1);
        if (res < 0)
        {
            LOG_ERR("Mutex unlock failed: err=0x%08x", res);
            throw std::runtime_error("Mutex unlock failed");
        }
    }

private:
    SceKernelLwMutexWork _mutex;

    friend class Cond;
};

class Cond
{
public:
    Cond(const Cond&) = delete;
    Cond(Cond&&) = delete;
    Cond& operator=(const Cond&) = delete;
    Cond& operator=(Cond&&) = delete;

    Cond(const std::string& name) : _mutex(name + "_mutex")
    {
        const auto res = sceKernelCreateLwCond(
                &_cond, name.c_str(), 0, &_mutex._mutex, nullptr);
        if (res < 0)
        {
            LOG_ERR("Condition variable creation failed: err=0x%08x", res);
            throw std::runtime_error("Condition variable creation failed");
        }
    }

    ~Cond()
    {
        const auto res = sceKernelDeleteLwCond(&_cond);
        if (res < 0)
        {
            // TODO assert
            LOG_ERR("Condition variable deletion failed: err=0x%08x", res);
        }
    }

    void notify_one()
    {
        const auto res = sceKernelSignalLwCond(&_cond);
        if (res < 0)
        {
            LOG_ERR("Condition variable signal failed: err=0x%08x", res);
            throw std::runtime_error("Condition variable signal failed");
        }
    }

    void wait()
    {
        const auto res = sceKernelWaitLwCond(&_cond, nullptr);
        if (res < 0)
        {
            LOG_ERR("Condition variable wait failed: err=0x%08x", res);
            throw std::runtime_error("Condition variable wait failed");
        }
    }

    Mutex& get_mutex()
    {
        return _mutex;
    }

private:
    Mutex _mutex;
    SceKernelLwCondWork _cond;
};

class Thread
{
public:
    using EntryPoint = std::function<void()>;
    static constexpr std::size_t kDefaultStackSize = 256 * 1024;

    Thread(const Thread&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread& operator=(Thread&&) = delete;

    Thread(
            const std::string& name,
            EntryPoint entry,
            std::size_t stack_size = kDefaultStackSize)
    {
        _tid = sceKernelCreateThread(
                name.c_str(),
                &entry_point,
                0xb0,
                static_cast<SceSize>(stack_size),
                0,
                0,
                nullptr);
        if (_tid < 0)
        {
            LOG_ERR("Thread creation failed: err=0x%08x", _tid);
            throw std::runtime_error("Thread creation failed");
        }
        auto entryp = new EntryPoint(std::move(entry));
        const auto res = sceKernelStartThread(_tid, sizeof(entryp), &entryp);
        if (res < 0)
        {
            delete entryp;
            LOG_ERR("Thread start failed: err=0x%08x", res);
            sceKernelDeleteThread(_tid);
            _tid = -1;
            throw std::runtime_error("Thread start failed");
        }
    }

    ~Thread()
    {
        if (_tid < 0)
            return;

        const auto res = sceKernelDeleteThread(_tid);
        if (res < 0)
        {
            // TODO assert
            LOG_ERR("Thread deletion failed: err=0x%08x", res);
        }
    }

    void join()
    {
        if (_tid < 0)
            return;

        int stat;
        const auto res = sceKernelWaitThreadEnd(_tid, &stat, nullptr);
        if (res < 0)
        {
            // TODO assert
            LOG_ERR("Thread join failed: err=0x%08x", res);
        }
    }

private:
    SceUID _tid{-1};

    static int entry_point(SceSize, void* argp)
    {
        try
        {
            auto entryp = std::unique_ptr<EntryPoint>(
                    *static_cast<EntryPoint**>(argp));
            (*entryp)();
            LOG("Thread terminated successfully");
        }
        catch (const std::exception& e)
        {
            LOG_ERR("Thread terminated with exception: %s", e.what());
        }
        catch (...)
        {
            LOG_ERR("Thread terminated with unknown exception");
        }
        return 0;
    }
};
#endif // PKGI_SIMULATOR
