#pragma once

#include "http.hpp"

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Simple blocking Http implementation using libcurl.
// Intended for small fetches (cover art, patch XML) that already run inside a
// background thread — curl_easy_perform buffers the full response in memory
// and returns. No internal threading or ring buffer needed.
class CurlHttp : public Http
{
public:
    // external_abort: optional pointer to an atomic flag owned by the caller.
    // The progress callback checks it in addition to the local abort() call,
    // so an abort() signal set before start() is invoked is never lost.
    explicit CurlHttp(const std::atomic<bool>* external_abort = nullptr);
    ~CurlHttp();

    void start(const std::string& url, uint64_t offset) override;
    int64_t read(uint8_t* buffer, uint64_t size) override;
    void abort() override;

    int get_status() override;
    int64_t get_length() override;

    explicit operator bool() const override;

private:
    std::vector<uint8_t> _body;
    size_t               _read_pos       = 0;
    long                 _status_code    = 0;
    int64_t              _content_length = -1;
    std::atomic<bool>    _aborted{false};

    // Optional external abort flag (e.g. from ImageFetcher::_abort).
    const std::atomic<bool>* _external_abort = nullptr;

    // Kept alive until abort() or destruction so the progress callback can
    // reference _aborted atomically.
    CURL* _curl = nullptr;

    char _err_buf[CURL_ERROR_SIZE] = {};

    static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud);
    static int    progress_cb(void* ud,
                              curl_off_t, curl_off_t,
                              curl_off_t, curl_off_t);
};
