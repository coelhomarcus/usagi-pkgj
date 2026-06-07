#include "curlhttp.hpp"

#include "log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------

CurlHttp::CurlHttp(const std::atomic<bool>* external_abort)
    : _external_abort(external_abort)
{
}

CurlHttp::~CurlHttp()
{
    if (_curl)
    {
        curl_easy_cleanup(_curl);
        _curl = nullptr;
    }
}

void CurlHttp::start(const std::string& url, uint64_t offset)
{
    _body.clear();
    _read_pos       = 0;
    _status_code    = 0;
    _content_length = -1;
    // Do NOT reset _aborted here: abort() may have been called between
    // construction and start(), and clearing the flag here would lose that
    // signal, causing curl_easy_perform to run to its full timeout.
    std::memset(_err_buf, 0, sizeof(_err_buf));

    _curl = curl_easy_init();
    if (!_curl)
        throw HttpError("curl_easy_init failed");

    curl_easy_setopt(_curl, CURLOPT_URL,              url.c_str());
    curl_easy_setopt(_curl, CURLOPT_USERAGENT,        "libhttp/3.65 (PS Vita)");
    curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION,   1L);
    curl_easy_setopt(_curl, CURLOPT_MAXREDIRS,        5L);
    curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYPEER,   0L);
    curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYHOST,   0L);
    curl_easy_setopt(_curl, CURLOPT_CONNECTTIMEOUT,   30L);
    curl_easy_setopt(_curl, CURLOPT_TIMEOUT,          60L);
    curl_easy_setopt(_curl, CURLOPT_ERRORBUFFER,      _err_buf);
    curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION,    write_cb);
    curl_easy_setopt(_curl, CURLOPT_WRITEDATA,        &_body);
    curl_easy_setopt(_curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(_curl, CURLOPT_XFERINFODATA,     this);
    curl_easy_setopt(_curl, CURLOPT_NOPROGRESS,       0L);

    if (offset > 0)
    {
        const auto range = fmt::format("{}-", offset);
        curl_easy_setopt(_curl, CURLOPT_RANGE, range.c_str());
    }

    const CURLcode res = curl_easy_perform(_curl);

    curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &_status_code);

    curl_off_t cl = -1;
    curl_easy_getinfo(_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    _content_length = static_cast<int64_t>(cl);

    curl_easy_cleanup(_curl);
    _curl = nullptr;

    if (res != CURLE_OK && res != CURLE_ABORTED_BY_CALLBACK)
        throw HttpError(fmt::format(
            "curl failed: {} ({})",
            static_cast<int>(res),
            _err_buf[0] ? _err_buf : curl_easy_strerror(res)));
}

int64_t CurlHttp::read(uint8_t* buffer, uint64_t size)
{
    if (_read_pos >= _body.size())
        return 0;

    const size_t available = _body.size() - _read_pos;
    const size_t to_copy   =
        static_cast<size_t>(std::min<uint64_t>(size, available));

    std::memcpy(buffer, _body.data() + _read_pos, to_copy);
    _read_pos += to_copy;
    return static_cast<int64_t>(to_copy);
}

void CurlHttp::abort()
{
    _aborted = true;
}

int CurlHttp::get_status()
{
    return static_cast<int>(_status_code);
}

int64_t CurlHttp::get_length()
{
    return _content_length;
}

CurlHttp::operator bool() const
{
    return true;
}

// ---------------------------------------------------------------------------

size_t CurlHttp::write_cb(char* ptr, size_t size, size_t nmemb, void* ud)
{
    auto* body = static_cast<std::vector<uint8_t>*>(ud);
    body->insert(body->end(), ptr, ptr + size * nmemb);
    return size * nmemb;
}

int CurlHttp::progress_cb(void* ud,
                           curl_off_t, curl_off_t,
                           curl_off_t, curl_off_t)
{
    const auto* self = static_cast<CurlHttp*>(ud);
    if (self->_aborted.load())
        return 1;
    if (self->_external_abort && self->_external_abort->load())
        return 1;
    return 0;
}

// CurlHttp object is used. We guard with std::call_once so it can also be
