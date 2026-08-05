#include "gui_ost_playlist.hpp"

#include "applet_bgm.hpp"
#include "config/config.hpp"
#include "gui_browser.hpp"
#include "tune.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr std::array RepeatNames = {"Off", "One", "All"};

std::string FileName(const char* path) {
    const char* name = std::strrchr(path, '/');
    return name ? name + 1 : path;
}

}

OstPlaylistGui::OstPlaylistGui(u64 title_id, std::string name, bool startup)
    : m_title_id(title_id), m_name(std::move(name)), m_startup(startup) {}

tsl::elm::Element* OstPlaylistGui::createUI() {
    m_frame = new SysTuneOverlayFrame();
    m_list = new tsl::elm::List();
    populate();
    m_frame->setDescription(m_startup
        ? "\uE0E1 Back    \uE0E0 Open    \uE0E3 Remove"
        : "\uE0E1 Back  \uE0E0 Open  \uE0E3 Remove  ZL/ZR Move");
    m_frame->setContent(m_list);
    return m_frame;
}

void OstPlaylistGui::populate() {
    m_known_count = config::get_ost_playlist_size(m_title_id);
    m_list->addItem(new tsl::elm::CategoryHeader(m_name));

    if (m_startup) {
        m_list->addItem(new tsl::elm::CategoryHeader(
            "One sound is chosen randomly per boot", true));
        m_list->addItem(new tsl::elm::CategoryHeader(
            "HOME may begin when that sound finishes", true));
        m_list->addItem(new tsl::elm::CategoryHeader(
            "Another applet or game ends it early", true));
    } else {
        const bool shuffle_enabled = config::get_ost_shuffle(m_title_id);
        auto shuffle = new tsl::elm::ToggleListItem(
            "Shuffle on activation", shuffle_enabled, "On", "Off");
        shuffle->setStateChangedListener([this](bool value) {
            config::set_ost_shuffle(m_title_id, value);
            tuneReloadOstState(m_title_id);
        });
        m_list->addItem(shuffle);

        const auto repeat_mode = std::clamp(config::get_ost_repeat(m_title_id), 0, 2);
        auto repeat = new tsl::elm::ListItem("Repeat", RepeatNames[repeat_mode]);
        repeat->setClickListener([this, repeat](u64 keys) {
            if (keys & (HidNpadButton_A | HidNpadButton_Right | HidNpadButton_Left)) {
                auto mode = std::clamp(config::get_ost_repeat(m_title_id), 0, 2);
                mode = keys & HidNpadButton_Left ? (mode + 2) % 3 : (mode + 1) % 3;
                config::set_ost_repeat(m_title_id, mode);
                u64 active_state = applet_bgm::SilentTitleId;
                if (R_SUCCEEDED(tuneGetActiveOstState(&active_state)) &&
                    active_state == m_title_id) {
                    tuneSetRepeatMode(static_cast<TuneRepeatMode>(mode));
                }
                repeat->setValue(RepeatNames[mode]);
                return true;
            }
            return false;
        });
        m_list->addItem(repeat);

        if (m_title_id == applet_bgm::QlaunchTitleId) {
            m_list->addItem(new tsl::elm::CategoryHeader(
                "Home, Settings, and Lock share qlaunch", true));
            m_list->addItem(new tsl::elm::CategoryHeader(
                "HOME pauses for applets, then resumes", true));
        } else {
            m_list->addItem(new tsl::elm::CategoryHeader(
                "Restarts whenever this applet opens", true));
        }
    }

    auto add = new tsl::elm::ListItem("Add songs");
    add->setClickListener([this](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<BrowserGui>(m_title_id, m_name);
            return true;
        }
        return false;
    });
    m_list->addItem(add);

    auto clear = new tsl::elm::ListItem("Clear playlist");
    clear->setClickListener([this](u64 keys) {
        if ((keys & HidNpadButton_A) && config::get_ost_playlist_size(m_title_id) != 0) {
            config::clear_ost_playlist(m_title_id);
            tuneReloadOstState(m_title_id);
            m_frame->setToast("Playlist cleared", m_name);
            requestRebuild();
            return true;
        }
        return false;
    });
    m_list->addItem(clear);

    m_list->addItem(new tsl::elm::CategoryHeader(
        "Playlist — " + std::to_string(m_known_count) +
        (m_known_count == 1 ? " track" : " tracks")));

    if (m_known_count == 0) {
        auto empty = new tsl::elm::ListItem("No songs yet", "Add songs above");
        empty->setValue("Add songs above", true);
        m_list->addItem(empty);
        return;
    }

    char path[applet_bgm::PathSizeMax]{};
    for (u32 index = 0; index < m_known_count; index++) {
        if (!config::get_ost_playlist_item(m_title_id, index, path, sizeof(path))) {
            continue;
        }

        auto item = new tsl::elm::ListItem(
            FileName(path), std::to_string(index + 1));
        item->setClickListener([this, index](u64 keys) {
            if (keys & HidNpadButton_Y) {
                if (config::remove_ost_playlist_item(m_title_id, index)) {
                    tuneReloadOstState(m_title_id);
                    requestRebuild();
                }
                return true;
            }

            if (!m_startup && (keys & HidNpadButton_ZL) && index > 0) {
                if (config::move_ost_playlist_item(m_title_id, index, index - 1)) {
                    tuneReloadOstState(m_title_id);
                    requestRebuild();
                }
                return true;
            }

            if (!m_startup && (keys & HidNpadButton_ZR) && index + 1 < m_known_count) {
                if (config::move_ost_playlist_item(m_title_id, index, index + 1)) {
                    tuneReloadOstState(m_title_id);
                    requestRebuild();
                }
                return true;
            }
            return false;
        });
        m_list->addItem(item);
    }
}

void OstPlaylistGui::requestRebuild() {
    m_rebuild_pending = true;
}

void OstPlaylistGui::update() {
    static u8 tick = 0;
    if ((tick++ % 15) == 0 &&
        config::get_ost_playlist_size(m_title_id) != m_known_count) {
        m_rebuild_pending = true;
    }

    if (m_rebuild_pending) {
        tsl::Gui::removeFocus();
        m_list->clear();
        populate();
        m_rebuild_pending = false;
    }
}
