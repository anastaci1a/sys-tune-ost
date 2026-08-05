#include "gui_browser.hpp"

#include "applet_bgm.hpp"
#include "config/config.hpp"
#include "tune.h"

#include <utility>

namespace {

    bool ListItemTextCompare(const tsl::elm::ListItem *_lhs, const tsl::elm::ListItem *_rhs) {
        return strcasecmp(_lhs->getText().c_str(), _rhs->getText().c_str()) < 0;
    };

    bool StringTextCompare(const std::string& _lhs, const std::string& _rhs) {
        return strcasecmp(_lhs.c_str(), _rhs.c_str()) < 0;
    };

    ALWAYS_INLINE bool EndsWith(const char *name, const char *ext) {
        if (std::strlen(name) < std::strlen(ext)) {
            return false;
        }
        return strcasecmp(name + std::strlen(name) - std::strlen(ext), ext) == 0;
    }

    constexpr const std::array SupportedTypes = {
#ifdef WANT_MP3
        ".mp3",
#endif
#ifdef WANT_FLAC
        ".flac",
#endif
#ifdef WANT_WAV
        ".wav",
        ".wave",
#endif
    };

    bool SupportsType(const char *name) {
        for (auto &ext : SupportedTypes)
            if (EndsWith(name, ext))
                return true;
        return false;
    }

    constexpr const char *const base_path = "/music/";

    char path_buffer[FS_MAX_PATH];

}


BrowserGui::BrowserGui(u64 ost_state, std::string ost_name)
    : m_fs(), has_music(), cwd("/"), m_ost_state(ost_state),
      m_ost_name(std::move(ost_name)) {
    this->m_list = new tsl::elm::List();

    /* Open sd card filesystem. */
    Result rc = fsOpenSdCardFileSystem(&this->m_fs);
    if (R_SUCCEEDED(rc)) {
        /* Check if base path /music/ exists. */
        FsDir dir;
        std::strcpy(this->cwd, base_path);
        if (R_SUCCEEDED(fsFsOpenDirectory(&this->m_fs, this->cwd, FsDirOpenMode_ReadFiles, &dir))) {
            this->has_music = true;
            fsDirClose(&dir);
        } else {
            this->cwd[1] = '\0';
        }
        this->scanCwd();
    } else {
        this->m_list->addItem(new tsl::elm::CategoryHeader("Couldn't open SdCard"));
    }
}

BrowserGui::~BrowserGui() {
    fsFsClose(&this->m_fs);
}

tsl::elm::Element *BrowserGui::createUI() {
    m_frame = new SysTuneOverlayFrame();

    m_frame->setDescription("\uE0E1 Back   \uE0E0 Add   \uE0E2 Add folder");
    m_frame->setContent(this->m_list);

    return m_frame;
}

bool BrowserGui::handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        if (this->has_music && this->cwd[7] == '\0') {
            return false;
        } else if (this->cwd[1] != '\0') {
            this->upCwd();
            return true;
        }
    } else if (keysDown & HidNpadButton_X) {
        this->addAllToPlaylist();
        return true;
    }
    return false;
}

