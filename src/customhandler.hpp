#pragma once

#include <string>

class Downloader;
struct Config;
struct DbItem;

enum class CustomInstallTemplateKind
{
    Unknown,
    VitaLike,
    PsxLike,
    PsmLike,
    PspLike,
};

struct CustomInstallTemplateContext
{
    std::string               list_name;
    std::string               tsv_url;
    CustomInstallTemplateKind kind = CustomInstallTemplateKind::Unknown;
};

struct CustomInstallRequest
{
    const CustomInstallTemplateContext* context;
    Config*                             config;
    Downloader*                         downloader;
    DbItem*                             item;
};

CustomInstallTemplateContext pkgi_custom_make_template_context(
        const std::string& list_name,
        const std::string& tsv_url);

void pkgi_custom_open_list_template(
        const std::string& list_name,
        const std::string& tsv_url);

void pkgi_custom_confirm_item_template(const CustomInstallRequest& request);