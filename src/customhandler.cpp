#include "customhandler.hpp"

#include "dialog.hpp"
#include "download.hpp"
#include "downloader.hpp"
#include "gameview.hpp"
#include "pkgi.hpp"

#include <fmt/format.h>

namespace
{
void show_template_disabled(
        const CustomInstallTemplateContext& context,
        const char*                         flow_name)
{
    pkgi_dialog_error(
            fmt::format(
                    "Custom handler template is disabled.\n\n"
                    "List: {}\nTSV: {}\nTemplate: {}\n\n"
                    "Fill in src/customhandler.cpp to enable this flow.",
                    context.list_name,
                    context.tsv_url,
                    flow_name)
                    .c_str());
}

void handle_vita_like_template(const CustomInstallRequest& request)
{
    PKGI_UNUSED(request);
    // Template based on the regular PSV branch in src/pkgi.cpp:
    //   gameview = std::make_unique<GameView>(...)
    // Keep this disabled until the custom TSV format and metadata are mapped.
    show_template_disabled(*request.context, "PS Vita-like");
}

void handle_psx_like_template(const CustomInstallRequest& request)
{
    PKGI_UNUSED(request);
    // Template based on the PS1 branch in src/pkgi.cpp where installation goes
    // through pkgi_install_package / pkgi_start_download with PSX semantics.
    show_template_disabled(*request.context, "PS1-like");
}

void handle_psm_like_template(const CustomInstallRequest& request)
{
    PKGI_UNUSED(request);
    // Template based on the PSM branch in src/pkgi.cpp where install handling
    // differs from Vita and PS1 content.
    show_template_disabled(*request.context, "PSM-like");
}

void handle_psp_like_template(const CustomInstallRequest& request)
{
    PKGI_UNUSED(request);
    // Template based on the PSP branch in src/pkgi.cpp / src/gameview.cpp.
    show_template_disabled(*request.context, "PSP-like");
}
}

CustomInstallTemplateContext pkgi_custom_make_template_context(
        const std::string& list_name,
        const std::string& tsv_url)
{
    CustomInstallTemplateContext context;
    context.list_name = list_name;
    context.tsv_url   = tsv_url;

    // Placeholder: decide here how a custom list should map to an install flow.
    // For now this remains Unknown so future work can classify lists as
    // VitaLike / PsxLike / PsmLike / PspLike.
    context.kind = CustomInstallTemplateKind::Unknown;

    return context;
}

void pkgi_custom_open_list_template(
        const std::string& list_name,
        const std::string& tsv_url)
{
    const auto context = pkgi_custom_make_template_context(list_name, tsv_url);
    pkgi_dialog_error(
            fmt::format(
                    "Custom list selected but loading is not implemented yet.\n\n"
                    "List: {}\nTSV: {}\n\n"
                    "Use src/customhandler.cpp as the template entry point.",
                    context.list_name,
                    context.tsv_url)
                    .c_str());
}

void pkgi_custom_confirm_item_template(const CustomInstallRequest& request)
{
    switch (request.context->kind)
    {
    case CustomInstallTemplateKind::VitaLike:
        handle_vita_like_template(request);
        return;
    case CustomInstallTemplateKind::PsxLike:
        handle_psx_like_template(request);
        return;
    case CustomInstallTemplateKind::PsmLike:
        handle_psm_like_template(request);
        return;
    case CustomInstallTemplateKind::PspLike:
        handle_psp_like_template(request);
        return;
    case CustomInstallTemplateKind::Unknown:
        show_template_disabled(*request.context, "Unknown");
        return;
    }
}