void BrowserGui::scanCwd() {
    tsl::Gui::removeFocus();
    this->m_list->clear();

    this->m_list->addItem(new tsl::elm::CategoryHeader(
        "Add to: " + m_ost_name, true));

    /* Show absolute folder path. */
    this->m_list->addItem(new tsl::elm::CategoryHeader(this->cwd, true));

    /* Open directory. */
    FsDir dir;
    Result rc = fsFsOpenDirectory(&this->m_fs, this->cwd, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &dir);
    if (R_FAILED(rc)) {
        char result_buffer[0x10];
        std::snprintf(result_buffer, 0x10, "2%03X-%04X", R_MODULE(rc), R_DESCRIPTION(rc));
        this->m_list->addItem(new tsl::elm::ListItem("something went wrong :/"));
        this->m_list->addItem(new tsl::elm::ListItem(result_buffer));
        return;
    }
    tsl::hlp::ScopeGuard dirGuard([&] { fsDirClose(&dir); });

    std::vector<tsl::elm::ListItem *> folders, files;

    /* Iternate over directory. */
    s64 count = 0;
    const u64 max = 2048; // max items to be added to the array.
    std::vector<FsDirectoryEntry> entries(64);

    // avoid vector allocs / resize in the loop.
    folders.reserve(max);
    files.reserve(max);

    while (R_SUCCEEDED(fsDirRead(&dir, &count, entries.size(), entries.data())) && count) {
        for (s64 i = 0; i < count; i++) {
            if (folders.size() + files.size() >= max) {
                break;
            }

            const auto& entry = entries[i];
            if (entry.type == FsDirEntryType_Dir) {
                /* Add directory entries. */
                auto item = new tsl::elm::ListItem(entry.name);
                item->setClickListener([this, item](u64 down) -> bool {
                    if (down & HidNpadButton_A) {
                        const auto length = std::strlen(this->cwd);
                        const auto name = item->getText();
                        if (length + name.size() + 1 >= sizeof(this->cwd)) {
                            m_frame->setToast("Path is too long", "Choose a folder closer to the SD root.");
                            return true;
                        }
                        std::snprintf(
                            this->cwd + length, sizeof(this->cwd) - length,
                            "%s/", name.c_str());
                        this->scanCwd();
                        return true;
                    }
                    return false;
                });
                folders.push_back(item);
            } else if (SupportsType(entry.name)) {
                /* Add file entry. */
                auto item = new tsl::elm::ListItem(entry.name);
                item->setClickListener([this, item](u64 down) -> bool {
                    if (down & HidNpadButton_A) {
                        std::snprintf(path_buffer, sizeof(path_buffer), "%s%s", this->cwd, item->getText().c_str());
                        if (std::strlen(path_buffer) >= applet_bgm::PathSizeMax) {
                            m_frame->setToast("Path is too long", "Keep the full music path below 256 bytes.");
                        } else if (config::append_ost_playlist_item(m_ost_state, path_buffer)) {
                            tuneReloadOstState(m_ost_state);
                            m_frame->setToast("Playlist updated", item->getText().c_str());
                        } else {
                            m_frame->setToast("Couldn't add song", "Playlist full or path is invalid.");
                        }
                        return true;
                    }
                    return false;
                });
                files.push_back(item);
            }
        }

        if (folders.size() + files.size() >= max) {
            m_frame->setToast("Stopped scanning folder", "maximum of " + std::to_string(max) + " hit");
            break;
        }
    }
    if (folders.size() == 0 && files.size() == 0) {
        this->m_list->addItem(new tsl::elm::CategoryHeader("Empty..."));
        return;
    }

    tsl::elm::ListItem* focus_elm = nullptr;

    if (folders.size() > 0) {
        std::sort(folders.begin(), folders.end(), ListItemTextCompare);

        focus_elm = folders[0];

        for (auto element : folders)
            this->m_list->addItem(element);
    }
    if (files.size() > 0) {
        this->m_list->addItem(new tsl::elm::CategoryHeader("Files"));
        std::sort(files.begin(), files.end(), ListItemTextCompare);

        if (!focus_elm)
            focus_elm = files[0];

        for (auto element : files)
            this->m_list->addItem(element);
    }

    if (focus_elm)
        tsl::Gui::requestFocus(focus_elm, tsl::FocusDirection::None);
}

void BrowserGui::upCwd() {
    size_t length = std::strlen(this->cwd);
    if (length <= 1)
        return;

    for (size_t i = length - 1; i-- > 0;) {
        if (this->cwd[i] == '/') {
            this->cwd[i + 1] = '\0';
            this->scanCwd();
            return;
        }
    }
}

void BrowserGui::addAllToPlaylist() {
    FsDir dir;
    Result rc = fsFsOpenDirectory(&this->m_fs, this->cwd, FsDirOpenMode_ReadFiles|FsDirOpenMode_NoFileSize, &dir);
    if (R_FAILED(rc)) {
        char result_buffer[0x10];
        std::snprintf(result_buffer, 0x10, "2%03X-%04X", R_MODULE(rc), R_DESCRIPTION(rc));
        this->m_list->addItem(new tsl::elm::ListItem("something went wrong :/"));
        this->m_list->addItem(new tsl::elm::ListItem(result_buffer));
        return;
    }
    tsl::hlp::ScopeGuard dirGuard([&] { fsDirClose(&dir); });

    std::vector<std::string> file_list;
    s64 songs_added = 0;
    s64 count = 0;
    const u64 max = applet_bgm::PlaylistMax;
    std::vector<FsDirectoryEntry> entries(64);

    // avoid vector allocs / resize in the loop.
    file_list.reserve(max);

    while (R_SUCCEEDED(fsDirRead(&dir, &count, entries.size(), entries.data())) && count) {
        for (s64 i = 0; i < count; i++) {
            const auto& entry = entries[i];
            if (entry.type == FsDirEntryType_File && SupportsType(entry.name)){
                file_list.emplace_back(entry.name);

                if (file_list.size() >= max) {
                    break;
                }
            }
        }

        if (file_list.size() >= max) {
            break;
        }
    }

    std::sort(file_list.begin(), file_list.end(), StringTextCompare);
    for (auto const & file : file_list) {
        std::snprintf(path_buffer, sizeof(path_buffer), "%s%s", this->cwd, file.c_str());
        if (config::append_ost_playlist_item(m_ost_state, path_buffer)) {
            songs_added++;
        }
    }

    if (songs_added != 0) {
        tuneReloadOstState(m_ost_state);
    }

    std::snprintf(path_buffer, sizeof(path_buffer), "Added %ld songs to Playlist.", songs_added);
    m_frame->setToast("Playlist updated", path_buffer);
}
