#pragma once

#include "thread.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

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

    // Global singleton shared by all ImageFetcher instances.
    // Ensures at most one background download is in flight at any time.
    static WorkerSlot& image_worker()
    {
        static WorkerSlot s;
        return s;
    }

private:
    std::unique_ptr<Thread> _thread;
    std::atomic<bool>       _running{false};
    std::string             _current_task_id; // written from the main thread only
};
