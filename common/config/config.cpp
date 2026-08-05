#include "config.hpp"
#include "applet_bgm.hpp"
#include "sdmc/sdmc.hpp"
#include "minIni/minIni.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace config {

namespace {

const char CONFIG_PATH[]{"/config/sys-tune/config.ini"};

void create_config_dir() {
    /* Creating directory on every set call looks sus, but the user may delete the dir */
    /* whilst the sys-mod is running and then any changes made via the overlay */
    /* is lost, which sucks. */
    sdmc::CreateFolder("/config");
    sdmc::CreateFolder("/config/sys-tune");
}

auto get_tid_str(u64 tid) -> const char* {
    static char buf[21]{};
    std::sprintf(buf, "%016lX", tid);
    return buf;
}

void get_ost_section(u64 tid, char* out, size_t out_size) {
    std::snprintf(out, out_size, "ost_%016lX", tid);
}

void get_ost_item_key(u32 index, char* out, size_t out_size) {
    std::snprintf(out, out_size, "item_%03u", index);
}

struct OstPlaylistBrowseContext {
    char section[32]{};
    char* out{};
    size_t item_stride{};
    u32 max_items{};
    OstPlaylistConfig config{};
};

int browse_ost_playlist(const char* section, const char* key,
                        const char* value, void* user_data) {
    auto* context = static_cast<OstPlaylistBrowseContext*>(user_data);
    if (std::strcmp(section, context->section) != 0) {
        return 1;
    }

    if (std::strcmp(key, "count") == 0) {
        char* end{};
        const auto parsed = std::strtol(value, &end, 10);
        if (end != value && *end == '\0') {
            context->config.count = std::clamp<long>(
                parsed, 0, context->max_items);
        }
        return 1;
    }

    if (std::strcmp(key, "shuffle") == 0) {
        const auto first = value[0];
        if (first == '1' || first == 'y' || first == 'Y' ||
            first == 't' || first == 'T') {
            context->config.shuffle = true;
        } else if (first == '0' || first == 'n' || first == 'N' ||
                   first == 'f' || first == 'F') {
            context->config.shuffle = false;
        }
        return 1;
    }

    if (std::strcmp(key, "repeat") == 0) {
        char* end{};
        const auto parsed = std::strtol(value, &end, 10);
        if (end != value && *end == '\0') {
            context->config.repeat = std::clamp<long>(parsed, 0, 2);
        }
        return 1;
    }

    if (std::strncmp(key, "item_", 5) != 0) {
        return 1;
    }

    char* end{};
    const auto index = std::strtoul(key + 5, &end, 10);
    if (end == key + 5 || *end != '\0' || index >= context->max_items) {
        return 1;
    }

    auto* destination = context->out + index * context->item_stride;
    std::snprintf(destination, applet_bgm::PathSizeMax, "%s", value);
    return 1;
}

}

auto get_shuffle() -> bool {
    return ini_getbool("config", "shuffle", false, CONFIG_PATH);
}

void set_shuffle(bool value) {
    create_config_dir();
    ini_putl("config", "shuffle", value, CONFIG_PATH);
}

auto get_repeat() -> int {
    return ini_getl("config", "repeat", 1, CONFIG_PATH);
}

void set_repeat(int value) {
    create_config_dir();
    ini_putl("config", "repeat", value, CONFIG_PATH);
}

auto get_volume() -> float {
    return ini_getf("config", "volume", 1.f, CONFIG_PATH);
}

void set_volume(float value) {
    create_config_dir();
    ini_putf("config", "volume", value, CONFIG_PATH);
}

auto has_title_enabled(u64 tid) -> bool {
    return ini_haskey("title", get_tid_str(tid), CONFIG_PATH);
}

auto get_title_enabled(u64 tid) -> bool {
    return ini_getbool("title", get_tid_str(tid), true, CONFIG_PATH);
}

void set_title_enabled(u64 tid, bool value) {
    create_config_dir();
    ini_putl("title", get_tid_str(tid), value, CONFIG_PATH);
}

auto get_title_enabled_default() -> bool {
    return ini_getbool("title", "default", true, CONFIG_PATH);
}

void set_title_enabled_default(bool value) {
    create_config_dir();
    ini_putl("title", "default", value, CONFIG_PATH);
}

