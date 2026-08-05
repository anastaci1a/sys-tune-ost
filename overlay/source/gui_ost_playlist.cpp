#include "gui_ost_playlist.hpp"

#include "applet_bgm.hpp"
#include "config/config.hpp"
#include "elm_text_block.hpp"
#include "gui_browser.hpp"
#include "ost_volume_slider.hpp"
#include "ost_ui_state.hpp"
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
    : m_title_id(title_id), m_name(std::move(name)), m_startup(startup),
      m_main_startup(title_id == applet_bgm::StartupTitleId) {}

tsl::elm::Element* OstPlaylistGui::createUI() {
    m_frame = new SysTuneOverlayFrame();
    m_list = new tsl::elm::List();
    populate();
    m_frame->setDescription(m_startup
        ? "\uE0E1 Back    \uE0E0 Open    \uE0E3 Remove"
        : "\uE0E1 Back   \uE0E0 Open   \uE0E3 Remove\nZL/ZR Move");
    m_frame->setContent(m_list);
    return m_frame;
}

void OstPlaylistGui::populate() {
    m_paths = readPlaylist();
    m_known_count = m_paths.size();
    m_seen_revision = ost_ui_state::getPlaylistRevision(m_title_id);
    m_list->addItem(new tsl::elm::CategoryHeader(m_name));

    m_list->addItem(new tsl::elm::CategoryHeader("Playback Options"));
    m_list->addItem(ost_volume_ui::MakeVolumeSlider(
        "Soundtrack Volume", config::get_ost_volume(m_title_id),
        [title_id = m_title_id](float volume) {
            config::set_ost_volume(title_id, volume);
            tuneReloadOstMisc();
        }));

    if (m_title_id == applet_bgm::AlbumTitleId) {
        m_list->addItem(ost_volume_ui::MakeVolumeSlider(
            "Volume During Video Playback",
            config::get_album_video_volume(),
            [](float volume) {
                config::set_album_video_volume(volume);
                tuneReloadOstMisc();
            }));
        m_list->addItem(new ElmTextBlock(
            "Uses movie streams, including trimming; paused videos\n"
            "stay at this level. Changes use Mid-Song fades."));
    }

    if (m_main_startup) {
        m_list->addItem(new tsl::elm::CategoryHeader("Startup Behavior"));
        auto startup_on_wake = new tsl::elm::ToggleListItem(
            "Include Wake from Sleep", config::get_startup_on_wake(),
            "On", "Off");
        startup_on_wake->setStateChangedListener([](bool value) {
            config::set_startup_on_wake(value);
            tuneReloadOstMisc();
        });
        m_list->addItem(startup_on_wake);

        auto separate_wake = new tsl::elm::ToggleListItem(
            "Use Separate Wake Playlist",
            config::get_separate_wake_playlist(), "On", "Off");
        separate_wake->setStateChangedListener([this](bool value) {
            config::set_separate_wake_playlist(value);
            tuneReloadOstMisc();
            setWakePlaylistButtonVisible(value);
        });
        m_list->addItem(separate_wake);
        setWakePlaylistButtonVisible(
            config::get_separate_wake_playlist());
    } else if (!m_startup) {
        const bool shuffle_enabled = config::get_ost_shuffle(m_title_id);
        auto shuffle = new tsl::elm::ToggleListItem(
            "Shuffle on Activation", shuffle_enabled, "On", "Off");
        shuffle->setStateChangedListener([this](bool value) {
            config::set_ost_shuffle(m_title_id, value);
            markPlaylistChanged();
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
            m_list->addItem(new ElmTextBlock(
                "Home pauses for other UI states, then resumes."));
        } else {
            m_list->addItem(new ElmTextBlock(
                "The playlist restarts whenever this applet opens."));
        }
    }

    m_list->addItem(new tsl::elm::CategoryHeader("Playlist Management"));
    auto add = new tsl::elm::ListItem("Add Songs");
    add->setClickListener([this](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<BrowserGui>(m_title_id, m_name);
            return true;
        }
        return false;
    });
    m_list->addItem(add);

    auto clear = new tsl::elm::ListItem("Clear Playlist");
    clear->setClickListener([this](u64 keys) {
        if ((keys & HidNpadButton_A) && !m_paths.empty()) {
            clearPlaylist();
            return true;
        }
        return false;
    });
    m_list->addItem(clear);

    m_playlist_header = new tsl::elm::CategoryHeader("");
    m_list->addItem(m_playlist_header);
    updatePlaylistSummary();

    if (m_known_count == 0) {
        m_empty_item = new tsl::elm::ListItem("No Songs Yet", "Add Songs Above");
        m_empty_item->setValue("Add Songs Above", true);
        m_list->addItem(m_empty_item);
        return;
    }

    for (u32 index = 0; index < m_known_count; index++) {
        addTrackItem(m_paths[index]);
    }
}

