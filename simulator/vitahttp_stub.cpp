// vitahttp_stub.cpp
// Real HTTP implementation for the pkgj Linux simulator using libcurl.
//
// start()  → launches a background thread that runs curl_easy_perform();
//            blocks until response headers are received so that get_status()
//            and get_length() are valid immediately after start() returns.
// read()   → drains a shared deque filled by the curl write callback;
//            blocks until data or end-of-transfer.
// abort()  → sets a flag; the write callback returns 0 which causes curl to
//            cancel the transfer.

#include "vitahttp.hpp"

#include "log.hpp"

#include <curl/curl.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

struct pkgi_http
{
    CURL* easy = nullptr;
    std::thread thread;

    std::atomic<bool> aborted{false};

    // Set once headers are received; start() waits on this.
    bool headers_ready = false;
    long status_code = 0;
    curl_off_t content_length = -1;
    std::string error_msg;

    // Streaming body buffer shared between curl thread and read().
    std::deque<uint8_t> buf;
    bool transfer_done = false;

    std::mutex mtx;
    std::condition_variable cv;
};

// ---------------------------------------------------------------------------
// Curl callbacks
// ---------------------------------------------------------------------------

// Called once per header line.
static size_t header_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* h = static_cast<pkgi_http*>(userdata);
    const size_t total = size * nmemb;
    const std::string line(ptr, total);

    // "HTTP/1.x NNN …" or "HTTP/2 NNN …"
    if (line.rfind("HTTP/", 0) == 0)
    {
        const auto sp = line.find(' ');
        if (sp != std::string::npos)
            h->status_code = std::stol(line.substr(sp + 1));
    }
    else if (line.rfind("Content-Length:", 0) == 0)
    {
        const auto sp = line.find(':');
        if (sp != std::string::npos)
            h->content_length =
                    static_cast<curl_off_t>(std::stoll(line.substr(sp + 1)));
    }

    // Blank line = end of headers → wake start()
    if (line == "\r\n" || line == "\n")
    {
        std::lock_guard<std::mutex> lk(h->mtx);
        h->headers_ready = true;
        h->cv.notify_all();
    }

    return total;
}

// Called repeatedly with body data.
static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* h = static_cast<pkgi_http*>(userdata);
    const size_t total = size * nmemb;

    if (h->aborted)
        return 0; // returning 0 makes curl abort with CURLE_WRITE_ERROR

    // Back-pressure: don't buffer more than 8 MB at once.
    {
        std::unique_lock<std::mutex> lk(h->mtx);
        h->cv.wait(lk, [h] {
            return h->aborted.load() || h->buf.size() < 8 * 1024 * 1024;
        });
        if (h->aborted)
            return 0;

        h->buf.insert(h->buf.end(), (uint8_t*)ptr, (uint8_t*)ptr + total);
    }
    h->cv.notify_all();
    return total;
}

// ---------------------------------------------------------------------------
// VitaHttp
// ---------------------------------------------------------------------------

VitaHttp::~VitaHttp()
{
    if (_http)
    {
        _http->aborted = true;
        _http->cv.notify_all();
        if (_http->thread.joinable())
            _http->thread.join();
        curl_easy_cleanup(_http->easy);
        delete _http;
        _http = nullptr;
    }
}

void VitaHttp::start(const std::string& url, uint64_t offset)
{
    LOGF("VitaHttp::start {}", url);

    auto* h = new pkgi_http();
    h->easy = curl_easy_init();
    if (!h->easy)
    {
        delete h;
        throw HttpError("curl_easy_init() failed");
    }

    curl_easy_setopt(h->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(h->easy, CURLOPT_USERAGENT, "pkgj-sim/1.0");
    curl_easy_setopt(h->easy, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(h->easy, CURLOPT_HEADERDATA, h);
    curl_easy_setopt(h->easy, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(h->easy, CURLOPT_WRITEDATA, h);

    if (offset > 0)
    {
        const std::string range = std::to_string(offset) + "-";
        curl_easy_setopt(h->easy, CURLOPT_RANGE, range.c_str());
    }

    // Run the transfer in a background thread.
    h->thread = std::thread([h] {
        const CURLcode rc = curl_easy_perform(h->easy);
        if (rc != CURLE_OK && rc != CURLE_WRITE_ERROR)
        {
            // CURLE_WRITE_ERROR is expected when abort() returns 0.
            std::lock_guard<std::mutex> lk(h->mtx);
            h->error_msg = curl_easy_strerror(rc);
        }
        // Mark done and ensure start() can unblock even if no blank header
        // line was seen (e.g. on error).
        {
            std::lock_guard<std::mutex> lk(h->mtx);
            h->headers_ready = true;
            h->transfer_done = true;
        }
        h->cv.notify_all();
    });

    // Block until headers arrive (or transfer ends early on error).
    {
        std::unique_lock<std::mutex> lk(h->mtx);
        h->cv.wait(lk, [h] { return h->headers_ready; });
        if (!h->error_msg.empty())
        {
            // Thread will end on its own; join and clean up.
            lk.unlock();
            h->thread.join();
            curl_easy_cleanup(h->easy);
            const std::string msg = h->error_msg;
            delete h;
            throw HttpError(msg);
        }
    }

    _http = h;
}

int64_t VitaHttp::read(uint8_t* buffer, uint64_t size)
{
    std::unique_lock<std::mutex> lk(_http->mtx);
    _http->cv.wait(lk, [this] {
        return !_http->buf.empty() || _http->transfer_done ||
               _http->aborted;
    });

    if (_http->buf.empty())
        return 0; // EOF

    const size_t n = std::min((size_t)size, _http->buf.size());
    std::copy(_http->buf.begin(), _http->buf.begin() + n, buffer);
    _http->buf.erase(_http->buf.begin(), _http->buf.begin() + n);
    lk.unlock();
    _http->cv.notify_all(); // release back-pressure on write_cb
    return static_cast<int64_t>(n);
}

void VitaHttp::abort()
{
    if (_http)
    {
        _http->aborted = true;
        _http->cv.notify_all();
    }
}

int VitaHttp::get_status()
{
    return _http ? static_cast<int>(_http->status_code) : 0;
}

int64_t VitaHttp::get_length()
{
    return _http ? static_cast<int64_t>(_http->content_length) : -1;
}

VitaHttp::operator bool() const
{
    return _http != nullptr;
}

void VitaHttp::check_status()
{
    // Unused in simulator; status is available immediately after start().
}
