#include "dialog.hpp"
#include "curlhttp.hpp"
#include "file.hpp"
#include "pkgi.hpp"

#include <boost/scope_exit.hpp>

#include <vector>

// GitHub Releases API — latest release for toaster-code/pkgj
#define PKGJ_RELEASES_API \
    "https://api.github.com/repos/toaster-code/pkgj/releases/latest"

namespace
{
std::string release_tag;
std::string download_url;

// Minimal JSON string extractor: finds  "key":"value"  and returns the value.
static std::string extract_json_str(
        const std::string& json, const std::string& key)
{
    const auto search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return {};
    pos += search.size();
    std::string result;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            ++pos;
            if (json[pos] == '"')       result += '"';
            else if (json[pos] == '\\') result += '\\';
            else if (json[pos] == '/')  result += '/';
            else                        result += json[pos];
        }
        else
        {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

// Find the browser_download_url for the first .vpk asset in a JSON releases
// response.  The structure is: "assets":[{...,"browser_download_url":"..."}]
static std::string find_vpk_url(const std::string& json)
{
    // Iterate over browser_download_url values and return first .vpk
    size_t search_from = 0;
    const std::string key = "\"browser_download_url\":\"";
    while (true)
    {
        auto pos = json.find(key, search_from);
        if (pos == std::string::npos)
            return {};
        pos += key.size();
        std::string url;
        while (pos < json.size() && json[pos] != '"')
            url += json[pos++];
        if (url.size() > 4 &&
            url.substr(url.size() - 4) == ".vpk")
            return url;
        search_from = pos;
    }
}

void start_download()
{
    try
    {
        LOGF("Downloading PKGj update {}", release_tag);

        const auto filename = fmt::format(
                "{}/pkgj-{}.vpk", pkgi_get_config_folder(), release_tag);

        pkgi_dialog_message("Downloading update", 0);

        try
        {
            const auto file = pkgi_create(filename.c_str());
            BOOST_SCOPE_EXIT_ALL(&)
            {
                pkgi_close(file);
            };

            CurlHttp http;
            http.start(download_url, 0);
            std::vector<uint8_t> data(64 * 1024);
            while (true)
            {
                const auto read = http.read(data.data(), data.size());
                if (read == 0)
                    break;
                pkgi_write(file, data.data(), read);
            }

            LOGF("PKGj update downloaded successfully");
        }
        catch (...)
        {
            LOGF("PKGj update download failed, removing partial file");
            pkgi_rm(filename.c_str());
            throw;
        }

        pkgi_dialog_message(
                fmt::format(
                        "The update has been downloaded to {}, install "
                        "it through VitaShell.",
                        filename)
                        .c_str());
    }
    catch (const std::exception& e)
    {
        pkgi_dialog_error(fmt::format("Download failed: {}", e.what()).c_str());
    }
}

void update_thread()
{
    try
    {
        if (!pkgi_is_module_present("NoNpDrm"))
            pkgi_dialog_error(
                    "NoNpDrm not found. Games cannot be installed or played.");

        while (pkgi_dialog_is_open())
        {
            pkgi_sleep(20);
        }

        LOGF("Checking for updates at: {}", PKGJ_RELEASES_API);

        // Fetch the GitHub Releases JSON
        CurlHttp http;
        http.start(PKGJ_RELEASES_API, 0);

        constexpr size_t MAX_BYTES = 256 * 1024;
        std::vector<uint8_t> buf;
        buf.reserve(32 * 1024);
        size_t pos = 0;
        while (true)
        {
            if (pos == buf.size())
                buf.resize(pos + 4096);
            int64_t n = 0;
            try { n = http.read(buf.data() + pos, buf.size() - pos); }
            catch (...) { break; }
            if (n == 0)
                break;
            pos += static_cast<size_t>(n);
            if (pos > MAX_BYTES)
                break;
        }
        buf.resize(pos);

        const std::string json(
                reinterpret_cast<const char*>(buf.data()), buf.size());

        // Parse tag_name (e.g. "v0.60") and the .vpk download URL
        const auto tag = extract_json_str(json, "tag_name");
        if (tag.empty())
        {
            LOGF("Update check: could not parse tag_name from API response");
            return;
        }

        LOGF("Latest release: {}", tag);

        // Strip leading 'v' for version comparison against PKGI_VERSION
        const std::string tag_ver =
                (!tag.empty() && tag[0] == 'v') ? tag.substr(1) : tag;

        if (tag_ver == PKGI_VERSION)
        {
            LOGF("Already on latest version {}", PKGI_VERSION);
            return;
        }

        const auto vpk_url = find_vpk_url(json);
        if (vpk_url.empty())
        {
            LOGF("Update check: no .vpk asset found in release {}", tag);
            return;
        }

        release_tag  = tag;
        download_url = vpk_url;

        pkgi_dialog_question(
                fmt::format(
                        "New PKGj version {} is available!\nDo you want to "
                        "download it?",
                        tag)
                        .c_str(),
                {{"Yes",
                  [] {
                      pkgi_start_thread(
                              "pkgj_update_download", &start_download);
                  }},
                 {"No", [] {}}});
    }
    catch (const std::exception& e)
    {
        LOGF("Update check failed: {}", e.what());
        pkgi_dialog_error(
                fmt::format("Update check failed: {}", e.what()).c_str());
    }
}
}

void start_update_thread()
{
    pkgi_start_thread("pkgj_update", &update_thread);
}
