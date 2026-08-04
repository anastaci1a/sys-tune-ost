#include "gui_applet_bgm.hpp"

#include "applet_bgm.hpp"
#include "config/config.hpp"
#include "gui_browser.hpp"
#include "tune.h"

#include <cstring>

namespace {

std::string TrackName(u64 title_id) {
    char path[FS_MAX_PATH]{};
    if (!config::get_applet_bgm_path(title_id, path, sizeof(path))) {
        return "Not set";
    }

    const char* name = std::strrchr(path, '/');
    std::string display_name = name ? name + 1 : path;
    if (display_name.size() > 18) {
        display_name.resize(15);
        display_name += "...";
    }
    return display_name;
}

}

tsl::elm::Element* AppletBgmGui::createUI() {
    m_frame = new SysTuneOverlayFrame();
    m_list = new tsl::elm::List();
    m_track_items.clear();

    auto enabled = new tsl::elm::ToggleListItem(
        "Applet BGM", config::get_applet_bgm_enabled(), "On", "Off");
    enabled->setStateChangedListener([](bool value) {
        config::set_applet_bgm_enabled(value);
        tuneReloadAppletBgm();
    });
    m_list->addItem(enabled);

    m_list->addItem(new tsl::elm::CategoryHeader(
        "Mapped track loops; games are silent", true));
    m_list->addItem(new tsl::elm::CategoryHeader(
        "HOME + Settings share one track", true));

    addTarget(applet_bgm::QlaunchTarget.title_id, applet_bgm::QlaunchTarget.name);
    for (const auto& target : applet_bgm::Targets) {
        addTarget(target.title_id, target.name);
    }

    m_frame->setDescription("\uE0E1  Back     \uE0E0  Set     \uE0E3  Clear");
    m_frame->setContent(m_list);
    return m_frame;
}

void AppletBgmGui::addTarget(u64 title_id, const char* name) {
    const auto track_name = TrackName(title_id);
    auto item = new tsl::elm::ListItem(name, track_name);
    item->setValue(track_name, track_name == "Not set");
    item->setClickListener([this, item, title_id, name](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<BrowserGui>(title_id, std::string{name});
            return true;
        }

        if (keys & HidNpadButton_Y) {
            config::set_applet_bgm_path(title_id, "");
            tuneReloadAppletBgm();
            item->setValue("Not set", true);
            m_frame->setToast("Applet BGM cleared", name);
            return true;
        }

        return false;
    });

    m_track_items.emplace_back(title_id, item);
    m_list->addItem(item);
}

void AppletBgmGui::updateTargetValue(u64 title_id, tsl::elm::ListItem* item) {
    const auto track_name = TrackName(title_id);
    if (item->getValue() != track_name) {
        item->setValue(track_name, track_name == "Not set");
    }
}

void AppletBgmGui::update() {
    static u8 tick = 0;
    if ((tick++ % 15) != 0) {
        return;
    }

    for (const auto& [title_id, item] : m_track_items) {
        updateTargetValue(title_id, item);
    }
}