std::vector<std::string> OstPlaylistGui::readPlaylist() const {
    using PathBuffer = std::array<char, applet_bgm::PathSizeMax>;
    std::vector<PathBuffer> path_buffers(applet_bgm::PlaylistMax);
    const auto loaded = config::load_ost_playlist(
        m_title_id, path_buffers.front().data(), sizeof(PathBuffer),
        path_buffers.size());

    std::vector<std::string> paths;
    paths.reserve(loaded.count);
    for (u32 index = 0; index < loaded.count; index++) {
        paths.emplace_back(path_buffers[index].data());
    }
    return paths;
}

void OstPlaylistGui::syncPlaylist() {
    auto paths = readPlaylist();
    const auto shared_count = std::min(paths.size(), m_track_items.size());
    for (size_t index = 0; index < shared_count; index++) {
        if (paths[index] != m_paths[index]) {
            m_track_items[index]->setText(FileName(paths[index].c_str()));
        }
    }

    while (m_track_items.size() > paths.size()) {
        auto* item = m_track_items.back();
        if (getFocusedElement() == item) {
            tsl::Gui::requestFocus(m_list, tsl::FocusDirection::Up, false);
        }
        m_track_items.pop_back();
        m_list->removeItem(item);
    }

    m_paths = std::move(paths);
    while (m_track_items.size() < m_paths.size()) {
        addTrackItem(m_paths[m_track_items.size()]);
    }

    if (m_paths.empty() && !m_empty_item) {
        m_empty_item = new tsl::elm::ListItem("No Songs Yet", "Add Songs Above");
        m_empty_item->setValue("Add Songs Above", true);
        m_list->addItem(m_empty_item);
    } else if (!m_paths.empty() && m_empty_item) {
        m_list->removeItem(m_empty_item);
        m_empty_item = nullptr;
    }

    m_known_count = m_paths.size();
    updatePlaylistSummary();
}

void OstPlaylistGui::addTrackItem(const std::string& path) {
    auto item = new tsl::elm::ListItem(
        FileName(path.c_str()), std::to_string(m_track_items.size() + 1));
    item->setClickListener([this, item](u64 keys) {
        const auto position =
            std::find(m_track_items.begin(), m_track_items.end(), item);
        if (position == m_track_items.end()) {
            return false;
        }
        const auto index = static_cast<u32>(position - m_track_items.begin());

        if (keys & HidNpadButton_Y) {
            removeTrack(index);
            return true;
        }
        if (!m_startup && (keys & HidNpadButton_ZL) && index > 0) {
            moveTrack(index, index - 1);
            return true;
        }
        if (!m_startup && (keys & HidNpadButton_ZR) &&
            index + 1 < m_track_items.size()) {
            moveTrack(index, index + 1);
            return true;
        }
        return false;
    });
    m_track_items.push_back(item);
    m_list->addItem(item);
}

void OstPlaylistGui::removeTrack(u32 index) {
    if (index >= m_paths.size() ||
        !config::remove_ost_playlist_item(m_title_id, index)) {
        return;
    }

    if (index + 1 == m_track_items.size()) {
        tsl::Gui::requestFocus(m_list, tsl::FocusDirection::Up, false);
    }

    m_paths.erase(m_paths.begin() + index);
    for (size_t position = index; position < m_paths.size(); position++) {
        m_track_items[position]->setText(FileName(m_paths[position].c_str()));
        m_track_items[position]->setValue(std::to_string(position + 1));
    }

    auto* removed_item = m_track_items.back();
    m_track_items.pop_back();
    m_list->removeItem(removed_item);
    m_known_count = m_paths.size();

    if (m_paths.empty()) {
        m_empty_item = new tsl::elm::ListItem("No Songs Yet", "Add Songs Above");
        m_empty_item->setValue("Add Songs Above", true);
        m_list->addItem(m_empty_item);
    }
    updatePlaylistSummary();
    markPlaylistChanged();
}

