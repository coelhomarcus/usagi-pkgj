#include "descriptionfetcher.hpp"

#include "curlhttp.hpp"
#include "log.hpp"
#include "pkgi.hpp"

#include <fmt/format.h>
#include <mutex>
#include <vector>

namespace
{
// Returns the PSN Store country code for this item's region.
static std::string get_country(const DbItem* item)
{
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
        if (item->name.find("CHN") != std::string::npos)
            return "CN";
        if (item->content.substr(0, 6) == "HP0507")
            return "KR";
        return "HK";
    case RegionJPN:
        return "JP";
    case RegionEUR:
        return "GB";
    default:
        return "US";
    }
}

static std::string get_language(const DbItem* item)
{
    switch (pkgi_get_region(item->titleid))
    {
    case RegionASA:
    {
        if (item->name.find("CHN") != std::string::npos)
            return "zh";
        if (item->content.substr(0, 6) == "HP0507")
            return "ko";
        return "zh";
    }
    case RegionJPN:
        return "ja";
    default:
        return "en";
    }
}

// Minimal JSON string extractor: finds  "key":"value"  and returns the
// unescaped value.  Handles the common escape sequences used in PSN JSON.
static std::string extract_json_string(
        const std::string& json, const std::string& key)
{
    const auto search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return {};
    pos += search.size();

    std::string result;
    result.reserve(256);
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            ++pos;
            switch (json[pos])
            {
            case '"':
                result += '"';
                break;
            case '\\':
                result += '\\';
                break;
            case '/':
                result += '/';
                break;
            case 'n':
                result += '\n';
                break;
            case 'r':
                break; // drop bare CR
            case 't':
                result += ' ';
                break;
            default:
                result += json[pos];
                break;
            }
        }
        else
        {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}
