#include "gui_quick_settings_settings.hpp"

#include "config/config.hpp"
#include "duration_slider.hpp"
#include "elm_overlayframe.hpp"
#include "elm_text_block.hpp"
#include "ost_volume_slider.hpp"
#include "tune.h"

tsl::elm::Element* QuickSettingsSettingsGui::createUI() {
    auto* frame = new SysTuneOverlayFrame();
    auto* list = new tsl::elm::List();

    list->addItem(new tsl::elm::CategoryHeader("Quick Settings Volume"));
    list->addItem(new ElmTextBlock(
        "Lowers OST in the Switch panel.\n"
        "Ultrahand is unaffected."));

    auto* enabled = new tsl::elm::ToggleListItem(
        "Lower OST", config::get_quick_settings_enabled(), "On", "Off");
    enabled->setStateChangedListener([](bool value) {
        config::set_quick_settings_enabled(value);
        tuneReloadOstMisc();
    });
    list->addItem(enabled);

    list->addItem(ost_volume_ui::MakeVolumeSlider(
        "OST Multiplier", config::get_quick_settings_volume(),
        [](float volume) {
            config::set_quick_settings_volume(volume);
            tuneReloadOstMisc();
        },
        1.f));

    list->addItem(new tsl::elm::CategoryHeader("HOME Hold Detection"));
    list->addItem(new ElmTextBlock(
        "Panel visibility is not exposed.\n"
        "Adjust if lowering begins early or late."));
    list->addItem(duration_ui::MakeDurationSlider(
        "Hold Time", config::get_quick_settings_hold_ms(),
        duration_ui::QuickSettingsHoldDurations,
        config::set_quick_settings_hold_ms));

    frame->setDescription("\uE0E1 Back   \uE07A/\uE079 Adjust");
    frame->setContent(list);
    return frame;
}