void OstPlaylistGui::moveTrack(u32 index, u32 destination) {
    if (index >= m_paths.size() || destination >= m_paths.size() ||
        !config::move_ost_playlist_item(m_title_id, index, destination)) {
        return;
    }

    std::swap(m_paths[index], m_paths[destination]);
    m_track_items[index]->setText(FileName(m_paths[index].c_str()));
    m_track_items[destination]->setText(FileName(m_paths[destination].c_str()));
    markPlaylistChanged();
    tsl::Gui::requestFocus(
        m_list, destination < index
            ? tsl::FocusDirection::Up : tsl::FocusDirection::Down,
        false);
}

void OstPlaylistGui::clearPlaylist() {
    config::clear_ost_playlist(m_title_id);
    for (auto* item : m_track_items) {
        m_list->removeItem(item);
    }
    m_track_items.clear();
    m_paths.clear();
    m_known_count = 0;

    m_empty_item = new tsl::elm::ListItem("No Songs Yet", "Add Songs Above");
    m_empty_item->setValue("Add Songs Above", true);
    m_list->addItem(m_empty_item);
    updatePlaylistSummary();
    markPlaylistChanged();
    m_frame->setToast("Playlist Cleared", m_name);
}

void OstPlaylistGui::updatePlaylistSummary() {
    m_playlist_header->setText(
        "Playlist — " + std::to_string(m_known_count) +
        (m_known_count == 1 ? " Track" : " Tracks"));
}

void OstPlaylistGui::markPlaylistChanged() {
    m_seen_revision = ost_ui_state::markPlaylistChanged(m_title_id);
}

void OstPlaylistGui::setWakePlaylistButtonVisible(bool visible) {
    if (visible && !m_wake_playlist_button) {
        const auto count = config::get_ost_playlist_size(
            applet_bgm::WakeStartupTitleId);
        const auto count_text = std::to_string(count) +
            (count == 1 ? " Track" : " Tracks");
        m_wake_playlist_button = new tsl::elm::ListItem(
            "Wake Playlist", count_text);
        m_wake_playlist_button->setValue(count_text, count == 0);
        m_wake_playlist_button->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) {
                return false;
            }
            tsl::changeTo<OstPlaylistGui>(
                applet_bgm::WakeStartupTitleId,
                std::string{"Wake Startup Sound"}, true);
            return true;
        });
        m_wake_seen_revision = ost_ui_state::getPlaylistRevision(
            applet_bgm::WakeStartupTitleId);
        // Main Startup has six rows before this conditional button.
        m_list->addItem(m_wake_playlist_button, 0, 6);
    } else if (!visible && m_wake_playlist_button) {
        m_list->removeItem(m_wake_playlist_button);
        m_wake_playlist_button = nullptr;
    }
}

void OstPlaylistGui::updateWakePlaylistButtonValue() {
    if (!m_wake_playlist_button) {
        return;
    }
    const auto count = config::get_ost_playlist_size(
        applet_bgm::WakeStartupTitleId);
    const auto count_text = std::to_string(count) +
        (count == 1 ? " Track" : " Tracks");
    if (m_wake_playlist_button->getValue() != count_text) {
        m_wake_playlist_button->setValue(count_text, count == 0);
    }
}

void OstPlaylistGui::update() {
    const auto revision = ost_ui_state::getPlaylistRevision(m_title_id);
    if (revision != m_seen_revision) {
        syncPlaylist();
        m_seen_revision = revision;
    }

    if (m_wake_playlist_button) {
        const auto wake_revision = ost_ui_state::getPlaylistRevision(
            applet_bgm::WakeStartupTitleId);
        if (wake_revision != m_wake_seen_revision) {
            m_wake_seen_revision = wake_revision;
            updateWakePlaylistButtonValue();
        }
    }
}

bool OstPlaylistGui::handleInput(
    u64 keysDown, u64, const HidTouchState&,
    HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        ost_ui_state::flushPlaylistReload(m_title_id);
    }
    return false;
}