auto has_title_volume(u64 tid) -> bool {
    return ini_haskey("volume", get_tid_str(tid), CONFIG_PATH);
}

auto get_title_volume(u64 tid) -> float {
    return ini_getf("volume", get_tid_str(tid), 1.f, CONFIG_PATH);
}

void set_title_volume(u64 tid, float value) {
    create_config_dir();
    ini_putf("volume", get_tid_str(tid), value, CONFIG_PATH);
}

auto get_default_title_volume() -> float {
    return ini_getf("config", "global_volume", 1.f, CONFIG_PATH);
}

void set_default_title_volume(float value) {
    create_config_dir();
    ini_putf("config", "global_volume", value, CONFIG_PATH);
}

auto get_load_path(char* out, int max_len) -> int {
    return ini_gets("config", "load_path", "", out, max_len, CONFIG_PATH);
}

void set_load_path(const char* path) {
    create_config_dir();
    ini_puts("config", "load_path", path, CONFIG_PATH);
}

auto get_applet_bgm_enabled() -> bool {
    return ini_getbool("applet_bgm", "enabled", false, CONFIG_PATH);
}

void set_applet_bgm_enabled(bool value) {
    create_config_dir();
    ini_putl("applet_bgm", "enabled", value, CONFIG_PATH);
}

auto get_applet_bgm_path(u64 tid, char* out, int max_len) -> int {
    return ini_gets("applet_bgm", get_tid_str(tid), "", out, max_len, CONFIG_PATH);
}

void set_applet_bgm_path(u64 tid, const char* path) {
    create_config_dir();
    ini_puts("applet_bgm", get_tid_str(tid), path, CONFIG_PATH);
}

auto get_ost_playlist_size(u64 tid) -> u32 {
    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    const auto count = ini_getl(section, "count", 0, CONFIG_PATH);
    return std::clamp<long>(count, 0, applet_bgm::PlaylistMax);
}

