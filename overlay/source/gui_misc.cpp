#include "gui_misc.hpp"

#include "duration_slider.hpp"
#include "elm_overlayframe.hpp"
#include "elm_text_block.hpp"
#include "gui_album_video_diagnostics.hpp"
#include "gui_power_state_diagnostics.hpp"
#include "gui_quick_settings_settings.hpp"
#include "gui_qlaunch_scene_diagnostics.hpp"
#include "gui_ui_activity_diagnostics.hpp"
#include "config/config.hpp"

tsl::elm::Element* MiscGui::createUI() {
    auto frame = new SysTuneOverlayFrame();
    auto list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Context Settings"));
    auto* quick_settings = new tsl::elm::ListItem("Quick Settings");
    quick_settings->setClickListener([](u64 keys) {
        if (!(keys & HidNpadButton_A)) {
            return false;
        }
        tsl::changeTo<QuickSettingsSettingsGui>();
        return true;
    });
    list->addItem(quick_settings);

    list->addItem(new tsl::elm::CategoryHeader("Track Boundaries"));
    list->addItem(new ElmTextBlock(
        "Used when tracks start or end naturally.\n"
        "Tracks do not overlap."));
    list->addItem(duration_ui::MakeDurationSlider(
        "Fade In", config::get_fade_in_ms(), config::set_fade_in_ms));
    list->addItem(duration_ui::MakeDurationSlider(
        "Fade Out", config::get_fade_out_ms(), config::set_fade_out_ms));

    list->addItem(new tsl::elm::CategoryHeader("Mid-Song Transitions"));
    list->addItem(new ElmTextBlock(
        "Used when playback pauses or resumes,\n"
        "including Home Menu transitions."));
    list->addItem(duration_ui::MakeDurationSlider(
        "Mid-Song Fade In", config::get_mid_song_fade_in_ms(),
        config::set_mid_song_fade_in_ms));
    list->addItem(duration_ui::MakeDurationSlider(
        "Mid-Song Fade Out", config::get_mid_song_fade_out_ms(),
        config::set_mid_song_fade_out_ms));

    list->addItem(new tsl::elm::CategoryHeader("Experimental"));
    list->addItem(new ElmTextBlock(
        "Inspect state-detection signals.\n"
        "These pages do not alter playlists."));
    auto diagnostics = new tsl::elm::ListItem("Qlaunch Scene Diagnostics");
    diagnostics->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<QlaunchSceneDiagnosticsGui>();
            return true;
        }
        return false;
    });
    list->addItem(diagnostics);

    auto album_diagnostics = new tsl::elm::ListItem(
        "Album Video Diagnostics");
    album_diagnostics->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<AlbumVideoDiagnosticsGui>();
            return true;
        }
        return false;
    });
    list->addItem(album_diagnostics);

    auto power_diagnostics = new tsl::elm::ListItem(
        "Power-State Diagnostics");
    power_diagnostics->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<PowerStateDiagnosticsGui>();
            return true;
        }
        return false;
    });
    list->addItem(power_diagnostics);

    auto ui_diagnostics = new tsl::elm::ListItem(
        "UI Activity Diagnostics");
    ui_diagnostics->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<UiActivityDiagnosticsGui>();
            return true;
        }
        return false;
    });
    list->addItem(ui_diagnostics);

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 Adjust");
    frame->setContent(list);
    return frame;
}