// Strip HTML tags and decode common entities from PSN store descriptions.
// Converts <br>, <br/>, <br /> to newlines; strips all other tags;
// decodes &amp; &lt; &gt; &quot; &apos; &nbsp; and numeric &#NNN; / &#xHH;
// Collapses runs of 3+ newlines to 2.
static std::string clean_html(const std::string& html)
{
    std::string out;
    out.reserve(html.size());

    size_t i = 0;
    const size_t n = html.size();

    while (i < n)
    {
        if (html[i] == '<')
        {
            // Collect tag name
            size_t j = i + 1;
            while (j < n && html[j] != '>' && html[j] != ' ' && html[j] != '/')
                ++j;
            const std::string tag = html.substr(i + 1, j - (i + 1));

            // Advance past the closing >
            while (j < n && html[j] != '>')
                ++j;
            if (j < n)
                ++j; // skip '>'

            // <br> / <br/> → newline
            if (tag == "br" || tag == "BR")
                out += '\n';
            // else: strip all other tags

            i = j;
        }
        else if (html[i] == '&')
        {
            size_t j = i + 1;
            while (j < n && html[j] != ';' && (j - i) < 12)
                ++j;
            if (j < n && html[j] == ';')
            {
                const std::string entity = html.substr(i + 1, j - i - 1);
                if (entity == "amp")        out += '&';
                else if (entity == "lt")    out += '<';
                else if (entity == "gt")    out += '>';
                else if (entity == "quot")  out += '"';
                else if (entity == "apos")  out += '\'';
                else if (entity == "nbsp")  out += ' ';
                else if (!entity.empty() && entity[0] == '#')
                {
                    // Numeric entity — encode as UTF-8
                    unsigned long cp = 0;
                    try {
                        if (entity.size() > 1 &&
                            (entity[1] == 'x' || entity[1] == 'X'))
                            cp = std::stoul(entity.substr(2), nullptr, 16);
                        else
                            cp = std::stoul(entity.substr(1), nullptr, 10);
                    } catch (...) {}
                    // Encode codepoint as UTF-8
                    if (cp < 0x80)
                    {
                        out += static_cast<char>(cp);
                    }
                    else if (cp < 0x800)
                    {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    else if (cp < 0x10000)
                    {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    else if (cp < 0x110000)
                    {
                        out += static_cast<char>(0xF0 | (cp >> 18));
                        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                }
                else
                {
                    // Unknown entity — keep verbatim
                    out += html.substr(i, j - i + 1);
                }
                i = j + 1; // skip ';'
            }
            else
            {
                // Not a valid entity — emit as-is
                out += html[i++];
            }
        }
        else
        {
            out += html[i++];
        }
    }

    // Collapse runs of 3+ newlines to 2
    std::string collapsed;
    collapsed.reserve(out.size());
    int nl_run = 0;
    for (char c : out)
    {
        if (c == '\n')
        {
            ++nl_run;
            if (nl_run <= 2)
                collapsed += c;
        }
        else
        {
            nl_run = 0;
            collapsed += c;
        }
    }

    // Trim leading/trailing whitespace
    size_t start = collapsed.find_first_not_of(" \t\n\r");
    size_t end   = collapsed.find_last_not_of(" \t\n\r");
    if (start == std::string::npos)
        return {};
    return collapsed.substr(start, end - start + 1);
}
} // namespace

DescriptionFetcher::DescriptionFetcher(const DbItem* item)
    : _item(item)
    , _mutex("desc_fetcher_mutex")
    , _thread("desc_fetcher", [this] { do_request(); })
{
}

DescriptionFetcher::~DescriptionFetcher()
{
    {
        std::lock_guard<Mutex> lock(_mutex);
        _abort = true;
    }
    _thread.join();
}

DescriptionFetcher::Status DescriptionFetcher::get_status()
{
    std::lock_guard<Mutex> lock(_mutex);
    return _status;
}

std::string DescriptionFetcher::get_description()
{
    std::lock_guard<Mutex> lock(_mutex);
    return _description;
}

void DescriptionFetcher::do_request()
{
    try
    {
        {
            std::lock_guard<Mutex> lock(_mutex);
            if (_abort)
                return;
        }

        const auto country  = get_country(_item);
        const auto language = get_language(_item);

        // Chihiro container endpoint — returns a JSON object that includes
        // "long_desc" (and "short_desc" as fallback).
        const auto url = fmt::format(
                "https://store.playstation.com/store/api/chihiro/"
                "00_09_000/container/{}/{}/19/{}",
                country,
                language,
                _item->content);

        CurlHttp http;
        try
        {
            http.start(url, 0);
        }
        catch (const std::exception& e)
        {
            LOGFW("[DescriptionFetcher] HTTP start failed for {}: {}",
                  _item->titleid,
                  e.what());
            std::lock_guard<Mutex> lock(_mutex);
            _status = Status::Error;
            return;
        }

        if (http.get_status() == 404)
        {
            std::lock_guard<Mutex> lock(_mutex);
            _status = Status::NotAvailable;
            return;
        }

        // Read up to 512 KB — description JSON is typically < 32 KB.
        constexpr size_t MAX_BYTES = 512 * 1024;
        std::vector<uint8_t> data;
        data.reserve(32 * 1024);
        size_t pos = 0;
        while (true)
        {
            {
                std::lock_guard<Mutex> lock(_mutex);
                if (_abort)
                    return;
            }
            if (pos == data.size())
                data.resize(pos + 4096);

            int64_t n = 0;
            try
            {
                n = http.read(data.data() + pos, data.size() - pos);
            }
            catch (...)
            {
                break;
            }
            if (n == 0)
                break;
            pos += static_cast<size_t>(n);
            if (pos > MAX_BYTES)
                break;
        }
        data.resize(pos);

        const std::string json(
                reinterpret_cast<const char*>(data.data()), data.size());

        // Try long description first, fall back to short.
        auto desc = extract_json_string(json, "long_desc");
        if (desc.empty())
            desc = extract_json_string(json, "short_desc");

        std::lock_guard<Mutex> lock(_mutex);
        if (!_abort)
        {
            if (desc.empty())
                _status = Status::NotAvailable;
            else
            {
                _description = clean_html(desc);
                _status      = Status::Found;
            }
        }
    }
    catch (const std::exception& e)
    {
        LOGFW("[DescriptionFetcher] exception for {}: {}",
              _item->titleid,
              e.what());
        std::lock_guard<Mutex> lock(_mutex);
        _status = Status::Error;
    }
}