auto get_ost_playlist_item(u64 tid, u32 index, char* out, int max_len) -> int {
    if (index >= get_ost_playlist_size(tid)) {
        if (max_len > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    char section[32]{};
    char key[16]{};
    get_ost_section(tid, section, sizeof(section));
    get_ost_item_key(index, key, sizeof(key));
    return ini_gets(section, key, "", out, max_len, CONFIG_PATH);
}

auto load_ost_playlist(u64 tid, char* out, size_t item_stride,
                       u32 max_items) -> OstPlaylistConfig {
    if (!out || item_stride < applet_bgm::PathSizeMax || max_items == 0) {
        return {};
    }

    max_items = std::min(max_items, applet_bgm::PlaylistMax);
    for (u32 i = 0; i < max_items; i++) {
        out[i * item_stride] = '\0';
    }

    OstPlaylistBrowseContext context{
        .out = out,
        .item_stride = item_stride,
        .max_items = max_items,
    };
    get_ost_section(tid, context.section, sizeof(context.section));
    if (!ini_browse(browse_ost_playlist, &context, CONFIG_PATH)) {
        return {};
    }
    return context.config;
}

auto append_ost_playlist_item(u64 tid, const char* path) -> bool {
    if (!path || path[0] == '\0' || std::strlen(path) >= applet_bgm::PathSizeMax) {
        return false;
    }

    const auto count = get_ost_playlist_size(tid);
    if (count >= applet_bgm::PlaylistMax) {
        return false;
    }

    char section[32]{};
    char key[16]{};
    get_ost_section(tid, section, sizeof(section));
    get_ost_item_key(count, key, sizeof(key));
    create_config_dir();
    if (!ini_puts(section, key, path, CONFIG_PATH)) {
        return false;
    }
    return ini_putl(section, "count", count + 1, CONFIG_PATH) != 0;
}

auto remove_ost_playlist_item(u64 tid, u32 index) -> bool {
    const auto count = get_ost_playlist_size(tid);
    if (index >= count) {
        return false;
    }

    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    create_config_dir();

    char path[applet_bgm::PathSizeMax]{};
    char key[16]{};
    for (u32 i = index; i + 1 < count; i++) {
        get_ost_playlist_item(tid, i + 1, path, sizeof(path));
        get_ost_item_key(i, key, sizeof(key));
        ini_puts(section, key, path, CONFIG_PATH);
    }

    get_ost_item_key(count - 1, key, sizeof(key));
    ini_puts(section, key, "", CONFIG_PATH);
    return ini_putl(section, "count", count - 1, CONFIG_PATH) != 0;
}

auto move_ost_playlist_item(u64 tid, u32 src, u32 dst) -> bool {
    const auto count = get_ost_playlist_size(tid);
    if (src >= count || dst >= count || src == dst) {
        return false;
    }

    char src_path[applet_bgm::PathSizeMax]{};
    char dst_path[applet_bgm::PathSizeMax]{};
    if (!get_ost_playlist_item(tid, src, src_path, sizeof(src_path)) ||
        !get_ost_playlist_item(tid, dst, dst_path, sizeof(dst_path))) {
        return false;
    }

    char section[32]{};
    char src_key[16]{};
    char dst_key[16]{};
    get_ost_section(tid, section, sizeof(section));
    get_ost_item_key(src, src_key, sizeof(src_key));
    get_ost_item_key(dst, dst_key, sizeof(dst_key));
    create_config_dir();
    return ini_puts(section, src_key, dst_path, CONFIG_PATH) != 0 &&
           ini_puts(section, dst_key, src_path, CONFIG_PATH) != 0;
}

void clear_ost_playlist(u64 tid) {
    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    create_config_dir();
    ini_putl(section, "count", 0, CONFIG_PATH);
}

auto get_ost_shuffle(u64 tid) -> bool {
    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    return ini_getbool(section, "shuffle", false, CONFIG_PATH);
}

void set_ost_shuffle(u64 tid, bool value) {
    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    create_config_dir();
    ini_putl(section, "shuffle", value, CONFIG_PATH);
}

auto get_ost_repeat(u64 tid) -> int {
    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    return std::clamp<long>(ini_getl(section, "repeat", 2, CONFIG_PATH), 0, 2);
}

void set_ost_repeat(u64 tid, int value) {
    char section[32]{};
    get_ost_section(tid, section, sizeof(section));
    create_config_dir();
    ini_putl(section, "repeat", std::clamp(value, 0, 2), CONFIG_PATH);
}

auto get_fade_in_ms() -> u32 {
    return std::clamp<long>(ini_getl("ost_manager", "fade_in_ms", 500, CONFIG_PATH), 0, 5000);
}

void set_fade_in_ms(u32 value) {
    create_config_dir();
    ini_putl("ost_manager", "fade_in_ms", std::min(value, 5000u), CONFIG_PATH);
}

auto get_fade_out_ms() -> u32 {
    return std::clamp<long>(ini_getl("ost_manager", "fade_out_ms", 500, CONFIG_PATH), 0, 5000);
}

void set_fade_out_ms(u32 value) {
    create_config_dir();
    ini_putl("ost_manager", "fade_out_ms", std::min(value, 5000u), CONFIG_PATH);
}

auto get_startup_on_wake() -> bool {
    return ini_getbool("ost_manager", "startup_on_wake", false, CONFIG_PATH);
}

void set_startup_on_wake(bool value) {
    create_config_dir();
    ini_putl("ost_manager", "startup_on_wake", value, CONFIG_PATH);
}

void migrate_ost_config() {
    if (ini_getl("ost_manager", "migration_version", 0, CONFIG_PATH) >= 1) {
        return;
    }

    const auto migrate_track = [](u64 tid) {
        if (get_ost_playlist_size(tid) != 0) {
            return;
        }

        char path[applet_bgm::PathSizeMax]{};
        if (get_applet_bgm_path(tid, path, sizeof(path))) {
            append_ost_playlist_item(tid, path);
        }
    };

    migrate_track(applet_bgm::QlaunchTitleId);
    for (const auto& target : applet_bgm::Targets) {
        migrate_track(target.title_id);
    }

    if (get_ost_playlist_size(applet_bgm::StartupTitleId) == 0) {
        char load_path[applet_bgm::PathSizeMax]{};
        if (get_load_path(load_path, sizeof(load_path)) && sdmc::FileExists(load_path)) {
            append_ost_playlist_item(applet_bgm::StartupTitleId, load_path);
        }
    }

    create_config_dir();
    ini_putl("ost_manager", "migration_version", 1, CONFIG_PATH);
}

}
