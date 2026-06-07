#pragma once

#include "db.hpp"
#include "thread.hpp"

#include <string>

// Fetches the long description for a game from the PlayStation Store (chihiro
// container API).  Runs in its own background thread; main thread polls
// get_status() and reads get_description() only after Status::Found.
class DescriptionFetcher
{
public:
    enum class Status
    {
        Fetching,
        Found,
        NotAvailable,
        Error,
    };

    // Starts the background fetch immediately.
    DescriptionFetcher(const DbItem* item);
    ~DescriptionFetcher();

    Status            get_status();
    // Safe to call from the main thread at any time; returns empty string
    // until Status::Found.
    std::string       get_description();

private:
    const DbItem* _item;

    Mutex       _mutex;
    bool        _abort{false};
    Status      _status{Status::Fetching};
    std::string _description;

    Thread _thread;

    void do_request();
};
