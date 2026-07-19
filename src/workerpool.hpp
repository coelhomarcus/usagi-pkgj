#pragma once

#include "thread.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// WorkerSlot — a single-slot background worker.
//
// At most one task runs at a time.  The main thread submits tasks via
// try_submit(); the worker thread runs the provided lambda entirely on
// local data and never touches the caller's object.
//
// Key guarantees:
//  • Only one background thread is alive at any time.
//  • Submitting the same task_id while it is already running returns false
//    (duplicate detection).  A different task also returns false while the
//    slot is busy — the caller retries next frame.
//  • The caller can be destroyed safely at any time: results are handed
//    back through a shared_ptr, so even if the owner is gone the worker
//    writes into the still-live result object and then releases it.
//
// See WorkerPool below for a small fixed-size pool of these — that's what
// ImageFetcher actually submits to.
class WorkerSlot
{
public:
    WorkerSlot()  = default;
    ~WorkerSlot()
    {
        // Join the OS thread on shutdown so no thread outlives the process.
        if (_thread)
            _thread->join();
    }

    WorkerSlot(const WorkerSlot&)            = delete;
    WorkerSlot& operator=(const WorkerSlot&) = delete;

    // Attempt to start a new background task.
    //
    // task_id  — any string that uniquely identifies this unit of work.
    //            If the same task_id is already running the call is treated
    //            as a duplicate (in-flight) and immediately returns false.
    // fn       — the work function; MUST operate only on data captured by
    //            value or through a shared_ptr.  It must NOT capture raw
    //            pointers or references to objects that may be destroyed
    //            before the worker finishes.
    //
    // Returns true  → thread started; the slot is now busy.
    // Returns false → slot is busy (same or different task); no state change.
    //                 The caller should retry on the next frame.
    bool try_submit(const std::string& task_id, std::function<void()> fn)
    {
        if (_running.load(std::memory_order_acquire))
        {
            // Slot is busy.
            // If task_id == _current_task_id this is a duplicate in-flight
            // request; either way we cannot start another thread yet.
            return false;
        }

        // Worker is idle.  Join (and free) the finished Thread object before
        // creating a new one — on Vita sceKernelDeleteThread must be called
        // after the thread has ended.
        if (_thread)
        {
            _thread->join();
            _thread.reset();
        }

        _current_task_id = task_id;
        _running.store(true, std::memory_order_release);

        // Wrap fn: after fn() returns, mark the slot idle again so the next
        // try_submit() can start a new task on the following frame.
        _thread = std::make_unique<Thread>(
                "img_worker",
                [this, fn = std::move(fn)]() mutable
                {
                    fn();
                    _running.store(false, std::memory_order_release);
                });

        return true;
    }

    bool               is_running()      const { return _running.load(std::memory_order_acquire); }
    const std::string& current_task_id() const { return _current_task_id; }

    // Block until the worker is idle.  For graceful shutdown only — do not
    // call from the main render loop.
    void join()
    {
        if (_thread)
        {
            _thread->join();
            _thread.reset();
        }
    }

private:
    std::unique_ptr<Thread> _thread;
    std::atomic<bool>       _running{false};
    std::string             _current_task_id; // written from the main thread only
};

// WorkerPool — a small fixed-size pool of WorkerSlots.
//
// try_submit() hands the task to the first free slot. Unlike a single
// WorkerSlot, a busy slot no longer implies the whole pool is busy — so
// task_id duplicate detection has to be done explicitly, across every slot,
// before looking for a free one: the same cover can be wanted by two
// independent ImageFetcher instances at once (e.g. GridImageCache and
// GameView's own fetcher, for a title visible in both at the same time),
// and letting both run concurrently would race two workers writing the
// same .tmp cache file.
//
// All methods are main-thread-only, same as WorkerSlot.
class WorkerPool
{
public:
    explicit WorkerPool(size_t slot_count)
    {
        _slots.reserve(slot_count);
        for (size_t i = 0; i < slot_count; ++i)
            _slots.push_back(std::make_unique<WorkerSlot>());
    }

    bool try_submit(const std::string& task_id, std::function<void()> fn)
    {
        for (const auto& slot : _slots)
            if (slot->is_running() && slot->current_task_id() == task_id)
                return false; // duplicate in-flight on another slot

        for (auto& slot : _slots)
            if (slot->try_submit(task_id, fn))
                return true;

        return false; // every slot busy
    }

    // Global singleton shared by all ImageFetcher instances.
    // Bounds how many cover downloads can be in flight at once.
    static WorkerPool& image_workers()
    {
        static WorkerPool pool(3);
        return pool;
    }

private:
    std::vector<std::unique_ptr<WorkerSlot>> _slots;
};
