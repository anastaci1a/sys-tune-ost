#include "gui_applet_bgm.hpp"

#include "applet_bgm.hpp"
#include "config/config.hpp"
#include "elm_text_block.hpp"
#include "gui_ost_playlist.hpp"
#include "ost_ui_state.hpp"

namespace {

std::string PlaylistSize(u32 count) {
    return std::to_string(count) + (count == 1 ? " track" : " tracks");
}

}

tsl::elm::Element* AppletBgmGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    m_list = new tsl::elm::List();
    m_playlist_items.clear();

    m_list->addItem(new tsl::elm::CategoryHeader("Overview"));
    m_list->addItem(new ElmTextBlock(
        "Each UI state has its own playlist.\n"
        "Games and regular applications stay silent"));

    addTarget(applet_bgm::StartupTitleId, "Startup Sound", true);
    addTarget(applet_bgm::LockStateId, "Lock Screen");
    addTarget(applet_bgm::QlaunchTitleId, "Home Menu");
    addTarget(applet_bgm::LoadingStateId, "Loading Screen");
    addTarget(applet_bgm::SettingsStateId, "System Settings");
    for (const auto& target : applet_bgm::Targets) {
        addTarget(target.title_id, target.name);
    }
    m_global_revision = ost_ui_state::getGlobalRevision();

    frame->setDescription("\uE0E1  Back     \uE0E0  Edit Playlist");
    frame->setContent(m_list);
    return frame;
}

void AppletBgmGui::addTarget(u64 title_id, const char* name, bool startup) {
    const auto track_count = config::get_ost_playlist_size(title_id);
    const auto count_text = PlaylistSize(track_count);
    auto item = new tsl::elm::ListItem(name, count_text);
    item->setValue(count_text, track_count == 0);
    item->setClickListener([title_id, name, startup](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<OstPlaylistGui>(title_id, std::string{name}, startup);
            return true;
        }

        return false;
    });

    m_playlist_items.push_back({
        .title_id = title_id,
        .item = item,
        .revision = ost_ui_state::getPlaylistRevision(title_id),
    });
    m_list->addItem(item);
}

void AppletBgmGui::updateTargetValue(PlaylistItem& playlist_item) {
    const auto track_count =
        config::get_ost_playlist_size(playlist_item.title_id);
    const auto count_text = PlaylistSize(track_count);
    if (playlist_item.item->getValue() != count_text) {
        playlist_item.item->setValue(count_text, track_count == 0);
    }
}

void AppletBgmGui::update() {
    const auto global_revision = ost_ui_state::getGlobalRevision();
    if (global_revision == m_global_revision) {
        return;
    }

    for (auto& playlist_item : m_playlist_items) {
        const auto revision =
            ost_ui_state::getPlaylistRevision(playlist_item.title_id);
        if (revision != playlist_item.revision) {
            playlist_item.revision = revision;
            updateTargetValue(playlist_item);
        }
    }
    m_global_revision = global_revision;
}
