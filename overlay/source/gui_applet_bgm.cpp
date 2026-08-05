#include "gui_applet_bgm.hpp"

#include "applet_bgm.hpp"
#include "config/config.hpp"
#include "gui_ost_playlist.hpp"

namespace {

std::string PlaylistSize(u64 title_id) {
    const auto count = config::get_ost_playlist_size(title_id);
    return std::to_string(count) + (count == 1 ? " track" : " tracks");
}

}

tsl::elm::Element* AppletBgmGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    m_list = new tsl::elm::List();
    m_playlist_items.clear();

    m_list->addItem(new tsl::elm::CategoryHeader(
        "Each UI state has its own playlist", true));
    m_list->addItem(new tsl::elm::CategoryHeader(
        "Games and regular applications stay silent", true));

    addTarget(applet_bgm::StartupTitleId, "Startup Sound", true);
    addTarget(applet_bgm::QlaunchTarget.title_id, applet_bgm::QlaunchTarget.name);
    for (const auto& target : applet_bgm::Targets) {
        addTarget(target.title_id, target.name);
    }

    frame->setDescription("\uE0E1  Back     \uE0E0  Edit playlist");
    frame->setContent(m_list);
    return frame;
}

void AppletBgmGui::addTarget(u64 title_id, const char* name, bool startup) {
    const auto count = PlaylistSize(title_id);
    auto item = new tsl::elm::ListItem(name, count);
    item->setValue(count, config::get_ost_playlist_size(title_id) == 0);
    item->setClickListener([title_id, name, startup](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<OstPlaylistGui>(title_id, std::string{name}, startup);
            return true;
        }

        return false;
    });

    m_playlist_items.emplace_back(title_id, item);
    m_list->addItem(item);
}

void AppletBgmGui::updateTargetValue(u64 title_id, tsl::elm::ListItem* item) {
    const auto count = PlaylistSize(title_id);
    if (item->getValue() != count) {
        item->setValue(count, config::get_ost_playlist_size(title_id) == 0);
    }
}

void AppletBgmGui::update() {
    static u8 tick = 0;
    if ((tick++ % 15) != 0) {
        return;
    }

    for (const auto& [title_id, item] : m_playlist_items) {
        updateTargetValue(title_id, item);
    }
}
